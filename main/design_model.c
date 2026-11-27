#include "design_model.h"

#include <string.h>

bool design_gpio_available(int gpio)
{
    /* Spare header pins only. GPIO3 is a strap; UART, USB and all BSP pins are excluded. */
    return gpio == 2 || (gpio >= 4 && gpio <= 11);
}

static bool claim(int gpio, uint32_t *used)
{
    if (!design_gpio_available(gpio)) return false;
    uint32_t bit = UINT32_C(1) << gpio;
    if (*used & bit) return false;
    *used |= bit;
    return true;
}

bool design_hardware_valid(const design_hardware_t *h)
{
    if (!h) return false;
    uint32_t used = 0;
    for (size_t i = 0; i < DESIGN_MAX_BUTTONS; ++i) {
        if (h->buttons[i].gpio < -1 || h->buttons[i].action > DESIGN_EDIT_ALARM) return false;
        if (h->buttons[i].gpio >= 0 && !claim(h->buttons[i].gpio, &used)) return false;
    }
    if (h->motor_gpio < -1) return false;
    if (h->motor_gpio >= 0 && !claim(h->motor_gpio, &used)) return false;
    if (h->speaker || h->microphone) {
        if (!claim(h->bclk, &used) || !claim(h->ws, &used)) return false;
        if (h->speaker && !claim(h->dout, &used)) return false;
        if (h->microphone && !claim(h->din, &used)) return false;
    }
    return true;
}

static int interpolate(int a, int b, uint32_t t)
{
    return a + (b - a) * (int)t / 1024;
}

static uint32_t color_mix(uint32_t a, uint32_t b, uint32_t t)
{
    uint32_t color = 0;
    for (unsigned shift = 0; shift <= 16; shift += 8)
        color |= (uint32_t)interpolate((a >> shift) & 255, (b >> shift) & 255, t) << shift;
    return color;
}

void design_pose(const design_layer_t *layer, uint64_t elapsed_ms, design_pose_t *pose)
{
    pose->x = layer->x;
    pose->y = layer->y;
    pose->opacity = layer->opacity;
    if (!layer->keys || !layer->key_count || !layer->period_ms) return;
    uint32_t at = (uint32_t)(elapsed_ms % layer->period_ms);
    const design_key_t *a = &layer->keys[0], *b = a;
    for (size_t i = 1; i < layer->key_count; ++i) {
        b = &layer->keys[i];
        if (at < b->at_ms) break;
        a = b;
    }
    uint32_t t = b->at_ms > a->at_ms && at >= a->at_ms ?
                 (at - a->at_ms) * 1024 / (b->at_ms - a->at_ms) : 0;
    if (t > 1024) t = 1024;
    pose->x = (int16_t)interpolate(a->x, b->x, t);
    pose->y = (int16_t)interpolate(a->y, b->y, t);
    pose->opacity = (uint8_t)interpolate(a->opacity, b->opacity, t);
}

void design_effect(const design_sequence_t *sequence, uint64_t elapsed_ms, design_effect_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!sequence || !sequence->steps || !sequence->count || sequence->count > DESIGN_MAX_STEPS)
        return;
    uint64_t total = 0;
    for (size_t i = 0; i < sequence->count; ++i) total += sequence->steps[i].duration_ms;
    if (!total || (!sequence->loop && elapsed_ms >= total)) return;
    uint64_t at = elapsed_ms % total;
    size_t index = 0;
    while (index + 1 < sequence->count && at >= sequence->steps[index].duration_ms)
        at -= sequence->steps[index++].duration_ms;
    const design_step_t *a = &sequence->steps[index];
    const design_step_t *b = index + 1 < sequence->count ? a + 1 :
                             sequence->loop ? sequence->steps : a;
    uint32_t t = a->blend && a->duration_ms ? (uint32_t)(at * 1024 / a->duration_ms) : 0;
    t = t * t * (3072 - 2 * t) / 1048576; /* Smoothstep, Q10. */
    out->color = color_mix(a->color, b->color, t);
    out->screen_color = color_mix(a->screen_color, b->screen_color, t);
    out->brightness = (uint8_t)interpolate(a->brightness, b->brightness, t);
    out->vibration = (uint8_t)interpolate(a->vibration, b->vibration, t);
    out->volume = (uint8_t)interpolate(a->volume, b->volume, t);
    out->frequency = (uint16_t)interpolate(a->frequency, b->frequency, t);
    out->wave = a->wave;
    out->screen = a->screen;
}

size_t design_frame(const uint32_t *durations, size_t count, uint64_t elapsed_ms)
{
    if (!durations || !count) return 0;
    uint64_t total = 0;
    for (size_t i = 0; i < count; ++i) total += durations[i];
    if (!total) return 0;
    uint64_t at = elapsed_ms % total;
    for (size_t i = 0; i < count; ++i) {
        if (at < durations[i]) return i;
        at -= durations[i];
    }
    return 0;
}
