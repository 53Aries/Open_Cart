// Open_Cart
#include "app.h"

#include "unload_log.h"

App g_app;

bool app_init() {
    g_app.lock = xSemaphoreCreateMutex();
    g_app.save_lock = xSemaphoreCreateMutex();
    return g_app.lock && g_app.save_lock;
}

bool app_toggle_unload() {
    UnloadTracker::Event ev;
    bool valid;
    {
        AppLock l;
        valid = g_app.scale.ready() && g_app.scale.has_calibration();
        if (valid) ev = g_app.unload.toggle_manual();
    }
    if (ev.finished) unload_log_append(ev.start_lb, ev.end_lb, ev.total_lb);   // flash write outside the lock
    return valid;
}

Snapshot app_snapshot() {
    AppLock l;
    Snapshot s{};
    s.gross_lb = g_app.scale.gross_lb();
    s.tare_lb = g_app.scale.tare_lb();
    s.net_lb = g_app.scale.net_lb();
    s.display_lb = g_app.scale.display_lb();
    s.raw = g_app.scale.filtered_raw();
    s.last_raw = g_app.last_raw;
    s.sample_count = g_app.sample_count;
    s.n_cal = static_cast<uint8_t>(g_app.scale.cal_count());
    s.motion = g_app.scale.in_motion();
    s.approximate = g_app.scale.is_approximate();
    s.has_cal = g_app.scale.has_calibration();
    s.ready = g_app.scale.ready();
    s.unloading = g_app.unload.showing();
    s.unload_manual = g_app.unload.manual();
    s.unload_auto = g_app.unload.auto_enabled();
    s.unload_running_lb = g_app.unload.running_lb();
    s.has_last_unload = g_app.unload.has_last();
    s.last_unload_lb = g_app.unload.last_lb();
    return s;
}

bool app_persist_scale() {
    SaveLock save;
    Settings copy;
    {
        AppLock l;
        CalPoint pts[MAX_CAL_POINTS];
        const size_t n = g_app.scale.get_calibration(pts, MAX_CAL_POINTS);
        g_app.settings.n_cal = static_cast<uint8_t>(n);
        for (size_t i = 0; i < n; ++i) g_app.settings.cal[i] = pts[i];
        g_app.settings.tare_lb = g_app.scale.tare_lb();
        copy = g_app.settings;
    }
    return settings_save(copy);   // flash write happens outside AppLock
}

bool app_save_cells(const CellConfig& c) {
    SaveLock save;
    if (!cells_save(c)) return false;
    AppLock l;
    g_app.cells = c;
    g_app.scale.set_nominal_counts_per_lb(Scale::nominal_slope(c));
    return true;
}

bool app_save_unload_cfg(const UnloadConfig& c) {
    SaveLock save;
    if (!unload_cfg_save(c)) return false;
    AppLock l;
    g_app.unload.configure(c);
    return true;
}
