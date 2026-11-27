#ifndef BEEPER_EXPANSION_H
#define BEEPER_EXPANSION_H

#include "design_model.h"
#include "esp_err.h"

typedef void (*expansion_button_cb_t)(design_action_t action);

/* All channels are opt-in. A failed optional peripheral never aborts the app. */
esp_err_t expansion_init(const design_hardware_t *config, expansion_button_cb_t button);
void expansion_tick(uint64_t now_ms);
void expansion_effect(const design_effect_t *effect);
unsigned expansion_microphone_level(void);
/* Latest mono 16-bit microphone block, for future recording/voice integrations. */
size_t expansion_microphone_read(int16_t *samples, size_t capacity);

#endif
