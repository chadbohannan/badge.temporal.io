# Host build for the Packit game core, including gameDraw() -- the same
# u8g2 drawing the badge will call, built for this machine with u8g2
# writing into memory instead of an SSD1306. Mirrors firmware/host/helgrind/'s
# pattern.
#
#   make            # builds ./packit-test
#   make test       # runs the scripted host tests (test.cpp)
#   make play       # interactive SDL2 viewer showing the real 128x64 1-bit frame
#
# Needs: a C++17 compiler, GNU make, and the U8g2 library PlatformIO fetched
# for the firmware (`pio run` once, or set U8G2_DIR to any u8g2 checkout) --
# gameDraw() now lives in PackitGame.cpp, so both targets need u8g2's clib
# to link, even though `make test` never calls gameDraw itself.
# SDL2 is optional -- found via pkg-config; without it `make play` builds a
# stub that prints an install hint, and `make test` is unaffected either way.
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
CXXFLAGS += -DPACKIT_SDL $(shell pkg-config --cflags sdl2)
LDLIBS   += $(shell pkg-config --libs sdl2)
endif

GAME_SRC := PackitGame.cpp
U8G2_SRC := $(wildcard $(U8G2_DIR)/clib/*.c)
U8G2_OBJS := $(patsubst $(U8G2_DIR)/clib/%.c,$(BUILD)/u8g2/%.o,$(U8G2_SRC))
HEADERS  := display.h window.h $(FW)/src/screens/PackitGame.h

.PHONY: test play clean

test: packit-test
	./packit-test

packit-test: $(BUILD)/test.o $(BUILD)/PackitGame.o $(U8G2_OBJS)
	$(CXX) -o $@ $^ $(LDLIBS)

play: packit-play
	./packit-play

packit-play: $(BUILD)/play.o $(BUILD)/window.o $(BUILD)/display.o $(BUILD)/png.o $(BUILD)/PackitGame.o $(U8G2_OBJS)
	$(CXX) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: %.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/%.o: $(FW)/src/screens/%.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/u8g2/%.o: $(U8G2_DIR)/clib/%.c | $(BUILD)/u8g2
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD) $(BUILD)/u8g2:
	mkdir -p $@

clean:
	rm -rf $(BUILD) $(OUT) packit-test packit-play
