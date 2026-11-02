CMAKE ?= cmake
BUILD_DIR ?= build
PREFIX ?= /usr/local
BUILD_SHARED_LIBS ?= ON

.PHONY: all test install clean
all:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$(PREFIX) -DBUILD_SHARED_LIBS=$(BUILD_SHARED_LIBS)
	$(CMAKE) --build $(BUILD_DIR)

test: all
	$(CMAKE) --build $(BUILD_DIR) --target test

install: all
	$(CMAKE) --install $(BUILD_DIR)

clean:
	$(CMAKE) -E rm -rf $(BUILD_DIR)
