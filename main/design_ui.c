#include "design_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "src/misc/cache/instance/lv_image_cache.h"

/* Generated as a build dependency. No user design is interpreted as executable code. */
#include "design_data.h"

static const char *TAG = "design_ui";
static lv_obj_t *s_root, *s_scenes[DESIGN_SCENES], *s_layers[DESIGN_MAX_LAYERS];
static lv_font_t s_fonts[10];
static design_scene_t s_scene = DESIGN_SCENES;
typedef struct {
    FILE *file;
    uint8_t *pixels;
    lv_image_dsc_t image;
    int frame;
    unsigned scenes;
    bool ready;
} asset_runtime_t;
static asset_runtime_t s_assets[DESIGN_MAX_ASSETS];

static lv_obj_t *container(lv_obj_t *parent)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_size(object, DESIGN_WIDTH, DESIGN_HEIGHT);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    return object;
}

static void load_asset(size_t index)
{
    const design_asset_t *asset = &design_assets[index];
    asset_runtime_t *runtime = &s_assets[index];
    runtime->frame = -1;
    if (!asset->frames || asset->frame_bytes != (uint32_t)asset->width * asset->height * 3 ||
        asset->width > DESIGN_WIDTH || asset->height > DESIGN_HEIGHT) return;
    struct stat info;
    if (stat(asset->path, &info) || info.st_size != (long)asset->frame_bytes * asset->frames) {
        ESP_LOGW(TAG, "asset missing or incomplete: %s", asset->path);
        return;
    }
    runtime->pixels = heap_caps_calloc(1, asset->frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!runtime->pixels) runtime->pixels = heap_caps_calloc(1, asset->frame_bytes, MALLOC_CAP_8BIT);
    runtime->file = runtime->pixels ? fopen(asset->path, "rb") : NULL;
    if (!runtime->file) {
        free(runtime->pixels);
        runtime->pixels = NULL;
        ESP_LOGW(TAG, "could not load asset: %s", asset->path);
        return;
    }
    runtime->image.header.magic = LV_IMAGE_HEADER_MAGIC;
    runtime->image.header.cf = LV_COLOR_FORMAT_RGB565A8;
    runtime->image.header.w = asset->width;
    runtime->image.header.h = asset->height;
    runtime->image.header.stride = asset->width * 2;
    runtime->image.data_size = asset->frame_bytes;
    runtime->image.data = runtime->pixels;
    runtime->ready = true;
}

bool design_ui_init(lv_obj_t *parent)
{
    if (design_layer_count > DESIGN_MAX_LAYERS || design_asset_count > DESIGN_MAX_ASSETS ||
        sizeof(design_fonts) / sizeof(*design_fonts) > 10) return false;
    for (size_t i = 0; i < sizeof(design_fonts) / sizeof(*design_fonts); ++i) {
        s_fonts[i] = *design_fonts[i];
        /* The editor uses exactly these bitmap advances, including custom fonts. */
        s_fonts[i].kerning = LV_FONT_KERNING_NONE;
    }
    s_root = container(parent);
    for (size_t i = 0; i < DESIGN_SCENES; ++i) {
        s_scenes[i] = container(s_root);
        lv_obj_add_flag(s_scenes[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (size_t i = 0; i < design_asset_count; ++i) load_asset(i);
    for (size_t i = 0; i < design_layer_count; ++i) {
        const design_layer_t *layer = &design_layers[i];
        lv_obj_t *parent_scene = s_scenes[layer->scene];
        lv_obj_t *object;
        if (layer->kind == DESIGN_TEXT) {
            object = lv_label_create(parent_scene);
            lv_obj_remove_style_all(object);
            lv_obj_set_style_text_font(object, &s_fonts[layer->font], 0);
            lv_obj_set_style_text_color(object, lv_color_hex(layer->color), 0);
            lv_obj_set_style_text_align(object, layer->align == 1 ? LV_TEXT_ALIGN_CENTER :
                                       layer->align == 2 ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, 0);
            lv_obj_set_style_text_letter_space(object, 0, 0);
            lv_obj_set_style_text_line_space(object, 0, 0);
            lv_label_set_long_mode(object, layer->scroll ? LV_LABEL_LONG_MODE_SCROLL : LV_LABEL_LONG_MODE_WRAP);
            lv_label_set_text(object, layer->text);
        } else if (layer->kind == DESIGN_IMAGE) {
            object = lv_image_create(parent_scene);
            if (layer->asset >= 0 && (size_t)layer->asset < design_asset_count) {
                asset_runtime_t *asset = &s_assets[layer->asset];
                asset->scenes |= 1U << layer->scene;
                if (asset->ready) lv_image_set_src(object, &asset->image);
            }
            lv_image_set_inner_align(object, LV_IMAGE_ALIGN_STRETCH);
        } else {
            object = lv_obj_create(parent_scene);
            lv_obj_remove_style_all(object);
            lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(object, lv_color_hex(layer->color), 0);
            lv_obj_set_style_radius(object, layer->kind == DESIGN_ELLIPSE ? LV_RADIUS_CIRCLE : 0, 0);
        }
        lv_obj_set_size(object, layer->width, layer->height);
        lv_obj_set_pos(object, layer->x, layer->y);
        lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
        s_layers[i] = object;
    }
    ESP_LOGI(TAG, "design \"%s\": %u layers, %u image assets", design_name,
             (unsigned)design_layer_count, (unsigned)design_asset_count);
    return true;
}

void design_ui_visible(bool visible)
{
    if (!s_root) return;
    if (visible) lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}

void design_ui_update(design_scene_t scene, uint64_t elapsed_ms,
                      const char *const bindings[9], const design_effect_t *effect)
{
    if (!s_root || scene >= DESIGN_SCENES) return;
    if (s_scene != scene) {
        for (size_t i = 0; i < DESIGN_SCENES; ++i) lv_obj_add_flag(s_scenes[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_scenes[scene], LV_OBJ_FLAG_HIDDEN);
        s_scene = scene;
    }
    lv_obj_set_style_bg_color(s_scenes[scene],
                             lv_color_hex(effect->screen ? effect->screen_color : design_backgrounds[scene]), 0);
    bool changed[DESIGN_MAX_ASSETS] = {false};
    for (size_t i = 0; i < design_asset_count; ++i) {
        asset_runtime_t *runtime = &s_assets[i];
        const design_asset_t *asset = &design_assets[i];
        if (!runtime->ready || !(runtime->scenes & (1U << scene))) continue;
        int frame = (int)design_frame(asset->durations, asset->frames, elapsed_ms);
        if (frame == runtime->frame) continue;
        if (fseek(runtime->file, (long)frame * asset->frame_bytes, SEEK_SET) ||
            fread(runtime->pixels, 1, asset->frame_bytes, runtime->file) != asset->frame_bytes) {
            memset(runtime->pixels, 0, asset->frame_bytes);
            runtime->ready = false;
            fclose(runtime->file);
            runtime->file = NULL;
            ESP_LOGW(TAG, "image read failed; suppressing asset");
        }
        runtime->frame = frame;
        lv_image_cache_drop(&runtime->image);
        changed[i] = true;
    }
    for (size_t i = 0; i < design_layer_count; ++i) {
        const design_layer_t *layer = &design_layers[i];
        if (layer->scene != scene) continue;
        lv_obj_t *object = s_layers[i];
        design_pose_t pose;
        design_pose(layer, elapsed_ms, &pose);
        lv_obj_set_pos(object, pose.x, pose.y);
        lv_obj_set_style_opa(object, pose.opacity, 0);
        if (layer->kind == DESIGN_TEXT && layer->binding != DESIGN_STATIC) {
            const char *value = bindings[layer->binding];
            if (value && strcmp(lv_label_get_text(object), value)) lv_label_set_text(object, value);
        } else if (layer->kind == DESIGN_IMAGE && layer->asset >= 0 && changed[layer->asset]) {
            lv_obj_invalidate(object);
        }
    }
}

void design_ui_effect(design_scene_t scene, uint64_t elapsed_ms, design_effect_t *effect)
{
    design_effect(scene < DESIGN_SCENES ? &design_sequences[scene] : NULL, elapsed_ms, effect);
}

const design_hardware_t *design_ui_hardware(void)
{
    return &design_hardware;
}
