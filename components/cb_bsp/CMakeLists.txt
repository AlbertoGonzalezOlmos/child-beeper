# components/cb_bsp — Child Beeper board support package (item 1: skeleton).
# Owner of this file: mimo26-pro (CONTRACT file-ownership; SRCS governed by R12).
#
# SRCS is an explicit, GLOB-free list (CONTRACT R12). The final item-1 list is
# exactly these three entries ("src/cb_math.c" and "src/cb_button_fsm.c" are
# owned by kimi-k31-pro and are on disk). Rule: never list a file that does not
# exist. Items 2..5 append their driver sources here via the review round
# (single writer for this file).
idf_component_register(
    SRCS
        "src/cb_bsp.c"
        "src/cb_math.c"
        "src/cb_button_fsm.c"
    INCLUDE_DIRS
        "include"
)
