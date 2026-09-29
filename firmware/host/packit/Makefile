# Fast, SDL-free unit tests for the Packit game core (PackitGame.cpp). To see or
# play the game, use the shared host harness (../README.md); it runs PackitScreen.
#
#   make            # builds ./packit-test
#   make test       # runs the scripted host tests (test.cpp)
#
# Needs: a C++17 compiler, GNU make, and the U8g2 library PlatformIO fetched
# for the firmware (`pio run -e host` once, or set U8G2_DIR to any u8g2 checkout).
# gameDraw() lives in PackitGame.cpp, so the test binary links u8g2's clib.
FW       := ../..
U8G2_DIR ?= $(firstword $(wildcard $(FW)/.pio/libdeps/host/U8g2/src $(FW)/.pio/libdeps/replay2026/U8g2/src))
BUILD    := build
OUT      := out

CXX      ?= g++
CC       ?= cc
CXXFLAGS := -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -I$(U8G2_DIR) -I$(FW)/src/screens
CFLAGS   := -O1 -w -I$(U8G2_DIR)/clib
LDLIBS   := -lm

GAME_SRC := PackitGame.cpp
U8G2_SRC := $(wildcard $(U8G2_DIR)/clib/*.c)
U8G2_OBJS := $(patsubst $(U8G2_DIR)/clib/%.c,$(BUILD)/u8g2/%.o,$(U8G2_SRC))
HEADERS  := $(FW)/src/screens/PackitGame.h

.PHONY: test clean

test: packit-test
	./packit-test

packit-test: $(BUILD)/test.o $(BUILD)/PackitGame.o $(U8G2_OBJS)
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
	rm -rf $(BUILD) $(OUT) packit-test
