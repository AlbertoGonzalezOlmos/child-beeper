# Root-app component. SRCS is explicit, no globs (CONTRACT R12 style).
# Wi-Fi station support (wifi_sta.c, item 8) reads wifi_settings.txt from the
# FAT `storage` partition; the image is generated from the repo-root
# wifi_settings.txt (see the project CMakeLists) and flashed with the app.
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/design")
idf_component_register(
    SRCS "app_main.c" "wifi_sta.c" "beeper.c" "beeper_model.c"
         "design_model.c" "design_ui.c" "expansion.c"
    INCLUDE_DIRS "." "${CMAKE_BINARY_DIR}/design"
    PRIV_REQUIRES cb_bsp esp_wifi esp_event esp_netif nvs_flash fatfs vfs wear_levelling
                  esp_http_server esp_timer lwip esp_driver_gpio esp_driver_ledc esp_driver_i2s)
