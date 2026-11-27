#ifndef BEEPER_MODEL_H
#define BEEPER_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BEEPER_TEXT_MAX 240
#define BEEPER_HISTORY 16
#define BEEPER_CLOCK_MIN 1704067200LL /* 2024-01-01 UTC */
#define BEEPER_CLOCK_MAX 4102444800LL /* 2100-01-01 UTC */

typedef struct {
    uint32_t id;
    bool from_device;
    int64_t epoch;
    char text[BEEPER_TEXT_MAX + 1];
} beeper_message_t;

typedef struct {
    bool enabled;
    uint8_t hour;
    uint8_t minute;
    int64_t last_fired_minute;
} beeper_alarm_t;

typedef struct {
    beeper_message_t messages[BEEPER_HISTORY]; /* oldest first */
    size_t count;
    uint32_t next_id;
    uint32_t notification_id;
    bool unread;
    beeper_alarm_t alarm;
    int utc_offset_minutes;
    uint64_t ringing_until_ms;
} beeper_model_t;

void beeper_model_init(beeper_model_t *model);
bool beeper_text_valid(const char *text);
bool beeper_model_message(beeper_model_t *model, const char *text,
                          bool from_device, int64_t epoch);
/* Returns true only on a new alarm firing; monotonic time bounds its duration. */
bool beeper_model_tick(beeper_model_t *model, int64_t epoch, uint64_t monotonic_ms);
void beeper_model_ack(beeper_model_t *model);
/* Double pulse: smoothly interpolates blue -> yellow -> blue twice per 1.4 s. */
uint8_t beeper_heartbeat_mix(uint64_t monotonic_ms);

#endif
