CROSS ?= arm-unknown-linux-gnueabihf-
CC := $(CROSS)gcc
OBJDUMP := $(CROSS)objdump
OBJCOPY := $(CROSS)objcopy

TARGET := build/nfsmw_mapper
MAPPER_SOURCES := src/main.c src/elf32_loader.c src/symbol_probe.c \
                  src/platform_probe.c src/relocation_probe.c \
                  src/softfp_bridge.c src/softfp_symbols.c \
                  src/compat_bridge.c src/initializer_trace.c \
                  src/jni_bridge.c src/obb_index.c src/opensl_bridge.c \
                  src/crash_trace.c
MAPPER_ASM_SOURCES := src/bionic_setjmp.S
MAPPER_OBJECTS := $(MAPPER_SOURCES:src/%.c=build/%.o) \
                  $(MAPPER_ASM_SOURCES:src/%.S=build/%.o)
BRIDGE_SOURCES :=

CPPFLAGS := -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -Isrc
CFLAGS := -std=c17 -O2 -g -fPIE -march=armv7-a -mfpu=neon \
          -mfloat-abi=hard -Wall -Wextra -Wpedantic -Wconversion \
          -Wshadow -Wformat=2 -Wno-format-nonliteral -Werror
LDFLAGS := -pie -Wl,-z,relro,-z,now
LDLIBS := -ldl -lm -pthread

.PHONY: all clean inspect syntax-android

all: $(TARGET)

$(TARGET): $(MAPPER_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
	$(OBJCOPY) --remove-section .note.ABI-tag $@

build/%.o: src/%.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

build/%.o: src/%.S | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

build:
	mkdir -p $@

inspect: $(TARGET)
	file $(TARGET)
	$(OBJDUMP) -p $(TARGET) | sed -n '/file format/,$$p'

# This validates the ARM32 source with an installed Android NDK when the Linux
# armhf cross toolchain is unavailable. It is not the R36S release build.
NDK_CC ?= $(HOME)/Library/Android/sdk/ndk/27.1.12297006/toolchains/llvm/prebuilt/darwin-x86_64/bin/armv7a-linux-androideabi21-clang
syntax-android:
	$(NDK_CC) $(CPPFLAGS) -std=c17 -O0 -Wall -Wextra -Wpedantic \
		-Wconversion -Wshadow -Wformat=2 -Wno-format-nonliteral -Werror \
		-fsyntax-only $(MAPPER_SOURCES) $(BRIDGE_SOURCES)

clean:
	rm -rf build
