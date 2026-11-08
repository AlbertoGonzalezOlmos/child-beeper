REPO_ROOT := $(abspath $(CURDIR)/../..)
CB_BUILD_ROOT ?= build

ifeq ($(filter /%,$(CB_BUILD_ROOT)),)
BUILD_ROOT_ABS := $(REPO_ROOT)/$(CB_BUILD_ROOT)
else
BUILD_ROOT_ABS := $(CB_BUILD_ROOT)
endif

HOST_BUILD_DIR := $(BUILD_ROOT_ABS)/host
RUN_TESTS := $(HOST_BUILD_DIR)/run_tests

CC ?= cc
CPPFLAGS ?=
CPPFLAGS += -I$(REPO_ROOT)/components/cb_bsp/include
CFLAGS ?= -std=c99 -O0 -g -Wall -Wextra -Werror -pedantic
LDFLAGS ?=
LDLIBS ?=

TEST_SOURCES := \
	$(REPO_ROOT)/tests/host/test_main.c \
	$(REPO_ROOT)/components/cb_bsp/src/cb_math.c \
	$(REPO_ROOT)/components/cb_bsp/src/cb_button_fsm.c

TEST_HEADERS := \
	$(REPO_ROOT)/components/cb_bsp/include/cb_pins.h \
	$(REPO_ROOT)/components/cb_bsp/include/cb_math.h \
	$(REPO_ROOT)/components/cb_bsp/include/cb_button_fsm.h \
	$(REPO_ROOT)/components/cb_bsp/include/cb_qmi8658_regs.h

.PHONY: all run_tests test clean
.DELETE_ON_ERROR:

all: $(RUN_TESTS)

run_tests: $(RUN_TESTS)

$(RUN_TESTS): $(TEST_SOURCES) $(TEST_HEADERS) | $(HOST_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

$(HOST_BUILD_DIR):
	mkdir -p $@

test: $(RUN_TESTS)
	$(RUN_TESTS)

clean:
	rm -rf $(HOST_BUILD_DIR)
