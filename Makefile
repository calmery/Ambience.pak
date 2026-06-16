# Ambience - desktop (macOS/Linux) build.
# For the NextUI/TrimUI build see build-pak.sh + the cross toolchain notes in README.

CC      ?= clang
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 SDL2_ttf)
SDL_LIBS   := $(shell pkg-config --libs sdl2 SDL2_ttf)
CFLAGS  := -O2 -Wall -Wextra $(SDL_CFLAGS)
LDLIBS  := $(SDL_LIBS) -lm

APP_OBJ := main.o system.o audio.o ui.o config.o actions.o keyboard.o
TP_OBJ  := stb_vorbis.o dr_mp3.o
HDRS    := $(wildcard src/*.h)

ambience: $(APP_OBJ) $(TP_OBJ)
	$(CC) $(APP_OBJ) $(TP_OBJ) -o ambience $(LDLIBS)

# app modules: strict warnings, rebuilt if any header changes
$(APP_OBJ): %.o: src/%.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

# third-party single-header decoders: build with warnings off
stb_vorbis.o: src/stb_vorbis.c
	$(CC) $(SDL_CFLAGS) -O2 -w -c $< -o $@

dr_mp3.o: src/dr_mp3.c src/dr_mp3.h
	$(CC) -O2 -w -c $< -o $@

run: ambience
	./ambience

selftest: ambience
	./ambience --selftest

clean:
	rm -f ambience $(APP_OBJ) $(TP_OBJ)

.PHONY: run selftest clean
