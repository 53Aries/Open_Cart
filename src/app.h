// Open_Cart
#pragma once
// Shared application state used by the sampling task and the output modules.

#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "scale.h"
#include "settings.h"
#include "unload.h"

struct App {
    Scale scale;
    Settings settings;
    SemaphoreHandle_t lock = nullptr;
    int32_t last_raw = 0;
    uint32_t sample_count = 0;
    UnloadTracker unload;
    CellConfig cells = CellConfig::defaults();
};

extern App g_app;

// RAII lock around g_app
class AppLock {
public:
    AppLock() { xSemaphoreTake(g_app.lock, portMAX_DELAY); }
    ~AppLock() { xSemaphoreGive(g_app.lock); }
    AppLock(const AppLock&) = delete;
    AppLock& operator=(const AppLock&) = delete;
};

// A consistent copy of the values outputs need.
struct Snapshot {
    double gross_lb;
    double net_lb;
    double display_lb;
    double tare_lb;
    double raw;
    int32_t last_raw;
    uint32_t sample_count;
    uint8_t n_cal;
    bool motion;
    bool approximate;
    bool has_cal;
    bool ready;
    bool unloading;
    bool unload_manual;
    bool unload_auto;
    double unload_running_lb;
    bool has_last_unload;
    double last_unload_lb;
};

Snapshot app_snapshot();

// Copy calibration and tare from the scale into settings and write them to NVS.
void app_persist_scale();

// The Unload button (web page and ISOBUS): starts a manual unload, or ends the one in progress
// and logs it. Returns false if there is no valid weight yet.
bool app_toggle_unload();

// Save a modified copy of settings (called by the web page).
void app_save_settings(const Settings& s);
