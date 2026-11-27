#ifndef DESIGN_MODEL_H
#define DESIGN_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DESIGN_WIDTH 172
#define DESIGN_HEIGHT 320
#define DESIGN_MAX_BUTTONS 4
#define DESIGN_MAX_LAYERS 48
#define DESIGN_MAX_STEPS 32
#define DESIGN_MAX_ASSETS 8

typedef enum { DESIGN_HOME, DESIGN_MESSAGE, DESIGN_ALARM, DESIGN_SCENES } design_scene_t;
typedef enum { DESIGN_TEXT, DESIGN_BOX, DESIGN_ELLIPSE, DESIGN_IMAGE } design_kind_t;
typedef enum {
    DESIGN_STATIC, DESIGN_CLOCK, DESIGN_DATE, DESIGN_ALARM_TEXT, DESIGN_MESSAGE_TEXT,
    DESIGN_REPLY, DESIGN_HINT, DESIGN_NETWORK, DESIGN_MIC_LEVEL
} design_binding_t;
typedef enum { DESIGN_NEXT_REPLY, DESIGN_SEND_REPLY, DESIGN_ACK, DESIGN_EDIT_ALARM } design_action_t;
typedef enum { DESIGN_SINE, DESIGN_SQUARE, DESIGN_TRIANGLE } design_wave_t;

typedef struct { uint32_t at_ms; int16_t x, y; uint8_t opacity; } design_key_t;
typedef struct { int16_t x, y; uint8_t opacity; } design_pose_t;
typedef struct {
    design_kind_t kind;
    design_scene_t scene;
    design_binding_t binding;
    int16_t x, y;
    uint16_t width, height;
    uint32_t color;
    uint8_t opacity, font, align;
    bool scroll;
    int8_t asset;
    const char *text;
    uint32_t period_ms;
    const design_key_t *keys;
    size_t key_count;
} design_layer_t;

typedef struct {
    uint32_t duration_ms, color, screen_color;
    uint16_t frequency;
    uint8_t brightness, vibration, volume;
    design_wave_t wave;
    bool blend, screen;
} design_step_t;
typedef struct { const design_step_t *steps; size_t count; bool loop; } design_sequence_t;
typedef struct {
    uint32_t color, screen_color;
    uint16_t frequency;
    uint8_t brightness, vibration, volume;
    design_wave_t wave;
    bool screen;
} design_effect_t;

typedef struct { int8_t gpio; design_action_t action; bool active_low; } design_button_t;
typedef struct {
    design_button_t buttons[DESIGN_MAX_BUTTONS];
    int8_t motor_gpio;
    bool speaker, microphone;
    int8_t bclk, ws, dout, din;
} design_hardware_t;

bool design_gpio_available(int gpio);
bool design_hardware_valid(const design_hardware_t *hardware);
void design_pose(const design_layer_t *layer, uint64_t elapsed_ms, design_pose_t *pose);
void design_effect(const design_sequence_t *sequence, uint64_t elapsed_ms, design_effect_t *effect);
size_t design_frame(const uint32_t *durations, size_t count, uint64_t elapsed_ms);

#endif
