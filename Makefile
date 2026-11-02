CMAKE ?= cmake
BUILD_DIR ?= build
PREFIX ?= /usr/local

.PHONY: all test install clean
all:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$(PREFIX)
	$(CMAKE) --build $(BUILD_DIR)

test: all
	$(CMAKE) --build $(BUILD_DIR) --target test

install: all
	$(CMAKE) --install $(BUILD_DIR)

clean:
	$(CMAKE) -E rm -rf $(BUILD_DIR)
