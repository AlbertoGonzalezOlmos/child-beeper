#ifndef BEEPER_DESIGN_UI_H
#define BEEPER_DESIGN_UI_H

#include "design_model.h"
#include "lvgl.h"

typedef struct {
    const char *path;
    uint16_t width, height, frames;
    uint32_t frame_bytes;
    const uint32_t *durations;
} design_asset_t;

/* Caller holds the BSP LVGL lock for all UI operations. */
bool design_ui_init(lv_obj_t *parent);
void design_ui_visible(bool visible);
void design_ui_update(design_scene_t scene, uint64_t elapsed_ms,
                      const char *const bindings[9], const design_effect_t *effect);
void design_ui_effect(design_scene_t scene, uint64_t elapsed_ms, design_effect_t *effect);
const design_hardware_t *design_ui_hardware(void);

#endif
