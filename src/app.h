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
    SemaphoreHandle_t lock = nullptr;        // guards everything in App (short holds only)
    SemaphoreHandle_t save_lock = nullptr;   // serializes settings/cal flash writes (see app_update_settings)
    int32_t last_raw = 0;
    uint32_t sample_count = 0;
    UnloadTracker unload;
    CellConfig cells = CellConfig::defaults();
};

extern App g_app;

// Creates the locks. Call first in app_main; false if out of memory.
bool app_init();

// RAII lock around g_app
class AppLock {
public:
    AppLock() { xSemaphoreTake(g_app.lock, portMAX_DELAY); }
    ~AppLock() { xSemaphoreGive(g_app.lock); }
    AppLock(const AppLock&) = delete;
    AppLock& operator=(const AppLock&) = delete;
};

// Held across "copy settings, write them to flash" so writes land in the order the copies were
// taken (an older copy can never overwrite a newer one). Take it before AppLock, never inside.
class SaveLock {
public:
    SaveLock() { xSemaphoreTake(g_app.save_lock, portMAX_DELAY); }
    ~SaveLock() { xSemaphoreGive(g_app.save_lock); }
    SaveLock(const SaveLock&) = delete;
    SaveLock& operator=(const SaveLock&) = delete;
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
// False if the flash write failed (the scale keeps using the new values until a restart).
bool app_persist_scale();

// The Unload button (web page and ISOBUS): starts a manual unload, or ends the one in progress
// and logs it. Returns false if there is no valid weight yet.
bool app_toggle_unload();

enum class SaveResult { Ok, Invalid, Failed };

// Edit the settings and save them. edit(Settings&) returns false to reject the change (nothing is
// saved). Calibration and tare are owned by the scale and written only by app_persist_scale(), so
// they are restored from the live settings here: a settings change can never undo a calibration
// point added at the same time from another screen.
template <typename Edit>
SaveResult app_update_settings(Edit edit) {
    SaveLock save;
    Settings s;
    {
        AppLock l;
        s = g_app.settings;
    }
    if (!edit(s)) return SaveResult::Invalid;
    {
        AppLock l;
        s.n_cal = g_app.settings.n_cal;
        for (size_t i = 0; i < MAX_CAL_POINTS; ++i) s.cal[i] = g_app.settings.cal[i];
        s.tare_lb = g_app.settings.tare_lb;
        g_app.settings = s;
    }
    return settings_save(s) ? SaveResult::Ok : SaveResult::Failed;   // flash write outside AppLock
}

// Save the load cell layout and, if that worked, apply it. False if the flash write failed.
bool app_save_cells(const CellConfig& c);

// Save the unload thresholds and, if that worked, apply them. False if the flash write failed.
bool app_save_unload_cfg(const UnloadConfig& c);
