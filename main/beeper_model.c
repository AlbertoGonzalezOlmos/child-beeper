#include "beeper_model.h"

#include <string.h>

void beeper_model_init(beeper_model_t *model)
{
    memset(model, 0, sizeof(*model));
    model->next_id = 1;
    model->alarm.hour = 7;
    model->alarm.last_fired_minute = -1;
}

bool beeper_text_valid(const char *text)
{
    if (!text) return false;
    size_t len = strlen(text);
    if (!len || len > BEEPER_TEXT_MAX) return false;
    bool visible = false;
    for (size_t i = 0; i < len;) {
        uint32_t cp = (unsigned char)text[i++];
        if (cp < 0x80) {
            if ((cp < 32 && cp != '\n' && cp != '\t') || cp == 127) return false;
            visible |= cp > 32;
            continue;
        }
        unsigned extra;
        uint32_t minimum;
        if (cp >= 0xc2 && cp <= 0xdf) { extra = 1; minimum = 0x80; cp &= 0x1f; }
        else if (cp >= 0xe0 && cp <= 0xef) { extra = 2; minimum = 0x800; cp &= 0x0f; }
        else if (cp >= 0xf0 && cp <= 0xf4) { extra = 3; minimum = 0x10000; cp &= 7; }
        else return false;
        if (i + extra > len) return false;
        while (extra--) {
            unsigned char next = (unsigned char)text[i++];
            if ((next & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (next & 0x3f);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
        visible = true;
    }
    return visible;
}

bool beeper_model_message(beeper_model_t *model, const char *text,
                          bool from_device, int64_t epoch)
{
    if (!beeper_text_valid(text)) return false;
    if (model->count == BEEPER_HISTORY) {
        memmove(model->messages, model->messages + 1,
                (BEEPER_HISTORY - 1) * sizeof(model->messages[0]));
        --model->count;
    }
    beeper_message_t *message = &model->messages[model->count++];
    message->id = model->next_id++;
    message->from_device = from_device;
    message->epoch = epoch >= BEEPER_CLOCK_MIN ? epoch : 0;
    strcpy(message->text, text);
    if (!from_device) {
        model->unread = true;
        model->notification_id = message->id;
    }
    return true;
}

bool beeper_model_tick(beeper_model_t *model, int64_t epoch, uint64_t monotonic_ms)
{
    if (model->ringing_until_ms && monotonic_ms >= model->ringing_until_ms) {
        model->ringing_until_ms = 0;
    }
    if (!model->alarm.enabled || epoch < BEEPER_CLOCK_MIN || epoch >= BEEPER_CLOCK_MAX) {
        return false;
    }
    int64_t local_minute = epoch / 60 + model->utc_offset_minutes;
    int minute_of_day = (int)(local_minute % 1440);
    if (minute_of_day != model->alarm.hour * 60 + model->alarm.minute ||
        local_minute <= model->alarm.last_fired_minute) return false;
    model->alarm.last_fired_minute = local_minute;
    model->ringing_until_ms = monotonic_ms + 60000;
    return true;
}

void beeper_model_ack(beeper_model_t *model)
{
    model->unread = false;
    model->ringing_until_ms = 0;
}

uint8_t beeper_heartbeat_mix(uint64_t monotonic_ms)
{
    unsigned phase = (unsigned)(monotonic_ms % 1400);
    unsigned pulse;
    if (phase < 360) pulse = phase;
    else if (phase >= 460 && phase < 820) pulse = phase - 460;
    else return 0;
    unsigned triangle = pulse <= 180 ? pulse : 360 - pulse;
    /* Smoothstep has zero slope at blue and yellow, avoiding abrupt flashes. */
    unsigned x = triangle * 255 / 180;
    return (uint8_t)(x * x * (765 - 2 * x) / 65025);
}
