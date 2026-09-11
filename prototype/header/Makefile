# PROTOTYPE. From the repo root: make -C prototype/header run
CFLAGS ?= -std=c99 -pedantic -Wall -Wextra -Werror -O0 -g
BUILD_DIR = build

.PHONY: all run clean

all: $(BUILD_DIR)/example_main $(BUILD_DIR)/example_rle

$(BUILD_DIR)/example_main: example_main.c qc_stub.c qc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ example_main.c qc_stub.c

$(BUILD_DIR)/example_rle: example_rle.c qc_stub.c qc.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ example_rle.c qc_stub.c

run: all
	-$(BUILD_DIR)/example_main
	@echo
	-$(BUILD_DIR)/example_rle

clean:
	rm -rf $(BUILD_DIR)
