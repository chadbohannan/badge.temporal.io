#pragma once
// Interactive play in an SDL2 window: both displays plus a help/status
// panel, keyboard as the badge's pad. Only built when the Makefile finds
// SDL2 (HELGRIND_SDL); otherwise play() explains and returns 2.
#include <string>

namespace window {

int play(const std::string& snapDir);

}  // namespace window
