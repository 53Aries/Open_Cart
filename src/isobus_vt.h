// Open_Cart
#pragma once
// ISOBUS Virtual Terminal client (AgIsoStack++). Disabled in the default build.
//
// Build with the s3_isobus PlatformIO environment and place a VT object pool at
// isobus/scale.iop (see isobus/README.md). Without a pool this module logs a message and exits.

void isobus_start();
