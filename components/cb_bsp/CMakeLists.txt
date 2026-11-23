# components/cb_bsp — Child Beeper board support package (item 1: skeleton).
# Owner of this file: deepseek-pro (CONTRACT lane cb_bsp, items 3+; item-1/2
# owner mimo26-pro — SRCS governed by R12).
#
# SRCS is an explicit, GLOB-free list (CONTRACT R12; item-2 ruling A3).
# Item 3 (deepseek-pro) appends the five RGB/button/battery sources: the two
# pure math modules (cb_rgb_math, cb_battery_math — host-tested) and the three
# drivers. Rule: never list a file that does not exist. Single writer for this
# file.
#
# REQUIRES/PRIV_REQUIRES per item-2 ruling A3: only the interface component
# (esp_lcd, exposed via cb_lcd.h) is public; the driver backends are private.
# lvgl / esp_lvgl_port / led_strip propagate through idf_component.yml (never
# as bare REQUIRES lvgl). Item 3 adds esp_driver_gpio (button), esp_timer
# (button poller) and esp_adc (battery) as private backends.
idf_component_register(
    SRCS
        "src/cb_bsp.c"
        "src/cb_math.c"
        "src/cb_button_fsm.c"
        "src/cb_lcd.c"
        "src/cb_backlight.c"
        "src/cb_pattern.c"
        "src/cb_rgb_math.c"
        "src/cb_battery_math.c"
        "src/cb_rgb.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_lcd
    PRIV_REQUIRES
        esp_driver_spi
        esp_driver_ledc
)
