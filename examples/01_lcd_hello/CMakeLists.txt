# 01_lcd_hello — display-stack example. This file is the CANONICAL EXAMPLE PATTERN
# (copy it for every new example; see also examples/01_lcd_hello/README.md):
#   1. two SDKCONFIG_DEFAULTS entries BEFORE include(project.cmake): the shared
#      board defaults, then this project's own main/sdkconfig.defaults (which
#      sets the project-relative partition CSV path);
#   2. main/idf_component.yml pulls cb_bsp via a MANIFEST-RELATIVE path —
#      three ups from examples/<NN>/main/: ../../../components/cb_bsp;
#   3. plain `idf.py -B <dir> build` inside this dir works with no -D flags
#      (the target comes from the shared board defaults).
cmake_minimum_required(VERSION 3.16)
set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/../../config/sdkconfig.board.defaults"
    "${CMAKE_CURRENT_LIST_DIR}/main/sdkconfig.defaults")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(01_lcd_hello)
