#pragma once
// Haptics.cpp includes this before its BADGE_HAS_HAPTICS guard. The host does
// not define that flag, so nothing in the file uses MCPWM. A host that turns
// haptics on has to add the MCPWM calls here.
