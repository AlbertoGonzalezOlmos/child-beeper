# components/cb_bsp — Child Beeper board support package (item 1: skeleton).
# Owner of this file: mimo26-pro (CONTRACT file-ownership; SRCS governed by R12).
#
# SRCS is an explicit, GLOB-free list (CONTRACT R12; item-2 ruling A3). The
# item-2 list is exactly these six entries; the "src/cb_pattern.c" entry landed
# with this commit because kimi-k31-pro's file already exists on disk (4e8fa07)
# — rule: never list a file that does not exist. Single writer for this file.
#
# REQUIRES/PRIV_REQUIRES per item-2 ruling A3: only the interface component
# (esp_lcd, exposed via cb_lcd.h) is public; the driver backends are private.
# lvgl / esp_lvgl_port / led_strip propagate through idf_component.yml (never
# as bare REQUIRES lvgl).
idf_component_register(
    SRCS
        "src/cb_bsp.c"
        "src/cb_math.c"
        "src/cb_button_fsm.c"
        "src/cb_lcd.c"
        "src/cb_backlight.c"
        "src/cb_pattern.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_lcd
    PRIV_REQUIRES
        esp_driver_spi
        esp_driver_ledc
)
