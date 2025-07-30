# Simple Makefile for building the Snake libretro core.
CC ?= gcc
CFLAGS ?= -O2 -g -Wall -Wextra -std=c11 -fPIC
LDFLAGS ?= -shared
TARGET := snake_libretro.dll
SOURCES := snake_core.c


# Emscripten (WebAssembly) build
ifeq ($(platform), emscripten)
	CC := emcc
	AR := emar
	CFLAGS := -O2 -Wall -std=c11 -s WASM=1 -fPIC
	LDFLAGS :=
	TARGET := snake_libretro.bc
	STATIC_LINKING ?= 1
endif

OBJS := $(SOURCES:.c=.o)
all: $(TARGET)

$(OBJS): $(SOURCES)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
ifeq ($(STATIC_LINKING), 1)
	$(AR) rcs $@ $(OBJS)
else
	$(CC) -o $@ $(SHARED) $(OBJS) $(LDFLAGS) $(LIBS)
endif

clean:
	rm -f $(TARGET)