# BSP API reference (item 1 — frozen surface)

> Status: build-verified (item 1, 2026-11-08); runtime behaviour is **not hardware-verified** —
> see [verification.md](verification.md). Only the names on this page are frozen by the item-1
> contract; driver APIs for LCD/RGB/IMU/SD are added in items 2–6 via review rounds. Do not invent
> `cb_*` names ahead of their item.

Component: `components/cb_bsp` (local IDF component). Consumers depend on it by path, e.g.
`examples/<NN>/main/idf_component.yml`:

```yaml
dependencies:
  idf: ">=6.1.0,<6.2.0"
  cb_bsp:
    path: ../../../components/cb_bsp   # manifest-relative: THREE ups from examples/<NN>/main/
```

(the root app's `main/idf_component.yml` uses ONE up: `path: ../components/cb_bsp`).
Headers are flat: `#include "cb_bsp.h"`, `#include "cb_pins.h"`, … (the component exports
`INCLUDE_DIRS include`). Item 1 ships **no Kconfig knobs** (deferred to items 4/5, which own them).

## `cb_bsp.h` — lifecycle

```c
#define CB_BSP_VERSION_MAJOR 0
#define CB_BSP_VERSION_MINOR 1
#define CB_BSP_VERSION_PATCH 0

typedef enum {
    CB_PART_NONE      = 0,
    CB_PART_BACKLIGHT = 1u << 0,
    CB_PART_RGB       = 1u << 1,
    CB_PART_I2C       = 1u << 2,
    CB_PART_IMU       = 1u << 3,
    CB_PART_BUTTON    = 1u << 4,
    CB_PART_BATTERY   = 1u << 5,
    CB_PART_LCD       = 1u << 6,
    CB_PART_LVGL      = 1u << 7,
    CB_PART_SD        = 1u << 8,
    CB_PART_ALL       = 0x1FF,
} cb_part_t;

esp_err_t cb_bsp_init(uint32_t parts);   /* inits only the requested parts, in fixed order */
const char *cb_bsp_version(void);        /* "0.1.0" */
```

**Fixed init order:** BACKLIGHT → RGB → I2C → IMU → BUTTON → BATTERY → LCD → LVGL → SD
(SD last: mounting can block ~1 s).

**Error policy (ruled):**
- Strict parts = {`CB_PART_LCD`, `CB_PART_LVGL`}; every other part is **optional**.
- Every requested part is attempted, in order, even after a failure.
- Optional-part failures are logged at WARN and suppressed.
- Strict-part failures are logged at ERROR and the **first** one is returned.
- A requested-but-not-yet-implemented part logs `WARN ... not implemented`; if strict, it
  contributes `ESP_ERR_NOT_SUPPORTED` to the return value.
- Therefore in item 1 `cb_bsp_init(CB_PART_ALL)` returns `ESP_ERR_NOT_SUPPORTED` — **expected**;
  apps log it and must not abort. `cb_bsp_init(0) == ESP_OK`.
- `cb_bsp_init` never aborts the boot; `ESP_ERROR_CHECK` on optional hardware inside `cb_bsp` is a
  forbidden construct (enforced by `audit-static`).

## `cb_pins.h` — pure macros, no includes, C89-safe

Single source of truth for every GPIO/geometry constant; the Arduino tree keeps byte-identical
copies (checked by `./cb verify` step `pin-drift`). Values are tabulated in
[hardware.md](hardware.md#pin-table-single-source-of-truth-cb_pinsh--do-not-renumber-anywhere-else);
highlights: `CB_LCD_SPI_HOST 1` (= `SPI2_HOST`), `CB_BAT_ADC_UNIT 0` (= `ADC_UNIT_1`), battery
`CB_BAT_DIVIDER_NUM 3` / `CB_BAT_DIVIDER_DEN 1` / `CB_BAT_CAL_GAIN_NUM 700` /
`CB_BAT_CAL_GAIN_DEN 695` with `V = cali_mv × 3/1 × 700/695 / 1000`.

## `cb_math.h` — pure math (no ESP-IDF/FreeRTOS includes, host-tested)

```c
#include <stdint.h>
#include <stdbool.h>
#define CB_MATH_BACKLIGHT_MAX 1023   /* 10-bit LEDC */
int     cb_math_duty_from_percent(int percent, int max_duty); /* 0..100 -> 0..max_duty, clamps */
int16_t cb_math_le_i16(const uint8_t *buf);                   /* little-endian signed 16 */
float   cb_math_accel_lsb_to_g(int16_t raw, unsigned range_g);
float   cb_math_gyro_lsb_to_dps(int16_t raw, unsigned range_dps);
float   cb_math_qmi_temp_c(uint16_t raw);
float   cb_math_adc_raw_to_mv(int raw, int cali_mv, bool calibrated);
```

Ruled semantics:
- `cb_math_adc_raw_to_mv` returns the **ADC-pin** voltage: `calibrated ? cali_mv :
  clamp(raw, 0, 4095) × 3300.0f / 4095.0f`. The battery divider (×3) and calibration gain
  (×700/695) are applied by the **battery driver (item 3)** using the `CB_BAT_*` macros — never
  inside `cb_math`.
- Scale conversions use `raw × range / 32768` (accel ranges 2/4/8/16 g; gyro ranges 16…1024 dps).
- `cb_math_duty_from_percent` clamps: ≤0 % → 0, ≥100 % → `max_duty` (so 100 % → **1023**, never
  1024 — the vendor Arduino driver's off-by-one is a documented pitfall).

## `cb_button_fsm.h` — pure FSM (host-tested; no IDF includes)

```c
typedef enum { CB_BTN_EV_NONE = 0, CB_BTN_EV_PRESS, CB_BTN_EV_RELEASE,
               CB_BTN_EV_CLICK, CB_BTN_EV_DOUBLE_CLICK, CB_BTN_EV_LONG_PRESS } cb_btn_event_t;
#define CB_BTN_DEBOUNCE_MS   20
#define CB_BTN_DOUBLE_GAP_MS 350
#define CB_BTN_LONG_MS       800
void cb_btn_fsm_init(cb_btn_fsm_t *fsm);                       /* fully initialises */
cb_btn_event_t cb_btn_fsm_update(cb_btn_fsm_t *fsm, bool level_pressed, uint32_t now_ms);
```

Ruled semantics (deferred-CLICK machine — implement/exactly this, no third variant):
- `level_pressed` is the **raw** pin level, 1 = pressed (BOOT is active-low; callers invert).
- Time is **injected** (`now_ms`); no tick function inside; all elapsed-time math is wrap-safe
  `uint32_t` subtraction. Debounce: the raw level must be stable for **≥ 20 ms**.
- **At most one event per `update()` call.**
- PRESS on the stable press edge; RELEASE on the stable release edge.
- LONG_PRESS fires **once** when a stable press reaches ≥ 800 ms; it suppresses any click; the
  following release yields only RELEASE.
- Every non-long release arms a **deferred** click: CLICK is emitted once the 350 ms double-gap
  expires with **no second qualifying RELEASE** (so single clicks arrive ~350 ms late — by design,
  no dead zone: every non-long release produces CLICK or DOUBLE_CLICK). A second press still
  *held* when the gap expires does **not** suppress the CLICK (host-tested); only a second
  release inside the gap converts the pending click into DOUBLE_CLICK.
- A second stable release within 350 ms of the first yields RELEASE then **DOUBLE_CLICK** — never
  CLICK followed by DOUBLE_CLICK.

The IDF button wrapper (item 3) feeds the FSM every 5–10 ms and dispatches callbacks; Arduino code
feeds it from `loop()` with `millis()`.

## `cb_qmi8658_regs.h` — QMI8658 register map (`CB_QMI_*` prefix)

WHO_AM_I 0x00 (expect `CB_QMI_WHO_AM_I_VAL 0x05`), REVISION_ID 0x01, CTRL1 0x02, CTRL2 0x03,
CTRL3 0x04, CTRL5 0x06, CTRL7 0x08, STATUSINT 0x2D, TEMP_L/H 0x33/0x34, AX_L…AZ_H 0x35–0x3A,
GX_L…GZ_H 0x3B–0x40, plus `CB_QMI_ALPF_MASK`/`CB_QMI_GLPF_MASK`-style masks and range/ODR enum
macros consumed by the item-4 driver.

Porting rules (vendor bugs explicitly **not** inherited — see
[troubleshooting.md](troubleshooting.md#vendor-bug-ledger)):
- Clear mask bits with bitwise complement: `reg &= (uint8_t)~CB_QMI_ALPF_MASK;` — the vendor used
  logical NOT (`!MASK`), which zeroes the whole register.
- The vendor's CTRL9 host-command path contains an **unbounded** `while(...)` wait; item 1 does not
  use CTRL9 at all, and any future use must add a timeout.
- The driver probes **both** I2C addresses (`CB_QMI_ADDR_0 0x6A`, `CB_QMI_ADDR_1 0x6B`) with
  `i2c_master_probe()` and logs which answered; WHO_AM_I mismatch is a WARN, not a hard fail.

## Threading / LVGL rules (for later items)

- LVGL is not thread-safe: once item 2 lands the display, touch LVGL objects only inside the
  port lock (`lvgl_port_lock()/unlock()` or the BSP wrapper that replaces it).
- The led_strip device is not re-entrant: item 3 wraps set+refresh in a mutex.
- The BSP starts no hidden tasks other than the documented button timer (item 3).
