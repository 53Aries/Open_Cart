// Open_Cart
#pragma once
// Serial output to a remote scoreboard display.
//
// scoreboard_format() builds the Digi-Star EZ2000 frame <stx>ABBBCD<cr> (7E1, 1200 baud default).
// The mapping of the six characters to the display digits is UNVERIFIED (see scoreboard.cpp).
// Confirm against a real indicator capture or by probing the display before relying on it.

#include <cstddef>
#include "app.h"

// Format one frame into buf. Return the number of bytes to send (0 = send nothing).
size_t scoreboard_format(char* buf, size_t cap, const Snapshot& snap);

void scoreboard_start();
