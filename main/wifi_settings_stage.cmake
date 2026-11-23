# wifi_settings_stage.cmake — build-time step (run via `cmake -P` on every
# build by main/CMakeLists.txt): copy the repo-root wifi_settings.txt into the
# staging dir that fatfs_create_spiflash_image embeds, or write an empty
# placeholder when the file does not exist yet (fresh clone). The app then
# keeps Wi-Fi off until a real SSID is flashed.
if(EXISTS "${SRC}")
    configure_file("${SRC}" "${DST}" COPYONLY)
else()
    file(WRITE "${DST}" "ssid=\npassword=\n")
endif()
