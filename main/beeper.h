#ifndef BEEPER_H
#define BEEPER_H

#include "beeper_model.h"
#include "esp_err.h"

typedef struct {
    char latest[BEEPER_TEXT_MAX + 1];
    bool latest_from_device;
    bool unread;
    uint32_t notification_id;
    bool ringing;
    bool clock_valid;
    int64_t local_epoch;
    int utc_offset_minutes;
    beeper_alarm_t alarm;
    char pairing_code[7];
} beeper_view_t;

esp_err_t beeper_init(void);
void beeper_tick(uint64_t monotonic_ms);
void beeper_view(beeper_view_t *view);
bool beeper_send(const char *text);
void beeper_ack(void);
esp_err_t beeper_set_alarm(bool enabled, unsigned hour, unsigned minute);

#endif
