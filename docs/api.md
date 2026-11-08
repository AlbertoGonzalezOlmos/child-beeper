# BSP API reference (items 1–2 — frozen surface)

> Status: build-verified (item 2, display stack); runtime behaviour is **not hardware-verified**
> until the orchestrator's flash session — see [verification.md](verification.md). Only the names
> on this page are frozen (item-1 contract + item-2 rulings A1–A10); driver APIs for RGB/IMU/SD
> are added in items 3–6 via review rounds. Do not invent `cb_*` names ahead of their item.

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

## Display stack (item 2 — rulings A1/A2/A4/A7/A8)

### `cb_lcd.h` — exactly six public symbols (A1)

```c
#include "esp_err.h"  /* + esp_lcd_panel_ops.h, lvgl.h, stdbool.h, stdint.h */
esp_err_t cb_lcd_init(void);                  /* SPI2 + panel; idempotent */
esp_err_t cb_lcd_init_lvgl(void);             /* port + display; requires cb_lcd_init(); idempotent */
esp_lcd_panel_handle_t cb_lcd_panel(void);
lv_display_t *cb_lcd_lvgl_display(void);      /* NULL until the port is up */
bool cb_lcd_lvgl_lock(uint32_t timeout_ms);   /* wraps lvgl_port_lock; 0 = wait forever */
void cb_lcd_lvgl_unlock(void);
```

- `cb_lcd_init()` brings up the panel only, in the frozen order: SPI2 bus @ `CB_LCD_PIXEL_CLK_HZ`
  (SCLK/MOSI from `cb_pins.h`, `max_transfer_sz = CB_LCD_H_RES*80*2`) → `esp_lcd_new_panel_io_spi`
  (CS/DC, mode 0, 8-bit cmd/param, queue 10) → `esp_lcd_new_panel_st7789`
  (`rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB`, 16 bpp, RST pin) → `reset` → `init` →
  `esp_lcd_panel_set_gap(panel, CB_LCD_X_GAP, CB_LCD_Y_GAP)` → `invert_color(true)` →
  `disp_on_off(true)`. Idempotent; returns errors (never aborts); the `.c` file carries
  `_Static_assert(CB_LCD_SPI_HOST == SPI2_HOST)` (ruling 1 — no raw enum casts).
- `cb_lcd_init_lvgl()` without a successful `cb_lcd_init()` returns **`ESP_ERR_INVALID_STATE`**
  (no implicit panel bring-up). On success: `lvgl_port_init(ESP_LVGL_PORT_INIT_CONFIG())` —
  **unmodified defaults (A7, verified)**: `task_priority 4`, `task_stack 7168`,
  `task_max_sleep_ms 500`, `timer_period_ms 5` — then `lvgl_port_add_disp` with
  `buffer_size = CB_LCD_H_RES * 40` **pixels**, `double_buffer = true`,
  `LV_COLOR_FORMAT_RGB565`, flags `buff_dma + buff_spiram + swap_bytes` (buffers in OCT PSRAM).
  Port-init and display-add success are tracked separately (A8) so a failed `add_disp` can be
  retried without spawning a second LVGL task.
- **Examples must not include `esp_lvgl_port.h`** — the six symbols above are the whole surface;
  all LVGL access goes through `cb_lcd_lvgl_lock()/unlock()`.

### `cb_backlight.h` (A2)

```c
esp_err_t cb_backlight_init(void);  /* LEDC CB_LCD_BL_PWM_HZ (4 kHz) / 10-bit on CB_PIN_LCD_BL;
                                       starts at DUTY 0 (GPIO46 strapping: no white flash);
                                       idempotent */
void cb_backlight_set(int percent); /* clamps 0..100; duty via cb_math_duty_from_percent(percent,
                                       CB_MATH_BACKLIGHT_MAX) → 100 % = 1023, never 1024;
                                       logged no-op before init */
int  cb_backlight_get(void);        /* last set percent */
```

**Bring-up is owned by `cb_lcd`**: after `lvgl_port_add_disp` succeeds it registers a one-shot
`LV_EVENT_REFR_READY` callback that calls `cb_backlight_set(CB_BACKLIGHT_ON_PERCENT)` once
(`#define CB_BACKLIGHT_ON_PERCENT 100` in `cb_lcd.c`). Examples must **not** enable the backlight
themselves; they may dim it later.

### `cb_pattern.h` — pure pixel-pattern generator (no IDF/LVGL includes; host-tested)

```c
void cb_pattern_fill_diagnostic(uint16_t *dst, int width, int height);
void cb_pattern_fill_solid(uint16_t *dst, int width, int height, uint16_t rgb565);
uint16_t cb_pattern_bar_color(int index);   /* index 0..7 */
```

Frozen layout (A4): 8 vertical bars, index `(int)((int64_t)x * 8 / width)`, palette
`0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0x0000` (white, yellow, cyan, green,
magenta, red, blue, black); bottom grey ramp of `height/16` rows with `v = x*255/(width-1)`;
then four **2×2 corner markers** drawn last (TL red `0xF800`, TR yellow `0xFFE0`, BL cyan
`0x07FF`, BR magenta `0xF81F`). Output is **native-endian RGB565**, row-major, packed stride
`width*2` bytes; NULL or width/height ≤ 16 is a no-op; out-of-range palette index returns
`0x0000`. No manual byte swap — the panel path's `swap_bytes` belongs to the LVGL port flush.

### `cb_bsp_init` deltas (item 2)

- `CB_PART_BACKLIGHT` → `cb_backlight_init` (optional); `CB_PART_LCD` → `cb_lcd_init` (strict,
  panel only); `CB_PART_LVGL` → `cb_lcd_init_lvgl` (strict, adds the LVGL port; requires LCD).
  Order and strict/optional policy unchanged.
- **From item 2, `cb_bsp_init(CB_PART_ALL)` returns `ESP_OK` on a good board** (A6): the strict
  parts are implemented; RGB/I2C/IMU/BUTTON/BATTERY/SD still log WARN `not implemented`.
- Idempotency (brief ruling 3): a second call logs “already initialized” per part and returns
  `ESP_OK`; failed parts are not marked, so a later call retries them.

## Threading / LVGL rules

- LVGL is not thread-safe: touch LVGL objects **only** inside
  `cb_lcd_lvgl_lock(timeout_ms)` / `cb_lcd_lvgl_unlock()` (`timeout_ms = 0` waits forever).
  The LVGL task is owned by `esp_lvgl_port` (priority 4) — never call `lv_timer_handler()`
  yourself, and keep `CONFIG_LV_OS_NONE=y` (asserted by `./cb verify`).
- The `LV_EVENT_REFR_READY` backlight hook runs inside the LVGL task; LEDC calls are task-safe.
- The led_strip device is not re-entrant: item 3 wraps set+refresh in a mutex.
- The BSP starts no hidden tasks other than the LVGL port task and (item 3) the button timer.
