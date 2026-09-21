// Entry point for the SDL viewer (window.h). Kept separate from test.cpp
// so `make test` never needs SDL2 -- only `make play` does.
#include "window.h"

int main() { return window::play(); }
