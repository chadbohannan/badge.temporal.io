# Host build of the Helgrind game core: the same HelgrindGame.cpp the badge
# runs, with u8g2 drawing into memory instead of an SSD1306.
#
#   make            # builds ./helgrind-host
#   make test       # runs every scripts/*.txt; a failed assert fails make
#   make check      # static checks on HelgrindWorld.cpp (check-world.py)
#   make play       # plays it in an SDL2 window
#   make run        # plays scripts/walk-to-axe.txt, frames land in out/
#
# Needs: a C++17 compiler, GNU make, and the U8g2 library PlatformIO fetched
# for the firmware (`pio run` once, or set U8G2_DIR to any u8g2 checkout).
# SDL2 is optional — found via pkg-config; without it `--play` is stubbed
# out and everything else still builds. Tested on Linux; the code is plain
# C++17 + C + SDL2 so macOS (Xcode CLT, brew sdl2 pkg-config) and MSYS2 or
# WSL on Windows should work too.
#
# All of u8g2's clib is compiled, display drivers included, because
# u8g2_d_setup.c references every driver and the two we use live in the
# same table. It's ~15 MB of font data and the slow part of the first
# build; objects are cached under build/ after that.

FW       := ../..
U8G2_DIR ?= $(FW)/.pio/libdeps/replay2026/U8g2/src
BUILD    := build
OUT      := out

CXX      ?= g++
CC       ?= cc
CXXFLAGS := -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -I$(U8G2_DIR) -I$(FW)/src/screens
CFLAGS   := -O1 -w -I$(U8G2_DIR)/clib
LDLIBS   := -lm

ifneq ($(shell pkg-config --exists sdl2 && echo yes),)
CXXFLAGS += -DHELGRIND_SDL $(shell pkg-config --cflags sdl2)
LDLIBS   += $(shell pkg-config --libs sdl2)
endif

HOST_SRC := main.cpp script.cpp status.cpp display.cpp png.cpp window.cpp
GAME_SRC := HelgrindGame.cpp HelgrindWorld.cpp
U8G2_SRC := $(wildcard $(U8G2_DIR)/clib/*.c)
OBJS     := $(HOST_SRC:%.cpp=$(BUILD)/%.o) $(GAME_SRC:%.cpp=$(BUILD)/%.o) \
            $(patsubst $(U8G2_DIR)/clib/%.c,$(BUILD)/u8g2/%.o,$(U8G2_SRC))
HEADERS  := $(wildcard *.h) $(FW)/src/screens/HelgrindGame.h $(FW)/src/screens/HelgrindWorld.h
SCRIPTS  := $(wildcard scripts/*.txt)

all: helgrind-host

helgrind-host: $(OBJS)
	$(CXX) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: %.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/%.o: $(FW)/src/screens/%.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/u8g2/%.o: $(U8G2_DIR)/clib/%.c | $(BUILD)/u8g2
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD) $(BUILD)/u8g2:
	mkdir -p $@

test: helgrind-host
	@for s in $(SCRIPTS); do echo "== $$s"; ./helgrind-host $$s $(OUT)/$$(basename $$s .txt) || exit 1; done

check: helgrind-host
	python3 check-world.py

play: helgrind-host
	./helgrind-host --play $(OUT)

run: helgrind-host
	./helgrind-host scripts/walk-to-axe.txt $(OUT)

clean:
	rm -rf $(BUILD) $(OUT) helgrind-host

.PHONY: all test check play run clean
