# Root-app component. SRCS is explicit, no globs (CONTRACT R12 style).
# Wi-Fi station support (wifi_sta.c, item 8) reads wifi_settings.txt from the
# FAT `storage` partition; the image is generated from the repo-root
# wifi_settings.txt (see the project CMakeLists) and flashed with the app.
idf_component_register(
    SRCS "app_main.c" "wifi_sta.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES cb_bsp esp_wifi esp_event esp_netif nvs_flash fatfs vfs wear_levelling)
