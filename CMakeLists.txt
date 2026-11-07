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
