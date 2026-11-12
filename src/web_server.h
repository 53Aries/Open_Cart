// Open_Cart
#pragma once
// Wi-Fi access point + small HTTP server with a single-page UI (weight, tare,
// calibration, settings). Works from any phone or tablet browser; no internet needed.

// False if Wi-Fi or the HTTP server could not be started (the error is logged).
bool web_server_start();
