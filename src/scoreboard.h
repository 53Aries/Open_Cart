// Open_Cart
#pragma once
// Serial output to a remote scoreboard display.
//
// scoreboard_format() builds the Digi-Star EZ2000 frame <stx>ABBBCD<cr> (7E1, 1200 baud default).
// The layout matches a capture from a real EZ2000 (see scoreboard.cpp and DESIGN.md "Scoreboard").

#include <cstddef>
#include "app.h"

// Format one frame into buf. Return the number of bytes to send (0 = send nothing).
size_t scoreboard_format(char* buf, size_t cap, const Snapshot& snap);

void scoreboard_start();
