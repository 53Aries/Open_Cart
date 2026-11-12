// Open_Cart
#pragma once
// Persistent settings stored in NVS as one versioned blob.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include "config.h"
#include "scale.h"

// NVS namespace (default "nvs" partition) shared by settings, load cells, field names and unload thresholds.
// Never rename it or any key stored in it: saved calibration and settings would be lost.
static constexpr const char* kNvsNamespace = "scale";

struct Settings {
    uint32_t version;

    // Which outputs run. Each can be toggled from the web page (takes effect after reboot).
    uint8_t enable_wifi_web;
    uint8_t enable_scoreboard;
    uint8_t enable_isobus;
    uint8_t reserved0;

    // Wi-Fi access point
    char ap_ssid[33];
    char ap_password[65];     // 8..63 chars for WPA2

    // Scale
    double display_increment_lb;
    uint16_t filter_samples;
    uint16_t reserved1;
    double motion_threshold_lb;
    double tare_lb;
    uint8_t n_cal;
    uint8_t reserved2[7];
    CalPoint cal[MAX_CAL_POINTS];

    // Scoreboard serial (placeholder until the real protocol is known)
    uint32_t sb_baud;
    uint16_t sb_interval_ms;
    uint8_t sb_use_rs485;     // drives DE pin if 1
    uint8_t reserved3;
};

static constexpr uint32_t SETTINGS_VERSION = 1;

// Settings is saved to flash as raw bytes, and a blob of a different size is discarded on load
// (calibration included). These checks stop a build that would change the stored layout. To add a
// field, use a reservedN byte, or bump SETTINGS_VERSION and write a migration from the old layout.
static_assert(sizeof(Settings) == 288, "Settings layout changed: saved calibration would be lost");
static_assert(offsetof(Settings, cal) == 152, "Settings layout changed: saved calibration would be lost");

// Allowed ranges for values set from the web page or the VT.
static constexpr double kMaxDisplayIncrementLb = 1000.0;
inline bool display_increment_ok(double v) { return std::isfinite(v) && v > 0 && v <= kMaxDisplayIncrementLb; }
inline bool filter_samples_ok(long v) { return v >= 1 && v <= static_cast<long>(Scale::kMaxWindow); }

// Load cell layout lives in its own NVS blob so changing Settings never wipes calibration.
CellConfig cells_load();
bool cells_save(const CellConfig& c);

void settings_defaults(Settings* s);
bool settings_load(Settings* s);     // false if missing/invalid (s filled with defaults)
bool settings_save(const Settings& s);
