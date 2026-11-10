CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c99 -Isrc

ifeq ($(OS),Windows_NT)
    EXE := .exe
    MATH_LIB :=
else
    EXE :=
    MATH_LIB := -lm
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        export PKG_CONFIG_PATH := /opt/homebrew/lib/pkgconfig:/usr/local/lib/pkgconfig:$(PKG_CONFIG_PATH)
    endif
endif

SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS   := $(shell pkg-config --libs sdl2)

CORE_SRCS = src/s20_assets.c src/s20_audio.c src/s20_games.c src/s20_games_data.c src/s20_lcd.c src/s20_menu.c

.PHONY: all assets test clean

all: nokia3310$(EXE)

assets:
	python3 tools/extract_s20_assets.py

nokia3310$(EXE): $(CORE_SRCS) src/platform_sdl2.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $^ -o $@ $(SDL_LIBS) $(MATH_LIB)

smoke_test$(EXE): $(CORE_SRCS) tests/smoke_test.c
	$(CC) $(CFLAGS) $^ -o $@ $(MATH_LIB)

test: smoke_test$(EXE)
	./smoke_test$(EXE)

clean:
	rm -f nokia3310 nokia3310.exe smoke_test smoke_test.exe
