#pragma once
// The game's observable state as named numeric fields, shared by the script
// runner (status/assert/until) and the SDL window's status line.
#include <string>

namespace status {

// "room=52", "x>=60", "hp<=3". Sets `known` false for an unknown field.
bool matches(const std::string& expr, bool& known);

// "room=52 x=93.6 y=19.7 hp=11 silver=0 arts=1 weapon=1 won=0 mode=1 enemies=2"
std::string line();

}  // namespace status
