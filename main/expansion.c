#include "expansion.h"

#include <math.h>
#include <stdatomic.h>
#include <string.h>

#include "cb_button_fsm.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define AUDIO_RATE 16000
#define AUDIO_FRAMES 256
#define EFFECT_WATCHDOG_MS 500

static const char *TAG = "expansion";
static design_hardware_t s_config;
static cb_btn_fsm_t s_buttons[DESIGN_MAX_BUTTONS];
static bool s_button_ready[DESIGN_MAX_BUTTONS];
static _Atomic bool s_motor_ready;
static esp_timer_handle_t s_motor_safety;
static expansion_button_cb_t s_button_cb;
static i2s_chan_handle_t s_tx, s_rx;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static design_effect_t s_effect;
static int64_t s_effect_at;
static int16_t s_microphone[AUDIO_FRAMES];
static size_t s_microphone_count;
static unsigned s_microphone_level;

static void motor_safety(void *unused)
{
    (void)unused;
    portENTER_CRITICAL(&s_lock);
    int64_t updated = s_effect_at;
    portEXIT_CRITICAL(&s_lock);
    if (s_motor_ready && esp_timer_get_time() / 1000 - updated > EFFECT_WATCHDOG_MS)
        ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0, 0);
}

static void audio_task(void *unused)
{
    (void)unused;
    int32_t output[AUDIO_FRAMES * 2], input[AUDIO_FRAMES * 2];
    float phase = 0, gain = 0;
    unsigned failures = 0;
    for (;;) {
        bool failed = false;
        portENTER_CRITICAL(&s_lock);
        design_effect_t effect = s_effect;
        int64_t updated = s_effect_at;
        portEXIT_CRITICAL(&s_lock);
        if (esp_timer_get_time() / 1000 - updated > EFFECT_WATCHDOG_MS) effect.volume = 0;
        if (s_tx) {
            float target = effect.frequency ? effect.volume / 100.0f : 0;
            for (size_t i = 0; i < AUDIO_FRAMES; ++i) {
                gain += (target - gain) * 0.01f; /* Avoid abrupt amplitude clicks. */
                phase += effect.frequency / (float)AUDIO_RATE;
                phase -= floorf(phase);
                float value = effect.wave == DESIGN_SQUARE ? (phase < .5f ? 1 : -1) :
                              effect.wave == DESIGN_TRIANGLE ? 1 - 4 * fabsf(phase - .5f) :
                              sinf(phase * 6.283185307f);
                int32_t sample = (int32_t)(value * gain * 16000);
                output[i * 2] = output[i * 2 + 1] = sample * 65536;
            }
            size_t written = 0;
            esp_err_t err = i2s_channel_write(s_tx, output, sizeof(output), &written, 50);
            if (err != ESP_OK || written != sizeof(output)) failed = true;
        }
        if (s_rx) {
            size_t bytes = 0;
            esp_err_t err = i2s_channel_read(s_rx, input, sizeof(input), &bytes, 50);
            if (err == ESP_OK && bytes) {
                size_t frames = bytes / (2 * sizeof(int32_t));
                int16_t mono[AUDIO_FRAMES];
                double energy = 0;
                for (size_t i = 0; i < frames; ++i) {
                    mono[i] = (int16_t)(input[i * 2] / 65536); /* Left slot, MSB aligned. */
                    energy += (double)mono[i] * mono[i];
                }
                unsigned level = (unsigned)(sqrt(energy / frames) * 100 / 32768);
                portENTER_CRITICAL(&s_lock);
                memcpy(s_microphone, mono, frames * sizeof(*mono));
                s_microphone_count = frames;
                s_microphone_level = level > 100 ? 100 : level;
                portEXIT_CRITICAL(&s_lock);
            } else if (err != ESP_ERR_TIMEOUT) failed = true;
        }
        failures = failed ? failures + 1 : 0;
        if (failures >= 10) {
            ESP_LOGW(TAG, "audio I/O failed repeatedly; disabling optional audio");
            if (s_tx) i2s_channel_disable(s_tx);
            if (s_rx) i2s_channel_disable(s_rx);
            portENTER_CRITICAL(&s_lock);
            s_microphone_count = s_microphone_level = 0;
            portEXIT_CRITICAL(&s_lock);
            vTaskDelete(NULL);
        }
        /* RX/TX DMA pace audio; yield even when a driver reports an immediate error. */
        vTaskDelay(1);
    }
}

static esp_err_t audio_init(void)
{
    if (!s_config.speaker && !s_config.microphone) return ESP_OK;
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.dma_desc_num = 4;
    channel.dma_frame_num = AUDIO_FRAMES;
    esp_err_t err = i2s_new_channel(&channel, s_config.speaker ? &s_tx : NULL,
                                  s_config.microphone ? &s_rx : NULL);
    if (err != ESP_OK) return err;
    i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                      I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = s_config.bclk,
            .ws = s_config.ws,
            .dout = s_config.speaker ? s_config.dout : I2S_GPIO_UNUSED,
            .din = s_config.microphone ? s_config.din : I2S_GPIO_UNUSED,
        },
    };
    if (s_tx) err = i2s_channel_init_std_mode(s_tx, &config);
    if (err == ESP_OK && s_rx) err = i2s_channel_init_std_mode(s_rx, &config);
    bool tx_enabled = false, rx_enabled = false;
    if (err == ESP_OK && s_tx) {
        err = i2s_channel_enable(s_tx);
        tx_enabled = err == ESP_OK;
    }
    if (err == ESP_OK && s_rx) {
        err = i2s_channel_enable(s_rx);
        rx_enabled = err == ESP_OK;
    }
    if (err == ESP_OK && xTaskCreate(audio_task, "beeper_audio", 8192, NULL, 3, NULL) != pdPASS)
        err = ESP_ERR_NO_MEM;
    if (err != ESP_OK) {
        if (tx_enabled) i2s_channel_disable(s_tx);
        if (rx_enabled) i2s_channel_disable(s_rx);
        if (s_tx) i2s_del_channel(s_tx);
        if (s_rx) i2s_del_channel(s_rx);
        s_tx = s_rx = NULL;
    }
    return err;
}

esp_err_t expansion_init(const design_hardware_t *config, expansion_button_cb_t button)
{
    if (!design_hardware_valid(config)) return ESP_ERR_INVALID_ARG;
    s_config = *config;
    s_button_cb = button;
    esp_err_t result = ESP_OK;
    for (size_t i = 0; i < DESIGN_MAX_BUTTONS; ++i) {
        if (config->buttons[i].gpio < 0) continue;
        gpio_config_t gpio = {
            .pin_bit_mask = UINT64_C(1) << config->buttons[i].gpio,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = config->buttons[i].active_low,
            .pull_down_en = !config->buttons[i].active_low,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t err = gpio_config(&gpio);
        if (err != ESP_OK) { result = err; continue; }
        cb_btn_fsm_init(&s_buttons[i]);
        s_button_ready[i] = true;
    }
    if (config->motor_gpio >= 0) {
        /* Timer/channel 0 belong to the BSP backlight. Expansion owns 1 only. */
        ledc_timer_config_t timer = {
            .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
            .timer_num = LEDC_TIMER_1, .freq_hz = 20000, .clk_cfg = LEDC_AUTO_CLK,
        };
        ledc_channel_config_t motor = {
            .gpio_num = config->motor_gpio, .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_1, .timer_sel = LEDC_TIMER_1, .duty = 0,
        };
        gpio_reset_pin(config->motor_gpio);
        gpio_set_direction(config->motor_gpio, GPIO_MODE_OUTPUT);
        gpio_set_level(config->motor_gpio, 0);
        esp_err_t err = ledc_timer_config(&timer);
        if (err == ESP_OK) err = ledc_channel_config(&motor);
        if (err == ESP_OK) {
            const esp_timer_create_args_t safety = {.callback = motor_safety, .name = "motor_safety"};
            err = esp_timer_create(&safety, &s_motor_safety);
            if (err == ESP_OK) err = esp_timer_start_periodic(s_motor_safety, 100000);
            if (err != ESP_OK) ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
        }
        s_motor_ready = err == ESP_OK;
        if (err != ESP_OK) result = err;
    }
    esp_err_t audio = audio_init();
    if (audio != ESP_OK) result = audio;
    ESP_LOGI(TAG, "external speaker=%d microphone=%d motor=%d (configured channels only)",
             config->speaker, config->microphone, s_motor_ready);
    return result;
}

void expansion_tick(uint64_t now_ms)
{
    for (size_t i = 0; i < DESIGN_MAX_BUTTONS; ++i) {
        if (!s_button_ready[i]) continue;
        bool pressed = gpio_get_level(s_config.buttons[i].gpio) == !s_config.buttons[i].active_low;
        cb_btn_event_t event = cb_btn_fsm_update(&s_buttons[i], pressed, (uint32_t)now_ms);
        if (event == CB_BTN_EV_PRESS && s_button_cb) s_button_cb(s_config.buttons[i].action);
    }
    portENTER_CRITICAL(&s_lock);
    int64_t updated = s_effect_at;
    unsigned vibration = s_effect.vibration;
    portEXIT_CRITICAL(&s_lock);
    if ((int64_t)now_ms - updated > EFFECT_WATCHDOG_MS) vibration = 0;
    int duty = (int)(vibration * 1023 / 100);
    if (s_motor_ready) {
        esp_err_t err = ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty, 0);
        if (err != ESP_OK) {
            ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
            s_motor_ready = false;
            ESP_LOGW(TAG, "motor PWM stopped after driver error");
        }
    }
}

void expansion_effect(const design_effect_t *effect)
{
    portENTER_CRITICAL(&s_lock);
    s_effect = *effect;
    if (s_effect.volume > 100) s_effect.volume = 100;
    if (s_effect.vibration > 100) s_effect.vibration = 100;
    if (s_effect.frequency > 6000) s_effect.frequency = 6000;
    s_effect_at = esp_timer_get_time() / 1000;
    portEXIT_CRITICAL(&s_lock);
}

unsigned expansion_microphone_level(void)
{
    portENTER_CRITICAL(&s_lock);
    unsigned value = s_microphone_level;
    portEXIT_CRITICAL(&s_lock);
    return value;
}

size_t expansion_microphone_read(int16_t *samples, size_t capacity)
{
    if (!samples || !capacity) return 0;
    portENTER_CRITICAL(&s_lock);
    size_t count = capacity < s_microphone_count ? capacity : s_microphone_count;
    memcpy(samples, s_microphone, count * sizeof(*samples));
    portEXIT_CRITICAL(&s_lock);
    return count;
}
