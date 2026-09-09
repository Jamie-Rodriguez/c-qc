# Unix build (Linux, macOS). Windows builds use CMakeLists.txt instead.
CFLAGS ?= -std=c99 -pedantic -Wall -Wextra -Werror -O2
BUILD_DIR = build

.PHONY: all check clean

all: $(BUILD_DIR)/smoke

$(BUILD_DIR)/smoke: tests/smoke.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

check: $(BUILD_DIR)/smoke
	$(BUILD_DIR)/smoke

clean:
	rm -rf $(BUILD_DIR)
