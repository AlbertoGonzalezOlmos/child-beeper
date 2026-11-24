# BSP API reference (items 1–5 — frozen surface)

> Status: item-5 SD API frozen by SD1–SD14; its full gate, root 0.4.0 capture and card evidence are
> PENDING. Item 4 is build-verified (**13 pass, 0 fail, 2 skip**) and its IMU serial capture is
> recorded in verification.md. Item 3's RGB/button/battery evidence is also recorded there. Only
> the names on this page are frozen; later self-test APIs require their own review round. Do not
> invent `cb_*` names ahead of their item.

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
#define CB_BSP_VERSION_MINOR 4
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
const char *cb_bsp_version(void);        /* "0.4.0" */
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
- The vendor's CTRL9 host-command path contains an **unbounded** `while(...)` wait. Item 1 did not
  use CTRL9; item 4 requires the vendor's **0x12 AHB-clock-gating command** but replaces that wait
  with a bounded 100 ms STATUSINT poll.
- The driver probes `CB_QMI_ADDR_0 0x6A`, then `CB_QMI_ADDR_1 0x6B` if needed, with
  `i2c_master_probe()` and logs which answered. A WHO_AM_I mismatch logs a WARN and fails optional
  IMU init with `ESP_ERR_INVALID_RESPONSE`; it never aborts the process.

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
  `LV_COLOR_FORMAT_RGB565`, `swap_bytes = true`, and — **measured on hardware, commit 92e336d** —
  `.io_handle` **must be passed** (esp_lvgl_port 2.9.0 asserts `disp_cfg->io_handle != NULL` at
  `esp_lvgl_port_disp.c:120`; omitting it boot-looped the board) and
  **`buff_dma = true` with `buff_spiram = false`**: two **internal** DMA-capable RGB565 draw
  buffers of 172×40 px (2 × 13,760 B = **27,520 B internal SRAM**). The port **rejects**
  `buff_dma + buff_spiram` at runtime with `ESP_ERR_NOT_SUPPORTED` on this SoC — it compiles but
  must never be used. Port-init and display-add success are tracked separately (A8) so a failed
  `add_disp` can be retried without spawning a second LVGL task (retry path runtime-untested).
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
  parts are implemented. In that historical item-2 state RGB/I2C/IMU/BUTTON/BATTERY/SD still logged
  WARN `not implemented`; item 3 removes RGB/BUTTON/BATTERY from that list, leaving I2C/IMU/SD for
  items 4/5.
- Idempotency (brief ruling 3): a second call logs “already initialized” per part and returns
  `ESP_OK`; failed parts are not marked, so a later call retries them.

## RGB, button and battery (item 3 — R1–R8, R1b–R12b)

### `cb_rgb_math.h` — pure hue-preserving peak cap

```c
#include <stdint.h>
#define CB_RGB_DIM_CAP_PERCENT 20
void cb_rgb_math_dim(uint8_t *r, uint8_t *g, uint8_t *b);
```

The helper has no IDF includes and has `extern "C"` guards for the item-7 Arduino mirror. If any
pointer is NULL it returns without writing anything. Otherwise read all three inputs, compute
`peak=max(r,g,b)` and `cap=255*CB_RGB_DIM_CAP_PERCENT/100` (**51**). If `peak<=cap`, leave all values
unchanged. Otherwise replace each channel with `(uint32_t)channel*cap/peak` (floor), using the
original channels for every calculation. This is a cap, not uniform percent scaling, and it is
idempotent. Required vectors include `(255,255,255)->(51,51,51)`, `(255,128,0)->(51,25,0)`,
`(52,0,0)->(51,0,0)`, `(20,10,5)` and `(1,1,1)` unchanged, and `dim(dim(x))==dim(x)`.

### `cb_rgb.h` — one WS2812-compatible pixel

```c
#include "esp_err.h"
#include <stdint.h>
esp_err_t cb_rgb_init(void);
esp_err_t cb_rgb_set(uint8_t r, uint8_t g, uint8_t b);
esp_err_t cb_rgb_off(void);
```

- Init creates the led_strip 3.1.1 RMT device and its mutex. The exact configuration fields are
  `.strip_gpio_num=CB_PIN_RGB`, `.max_leds=1`, `.led_model=LED_MODEL_WS2812`, and
  `.color_component_format=LED_STRIP_COLOR_COMPONENT_FMT_GRB`; RMT uses `RMT_CLK_SRC_DEFAULT` at
  **10 MHz** (`.mem_block_symbols=64`, DMA disabled). Init clears the pixel through the same
  set+refresh path as `cb_rgb_off()` so the LED is
  deterministically off at boot, then logs `I cb_rgb: rmt up gpio38, cap 20%`.
- Init is idempotent. Partial failure releases any acquired device/mutex (`led_strip_del()` and
  `vSemaphoreDelete()`), leaves no initialized state, returns the first error, and permits retry.
- Set applies `cb_rgb_math_dim` to local channels. `cb_rgb_off()` is exactly `cb_rgb_set(0,0,0)`.
  Before successful init both return `ESP_ERR_INVALID_STATE`; that pre-init WARN is one-shot and is
  re-armed by a later successful init. The mutex covers `led_strip_set_pixel()` +
  `led_strip_refresh()` and is taken with a **100 ms** timeout; timeout returns `ESP_ERR_TIMEOUT` and
  logs at most once per second. Set-failure WARNs are also limited to 1 Hz. Rate-limit and one-shot
  state is checked/updated inside a critical section, while the log call itself happens outside it.
  The first driver error is returned and the mutex is always released. No getter, clear variant,
  uncapped bypass, or driver handle is public. RGB calls are task-context only and must not be made
  while holding `cb_lcd_lvgl_lock()`.

### `cb_button.h` — BOOT GPIO wrapper over the frozen FSM

```c
#include "cb_button_fsm.h"
#include "esp_err.h"
#define CB_BUTTON_POLL_MS 10
typedef void (*cb_button_cb_t)(cb_btn_event_t event, void *user);
esp_err_t cb_button_init(void);
esp_err_t cb_button_set_callback(cb_button_cb_t cb, void *user);
```

- Init configures GPIO0 with `gpio_config()` as `GPIO_MODE_INPUT`, pull-up enabled, pull-down and
  interrupts disabled. It is never driven as an output. It initialises the existing `cb_btn_fsm_t`,
  creates one `ESP_TIMER_TASK` esp_timer, and starts it periodically at **10,000 µs**. GPIO/timer
  failure returns the error and cleans up any created timer; successful init is idempotent.
- Each poll reads `gpio_get_level(CB_PIN_BOOT_BTN)`, inverts the active-low level, and calls
  `cb_btn_fsm_update()` with `(uint32_t)(esp_timer_get_time()/1000ULL)`. Init seeds the resting
  level once before starting the timer; thereafter only the timer callback mutates the FSM. The
  frozen deferred-CLICK/LONG_PRESS semantics above are unchanged.
- `cb_button_set_callback()` **always stores the callback/user pair and returns `ESP_OK`**, before or
  after init and even when the button part is down; NULL disables delivery. Pair replacement and the
  callback+user snapshot take place inside a critical section. The callback itself is invoked
  **outside** that critical section in esp_timer task context (never ISR). It must return promptly;
  queue SD, RGB sequences, LVGL, or other blocking work to the application task. There is no deinit,
  pressed getter, or second debounce implementation.

### `cb_battery_math.h` — pure conversion and plausibility

```c
#include <stdbool.h>
#include <stdint.h>
#define CB_BAT_PLAUSIBLE_MIN_MV 2500
#define CB_BAT_PLAUSIBLE_MAX_MV 4500
int32_t cb_battery_math_mv_from_pin_mv(int32_t pin_mv);
bool cb_battery_math_plausible(int32_t battery_mv);
```

For nonpositive input the conversion returns zero. Otherwise it uses an **int64** intermediate:

```text
battery_mv = pin_mv * CB_BAT_DIVIDER_NUM * CB_BAT_CAL_GAIN_NUM
             / (CB_BAT_DIVIDER_DEN * CB_BAT_CAL_GAIN_DEN)
```

The result is truncating integer **millivolts**, with no `/1000`, and saturates at `INT32_MAX`.
Exact host vectors include `695→2100`, `1000→3021`, `1333→4027`, and `4095→12373`.
Plausibility is inclusive:
`battery_mv>=2500 && battery_mv<=4500`. It is only a heuristic for the no-cell warning, never a
fuel-gauge percentage or proof that a cell is present.

### `cb_battery.h` — ADC1 oneshot sample

```c
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    int raw;
    int32_t pin_mv;
    int32_t battery_mv;
    bool calibrated;
    bool plausible;
} cb_battery_sample_t;
esp_err_t cb_battery_init(void);
esp_err_t cb_battery_read(cb_battery_sample_t *out);
```

- Init binds `CB_BAT_ADC_UNIT`/`CB_BAT_ADC_CHANNEL` to `ADC_UNIT_1`/`ADC_CHANNEL_0` with static
  assertions, then calls `adc_oneshot_new_unit()` and `adc_oneshot_config_channel()` with
  `ADC_ATTEN_DB_12`, `ADC_BITWIDTH_12`, `ADC_DIGI_CLK_SRC_DEFAULT`, and `ADC_ULP_MODE_DISABLE`.
  Unit/channel failure deletes any acquired unit, returns the first error, and permits retry;
  successful init is idempotent.
- ESP32-S3 calibration uses **curve fitting only**: `adc_cali_curve_fitting_config_t` (including
  `.chan=ADC_CHANNEL_0`) and `adc_cali_create_scheme_curve_fitting()`. Calibration creation failure
  logs one WARN and falls back to `cb_math_adc_raw_to_mv()` without discarding a working ADC.
- `cb_battery_read(NULL)` returns `ESP_ERR_INVALID_ARG`; pre-init returns `ESP_ERR_INVALID_STATE`
  and emits its pre-init WARN only once (re-armed by successful init). A successful read obtains raw
  ADC data with `adc_oneshot_read()`, then either `adc_cali_raw_to_voltage()` or the frozen raw
  fallback, and fills all five fields from that one sample; `out` is untouched on driver error.
  Reads are serialized by an internal mutex with a 100 ms bound (`ESP_ERR_TIMEOUT` on timeout);
  the busy WARN is limited to 1 Hz and its last-log state is updated under a critical section. An
  implausible result still returns `ESP_OK` with `plausible=false`.
- The **driver owns plausibility transitions**, not per-read data output. The transition decision
  and `s_warned_implausible` update occur inside the read mutex so concurrent sample state cannot be
  reordered. On transition to implausible it logs
  `W cb_battery: implausible %ld mV (no cell inserted?)`; on recovery it logs
  `I cb_battery: reading plausible again`. Stable polling cannot repeat these messages. Examples
  own their own periodic sample lines; the driver prints no per-read data line.

### `cb_bsp_init` deltas (item 3)

- `CB_PART_RGB` → `cb_rgb_init`, `CB_PART_BUTTON` → `cb_button_init`, and
  `CB_PART_BATTERY` → `cb_battery_init`, in the existing fixed positions. All remain optional:
  failures WARN and are suppressed; `k_parts[]` order and the `CB_PART_ALL` assertions are
  unchanged.
- In item 3, `CB_BSP_VERSION_MAJOR/MINOR/PATCH` were **0/2/0**, so `cb_bsp_version()` returned
  `"0.2.0"`. The root app source remained unchanged: no `bat=`/`rgb=` heartbeat keys and no root
  button handler. The item-3 first-heartbeat summary contained
  `cb_bsp=0.2.0 init(CB_PART_ALL)=ESP_OK` when the strict LCD/LVGL parts succeeded; the current
  item-4 expectation is `cb_bsp=0.3.0 init(CB_PART_ALL)=ESP_OK`. Neither line proves optional-part
  health.
- After item 3, I2C, IMU and SD remain requested-but-not-yet-implemented parts. Item 4 removes
  I2C/IMU from that list, leaving only SD for item 5. Every repeated warning path added by item 3
  is one-shot, transition-gated, or rate-limited to **≤1 Hz** (R14b),
  with race-free last-log state. In particular, the RGB example logs
  `W rgb_button: cycle %u failed: %s` only on the first failure of a failure streak and clears its
  error-active flag on the next successful cycle.
- **R13b battery diagnostic addition:** on a `cb_battery_read` error, `03_battery` emits exactly
  `W battery: read failed: %s` once, substituting `esp_err_to_name(err)`, then stops polling. Its
  normal sample line remains `battery: raw=%d pin_mv=%ld battery_mv=%ld calibrated=%d plausible=%d`
  every 2 s and `battery: alive #%u` continues every 5 s, so a dead ADC is distinguishable from a
  wedged app. This failure path has not been hardware-injected; it is a code/build contract only.

## I2C and QMI8658 IMU (item 4 — R9–R27)

### `cb_i2c.h` — shared new-driver bus

```c
#include "driver/i2c_master.h"
#include "esp_err.h"
esp_err_t cb_i2c_init(void);
i2c_master_bus_handle_t cb_i2c_bus(void);  /* NULL until successful init */
```

These are the **only** public I2C functions: no probe helper, recovery/reset API, deinit or public
lock API. `CB_PART_I2C` owns the one bus; `CB_PART_IMU` requires it and fails
`ESP_ERR_INVALID_STATE` if `cb_i2c_bus()` is NULL. `cb_i2c_bus()` returns the shared handle after
successful init and NULL before it. Public `REQUIRES` includes `esp_driver_i2c` because this header
exposes the IDF handle type.

`cb_i2c_init()` is idempotent after success and calls `i2c_new_master_bus()` on the initial or
retry attempt with `.i2c_port=CB_I2C_PORT` (0), `.sda_io_num=CB_PIN_I2C_SDA` (48),
`.scl_io_num=CB_PIN_I2C_SCL` (47), `.clk_source=I2C_CLK_SRC_DEFAULT`,
`.glitch_ignore_cnt=7`, `.intr_priority=0`, `.trans_queue_depth=0`,
`.flags.enable_internal_pullup=true` and `.flags.allow_pd=false`. Internal pull-ups are logged with
the exact substring `i2c: internal pull-ups enabled`; their electrical suitability at 400 kHz is
**hardware-PENDING**. Failure returns the bus-creation error, sets the cached handle to NULL and
permits retry. A second call after successful init logs `already initialized` and returns `ESP_OK`
without creating another bus.

### `cb_imu_math.h` — pure burst decode

```c
#include <stdbool.h>
#include <stdint.h>
#define CB_IMU_RAW_LEN 14
typedef struct {
    float ax_g, ay_g, az_g;
    float gx_dps, gy_dps, gz_dps;
    float temp_c;
} cb_imu_sample_t;
bool cb_imu_math_decode(const uint8_t *raw, unsigned accel_range_g,
                        unsigned gyro_range_dps, cb_imu_sample_t *out);
```

The struct has exactly those fields in that order: X/Y/Z acceleration in **g**, X/Y/Z angular rate
in **degrees/s**, then die temperature in **°C**. There is no timestamp or separate temperature
call. `CB_IMU_RAW_LEN` is the public named length contract (`CB_QMI_GZ_H-CB_QMI_TEMP_L+1 == 14`).

The decode reads a burst beginning at `CB_QMI_TEMP_L` (0x33), in this exact slot order:
TEMP[0..1], AX[2..3], AY[4..5], AZ[6..7], GX[8..9], GY[10..11], GZ[12..13]. All values are
little-endian. Accel and gyro use the existing `cb_math_le_i16()` followed by
`cb_math_accel_lsb_to_g()` / `cb_math_gyro_lsb_to_dps()` — `raw*range/32768.0f`. Temperature uses
`cb_math_qmi_temp_c()` — signed 16-bit two's complement / 256 °C. No new sensitivity or LSB table
is introduced.

`cb_imu_math_decode` has no length parameter, so its caller must guarantee all 14 bytes are
readable. It returns false for NULL raw/out, accel range outside {2,4,8,16}, or gyro range outside
{16,32,64,128,256,512,1024}. Every failure leaves `*out` **byte-identical**. Success computes a
local sample and assigns it only after all seven fields are complete. `INT16_MIN` is decoded by the
existing sign-safe helper, never by signed negation. The public constant is host-tested with
`CB_IMU_RAW_LEN == 14`.

### `cb_imu.h` — QMI8658 device

```c
#include "cb_imu_math.h"
#include "esp_err.h"
esp_err_t cb_imu_init(void);
esp_err_t cb_imu_read(cb_imu_sample_t *out);
int cb_imu_addr(void);           /* 0 before full init, otherwise 0x6A or 0x6B */
uint8_t cb_imu_who_am_i(void);   /* last WHO byte; meaningful when cb_imu_addr() != 0 */
```

#### Frozen init/probe/configuration path (R21 + measured `a05fded` amendments)

1. Successful init is idempotent. No bus returns `ESP_ERR_INVALID_STATE`; no second bus is created
   implicitly. Create the one IMU mutex used to serialize WHO/configuration transfers and sample
   reads (100 ms acquisition bound) and clean it up on every failure.
2. Probe **0x6A then 0x6B** using `i2c_master_probe(bus,addr,CB_QMI_COMM_TIMEOUT_MS)` and stop at
   the first ACK. If neither answers, clean up and return `ESP_ERR_NOT_FOUND`. Log the outcome.
3. Add the selected device with `i2c_master_bus_add_device()` using
   `.dev_addr_length=I2C_ADDR_BIT_LEN_7`, `.device_address=winner`,
   `.scl_speed_hz=CB_I2C_FREQ_HZ` (400000), `.scl_wait_us=0`, and ACK checking enabled. Any
   post-add failure calls `i2c_master_bus_rm_device()`, deletes the mutex and leaves addr/WHO at
   their not-initialized zero values.
4. Read `CB_QMI_WHO_AM_I` with `i2c_master_transmit_receive`. A mismatch produces a WARN and
   `ESP_ERR_INVALID_RESPONSE`, never a process abort; `cb_bsp` handles the optional-part error.
   Read/config errors remove the device and mutex. Complete success logs
   `cb_imu: up addr=0x%02X who=0x%02X range=8g/512dps odr=%uHz`.
5. Configure in this exact order, every transfer bounded by `CB_QMI_COMM_TIMEOUT_MS` (50 ms):
   - **CTRL1**: write `CB_QMI_CTRL1_ADDR_AI` (**0x40**, bit 6) before any burst. Reset default 0x00
     would repeatedly read one register rather than advancing through all 14 bytes. **Measured on
     this part 2026-11-23** (fixed in `a05fded`): 0x80 repeated the first byte
     (`burst35=41 41 41 41 41 41`), while 0x40 returned real data (`3F F1 69 03 38 FB`); see
     [verification.md](verification.md) checklist 8.
   - **CTRL9 / CH9**: immediately after CTRL1, issue
     `CB_QMI_CTRL_CMD_AHB_CLOCK_GATING` (**0x12**) and poll STATUSINT bit 7 in 5 ms steps with a
     **100 ms bound**. This command is **required on the measured part**: without it the output
     registers returned a moving counter ([verification.md](verification.md) checklist 8). Timeout
     returns `ESP_ERR_TIMEOUT`; never use the vendor's unbounded wait.
   - **CTRL2**: read-modify-write with `CB_QMI_ASCALE_MASK/OFFSET` and `CB_QMI_AODR_MASK`, then set
     `CB_QMI_ACC_RANGE_8G` and selected ODR. At 120 Hz the range/ODR bits compose to **0x26**;
     bits outside those masks are preserved.
   - **CTRL3**: same RMW pattern with `CB_QMI_GSCALE_MASK/OFFSET` and `CB_QMI_GODR_MASK`, using
     `CB_QMI_GYR_RANGE_512DPS`; at 120 Hz the range/ODR bits compose to **0x56**, again preserving
     unmasked bits.
   - **CTRL5**: clear `CB_QMI_ALPF_MASK` and `CB_QMI_GLPF_MASK` using
     `reg &= (uint8_t)~MASK`, then apply `CB_QMI_LPF_MODE_0` (**0x00**, bypass) to both channels.
     Never use logical `!MASK`.
   - **CTRL7**: write `CB_QMI_CTRL7_ACC_EN | CB_QMI_CTRL7_GYR_EN` (**0x03**) last, enabling both
     sensors. The only added register macros are `CB_QMI_CTRL1_ADDR_AI`, `CB_QMI_CTRL7_ACC_EN`
     and `CB_QMI_CTRL7_GYR_EN`.
   - **Readback**: after a 50 ms settle, read revision, CTRL1, CTRL2, CTRL3, CTRL7 and CTRL9, then
     emit the permanent one-line boot readback quoted in the serial contract below.
6. Set addr/WHO only after the whole sequence succeeds. Post-add failures return the originating
   error (or `ESP_ERR_INVALID_RESPONSE` for WHO mismatch), remove the device/mutex and leave
   identity at zero so a later `cb_bsp_init` can retry without stale presence.

Kconfig is exactly a named choice (not a derived integer):

```kconfig
choice CB_IMU_ODR
    prompt "QMI8658 IMU output data rate"
    default CB_IMU_ODR_120
    config CB_IMU_ODR_120
        bool "120 Hz"
    config CB_IMU_ODR_250
        bool "250 Hz"
    config CB_IMU_ODR_1000
        bool "1000 Hz"
endchoice
```

The default is `CONFIG_CB_IMU_ODR_120=y`; the driver maps the three symbols to the matching
`CB_QMI_*_ODR_NORM_*` macros with `#if defined(...)`. Accel and gyro ranges stay fixed at ±8 g and
±512 dps, with no range Kconfig and no runtime range switching.

#### Read/getter semantics

- `cb_imu_read(NULL)` returns `ESP_ERR_INVALID_ARG`; pre-init returns `ESP_ERR_INVALID_STATE`.
  `out` is untouched on every error. Mutex timeout is `ESP_ERR_TIMEOUT` (100 ms), with first-event
  allowed, ≤1 Hz gated WARN and race-free last-log state.
- Read starts at `CB_QMI_TEMP_L` and performs one
  `i2c_master_transmit_receive(s_dev,&reg,1,raw,CB_IMU_RAW_LEN,CB_QMI_COMM_TIMEOUT_MS)`.
  A bus error is returned unchanged and the sample is untouched. On success decode at fixed 8 g /
  512 dps and return `ESP_OK`, even for a noisy sample. The driver prints **no per-read data line**.
- `cb_imu_addr()` returns 0 before full init, otherwise the winning address as `int`.
  `cb_imu_who_am_i()` returns the cached actual WHO byte when addr != 0. No runtime range setter is
  added.

### `cb_bsp_init` deltas (item 4)

- `CB_PART_I2C` maps to `cb_i2c_init` and `CB_PART_IMU` to `cb_imu_init`, in the existing
  I2C-before-IMU positions. Both remain optional/WARN-suppressed; all requested parts are attempted.
  In item 4, SD was the only remaining unimplemented part; item 5 wires it as shown below.
- `CB_BSP_VERSION_MAJOR/MINOR/PATCH` becomes **0/3/0**, so `cb_bsp_version()` returns `"0.3.0"`.
  Root source remains unchanged. The expected root boot-summary substring is
  `cb_bsp=0.3.0 init(CB_PART_ALL)=ESP_OK` when strict parts succeed.
- The component's explicit `SRCS` list contains `src/cb_i2c.c`, `src/cb_imu.c` and
  `src/cb_imu_math.c`. `REQUIRES` is `esp_lcd esp_driver_i2c`; the other private driver
  requirements are unchanged.

### `04_imu` serial contract (R23 + measured init diagnostics)

Successful driver init includes these two boot diagnostic lines before `cb_imu: up` and the
example's `ready` line:

```text
I cb_imu: ctrl9 cmd 0x%02X done in %d ms
I cb_imu: readback revision=0x%02X ctrl1=0x%02X ctrl2=0x%02X ctrl3=0x%02X ctrl7=0x%02X ctrl9=0x%02X
```

Once after successful init:

```text
I imu: ready addr=0x%02X who=0x%02X
```

Every **2 s** while polling:

```text
I imu: ax=%+.3f ay=%+.3f az=%+.3f gx=%+.2f gy=%+.2f gz=%+.2f temp_c=%+.2f
```

Alive is `I imu: alive #%u` first at +5 s and then every 5 s, including degraded operation. A read
failure prints `W imu: read failed: %s` once, then polling stops and alive continues. If
`cb_imu_addr()==0`, the example prints exactly
`W imu: no IMU found (probed 0x6A/0x6B), samples skipped` once, suppresses sample lines and keeps
alive running. There is no abort or fake no-device PASS.

Resting acceptance values for the README/coordinator are: max |accel axis| **0.9–1.1 g**, other two
axes **<0.15 g**, every |gyro component| **<2.00 dps**, and temperature **10–50 °C**. These are
sanity checks, not full-scale calibration or axis-orientation proof.

## TF card over SDMMC (item 5 — SD1–SD14)

### `cb_sd.h` — mount, state and capacity

```c
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
esp_err_t cb_sd_init(void);
esp_err_t cb_sd_unmount(void);              /* idempotent: ESP_OK when nothing is mounted */
bool cb_sd_mounted(void);
int cb_sd_bus_width(void);                  /* negotiated 4 or 1; 0 when unmounted */
const char *cb_sd_mount_point(void);        /* always "/sdcard" */
esp_err_t cb_sd_info(uint64_t *total_bytes, uint64_t *free_bytes);
```

These six functions are the whole public surface: no `cb_sd_card()`, `cb_sd_base_path()`,
`cb_sd_info_t`, format or mkfs API, and no public SDMMC types. `cb_sd_mount_point()` returns the
frozen literal in every state. `CB_PART_SD` maps to `cb_sd_init` in its existing optional, last
position; SD mounting remains outside the strict LCD/LVGL path. All nine parts are now implemented,
and `CB_BSP_VERSION_MAJOR/MINOR/PATCH` is **0/4/0**, so the expected root summary substring is
`cb_bsp=0.4.0 init(CB_PART_ALL)=ESP_OK` when the strict parts succeed. Optional SD failure is still
suppressed and is not proof of card health.

### One mount site and no-format invariant (SD3/SD8)

`cb_sd.c` has exactly **one textual** `esp_vfs_fat_sdmmc_mount()` call, inside a width-parameterized
helper used for both attempts. Its mount configuration is:

```c
esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
    .format_if_mount_failed = false,   /* the only mount site: never format, ever */
    .max_files = 5,
    .allocation_unit_size = 16 * 1024,
};
```

The explicit false field and single call site are code-review invariants documented here and in
verification.md. The existing forbidden `format_if_mount_failed = true` audit remains the only
machine rule; no new static scan is added. A failed mount returns an error and never formats,
partitions, or aborts.

The host starts as `SDMMC_HOST_DEFAULT()` with `host.slot = SDMMC_HOST_SLOT_1`,
`host.max_freq_khz = CONFIG_CB_SD_MAX_FREQ_KHZ` (default **20000**), and initial flags
`SDMMC_HOST_FLAG_4BIT | SDMMC_HOST_FLAG_DEINIT_ARG`. `DEINIT_ARG` is required because the default
union supplies `.deinit_p`. The slot starts as `SDMMC_SLOT_CONFIG_DEFAULT()` but overrides the
ESP32-S3 defaults with CLK14, CMD15, D0=16, D1=18, D2=17, D3=21, no card-detect or write-protect,
width 4, and `SDMMC_SLOT_FLAG_INTERNAL_PULLUP` (the exact IDF 6.1 spelling). Internal pull-up
suitability is a hardware-pending electrical detail; there is no separate SD pull-up log line.

On a 4-bit failure the driver logs exactly
`W cb_sd: 4-bit mount failed: %s — retrying 1-bit`, calls `sdmmc_host_deinit()`, and retries once
through the same helper with `SDMMC_HOST_FLAG_1BIT | SDMMC_HOST_FLAG_DEINIT_ARG` and slot width 1.
There is no third width attempt **per `cb_sd_init()` call** and no force-1-bit knob. After a failed
`cb_bsp_init`, the example may make one explicit re-probe to surface the actual error; it does not
keep retrying in the background. Success reports the negotiated width from
`1 << card->log_bus_width`, not merely the requested width, and logs
`I cb_sd: up mount=/sdcard width=%d max_freq_khz=%d`. If both attempts fail, the actual final error
is returned with no card pointer, mounted=false and width=0; the first-event-gated line is
`W cb_sd: mount failed: %s (no card?)`. **A mount error is not proof that no card exists.**

The only item-5 Kconfig addition is:

```kconfig
config CB_SD_MAX_FREQ_KHZ
    int "SDMMC maximum clock frequency (kHz)"
    range 400 40000
    default 20000
```

There is no frequency macro in `cb_pins.h` and no format/timeout/force-width knob. The frequency is
a configured maximum, not a measured clock.

### State, info, unmount and locking semantics

- `cb_sd_init()` is idempotent: a mounted card returns `ESP_OK` and logs
  `I cb_sd: already initialized`. Failed init leaves mounted=false and width=0 so a later call can
  retry with fresh host/slot state.
- `cb_sd_mounted()` and `cb_sd_bus_width()` are silent getters. Mounted/width snapshots are
  published and read under a `portMUX_TYPE` critical section so the pair cannot tear. No mutex is
  held across the blocking mount or unmount calls.
- `cb_sd_info()` uses `esp_vfs_fat_info("/sdcard", &total, &free)` and reports raw **bytes**, not
  KiB/MiB. NULL either output returns `ESP_ERR_INVALID_ARG`; unmounted returns
  `ESP_ERR_INVALID_STATE` with one-shot gated `W cb_sd: not mounted`. Outputs are untouched on both
  errors. Capacity/free values are snapshots, not constants. Both outputs are assigned together
  only after `esp_vfs_fat_info` succeeds, so they remain untouched on every error.
- `cb_sd_unmount()` is idempotent and requires callers to close their files first. For a mounted
  card it calls `esp_vfs_fat_sdcard_unmount("/sdcard", s_card)` and then `sdmmc_host_deinit()`.
  Success clears card/mounted/width and logs `I cb_sd: unmounted`. An error logs
  `W cb_sd: unmount failed: %s`, returns the IDF error, keeps the mounted state, and does not
  pretend cleanup succeeded. Do not call unmount after a failed mount.
- Final mount-failure and pre-mount info warnings are one-shot or first-event-allowed, ≤1 Hz gated
  with race-free state. SD6 requires every repeated WARN, including fallback and unmount retries,
  to receive equivalent review-verified gating. The driver owns init/transition lines only; the
  example owns capacity and file-data lines.

### `05_sdcard` serial and round-trip contract (SD10–SD12)

After mount and successful info, the example prints once:

```text
I sdcard: ready width=%d mount=%s total_bytes=%llu free_bytes=%llu
```

The round-trip uses `/sdcard/cb05_rt%03u.tmp`, starting at 000 and incrementing on `EEXIST` through
099. It opens with `O_CREAT | O_EXCL | O_WRONLY`, so an existing file is never truncated or reused.
Each of **three rounds, 10 s apart**, writes **4096 bytes** of `(uint8_t)(i * 31 + 7)`, reads them
back, and compares byte-for-byte. Only the newly created path is unlinked; the example never
enumerates or touches other files. A successful compare and successful unlink produce exactly:

```text
I sdcard: roundtrip ok path=%s bytes=4096 verified=1 cleanup=deleted
```

`cleanup=deleted` is never printed when unlink fails. Three successful rounds are followed by one
unmount. `W sdcard: roundtrip failed: %s` is one-shot, then polling stops. `I sdcard: alive #%u` starts at about +5 s and repeats about every 5 s using an
**accumulating** deadline (`next_alive_us += PERIOD`), including after polling stops. When the part
cannot be brought up, the example emits `W sdcard: no card or mount failed: %s` once and continues
heartbeat-only operation. The successful unmount line belongs to the driver (`cb_sd: unmounted`),
not a duplicate example line. Card presence is unknown on this bench: ready/mounted I/O and fallback
evidence remain PENDING unless the coordinator captures them.

## Threading / LVGL rules

- LVGL is not thread-safe: touch LVGL objects **only** inside
  `cb_lcd_lvgl_lock(timeout_ms)` / `cb_lcd_lvgl_unlock()` (`timeout_ms = 0` waits forever).
  The LVGL task is owned by `esp_lvgl_port` (priority 4) — never call `lv_timer_handler()`
  yourself, and keep `CONFIG_LV_OS_NONE=y` (asserted by `./cb verify`).
- The `LV_EVENT_REFR_READY` backlight hook runs inside the LVGL task; LEDC calls are task-safe.
- The led_strip device is not re-entrant: item 3 wraps set+refresh in a mutex.
- The BSP starts no hidden tasks other than the LVGL port task and (item 3) the button timer.
