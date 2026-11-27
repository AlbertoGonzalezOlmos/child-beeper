# child-beeper — root application ("hello beeper").
#
# Build:   ./cb build app        (or raw: idf.py -B build/<name> build)
# Pattern: two SDKCONFIG_DEFAULTS entries BEFORE include(project.cmake) —
#          the shared board defaults, then this project's own defaults
#          (main/sdkconfig.app.defaults sets the project-relative partition CSV path).
cmake_minimum_required(VERSION 3.16)
set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/config/sdkconfig.board.defaults"
    "${CMAKE_CURRENT_LIST_DIR}/main/sdkconfig.app.defaults")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(child_beeper)

# ---- Wi-Fi settings image (item 8) ------------------------------------------
# The repo-root wifi_settings.txt is embedded into the FAT `storage` partition
# image at build time and flashed together with the app by `idf.py flash`
# (FLASH_IN_PROJECT). A staging step refreshes the file on EVERY build (writing
# an empty placeholder when it is missing), so edit + re-flash is the whole
# provisioning flow — no CMake re-run needed. This block lives here, not in
# main/CMakeLists.txt, because component CMakeLists also run in CMake script
# mode (component_get_requirements), where add_custom_target is not available.
set(WIFI_SETTINGS_FILE "${CMAKE_CURRENT_LIST_DIR}/wifi_settings.txt")
set(WIFI_SETTINGS_STAGE "${CMAKE_BINARY_DIR}/wifi_settings")
file(MAKE_DIRECTORY "${WIFI_SETTINGS_STAGE}")

# A designer project is data. The compiler emits bounded C descriptors and
# RGB565/A8 frame files; it never executes project content.
set(DESIGN_PYTHON "${PYTHON}")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/.cache/designer/venv/bin/python")
    set(DESIGN_PYTHON "${CMAKE_CURRENT_LIST_DIR}/.cache/designer/venv/bin/python")
endif()
add_custom_target(design_compile ALL
    COMMAND "${DESIGN_PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/tools/designer/compiler.py"
        --project "${CMAKE_CURRENT_LIST_DIR}/design/project.json"
        --out "${CMAKE_BINARY_DIR}/design"
        --storage "${WIFI_SETTINGS_STAGE}/design"
    BYPRODUCTS "${CMAKE_BINARY_DIR}/design/design_data.h"
    VERBATIM)
add_dependencies(__idf_main design_compile)

add_custom_target(wifi_settings_stage ALL
    COMMAND ${CMAKE_COMMAND}
        "-DSRC=${WIFI_SETTINGS_FILE}"
        "-DDST=${WIFI_SETTINGS_STAGE}/wifi_settings.txt"
        -P "${CMAKE_CURRENT_LIST_DIR}/main/wifi_settings_stage.cmake"
    VERBATIM)

fatfs_create_spiflash_image(storage "${WIFI_SETTINGS_STAGE}" FLASH_IN_PROJECT)
# The image must be generated after the staging step refreshed the file.
add_dependencies(fatfs_storage_bin wifi_settings_stage design_compile)
