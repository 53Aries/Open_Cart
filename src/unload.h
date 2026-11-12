// Open_Cart
#pragma once
// Unload detection: pure logic, no ESP-IDF dependencies, so it builds and tests on a host
// (see test/test_unload.cpp).
//
// Fed the calibrated gross weight at a fixed rate (UNLOAD_TICK_MS). Tare does not matter: only
// differences are used. An unload starts when the weight falls start_drop_lb below the highest
// weight of the last ~10 s (that peak is the "last known weight before the drop"). While it runs,
// running_lb() = start - current weight. It ends when the weight has stopped falling for
// end_quiet_s, or when it rises start_drop_lb above the lowest point (loading started).

#include <cstddef>
#include <cstdint>
#include "config.h"

// Stored in NVS as raw bytes (see unload_log.cpp): do not add, remove or reorder members.
struct UnloadConfig {
    float start_drop_lb;    // drop below the recent peak that counts as "unloading"
    float end_quiet_s;      // seconds of no further drop that ends the unload (max 9)
    float min_unload_lb;    // shorter unloads are ignored and not logged
    bool enabled;           // automatic detection on/off (manual start/stop works either way)
};

class UnloadTracker {
public:
    static constexpr size_t kHist = 100;      // ticks of history (10 s at 10 Hz)
    static constexpr size_t kWarmTicks = 30;  // history needed before a start can be detected

    struct Event {
        bool finished = false;   // an unload ended and should be logged
        double start_lb = 0, end_lb = 0, total_lb = 0;
    };

    // Allowed ranges for values entered on the web page or the VT.
    static constexpr double kMinStartDropLb = 10, kMaxStartDropLb = 20000;
    static constexpr double kMinQuietS = 1, kMaxQuietS = 9;
    static constexpr double kMinUnloadLb = 0, kMaxUnloadLb = 100000;
    static bool start_drop_ok(double v) { return v >= kMinStartDropLb && v <= kMaxStartDropLb; }   // NaN fails
    static bool quiet_ok(double v) { return v >= kMinQuietS && v <= kMaxQuietS; }
    static bool min_unload_ok(double v) { return v >= kMinUnloadLb && v <= kMaxUnloadLb; }

    UnloadTracker();

    void configure(const UnloadConfig& c);
    static UnloadConfig default_config();

    // Call once per tick with the calibrated gross weight.
    Event update(double gross_lb);

    // Weight is not valid (not ready, not calibrated): drop history and abandon any unload.
    void invalidate();

    // Manual control (the Unload button). If no unload is running, starts one at the current weight
    // and returns an unfinished Event. If one is running (manual or automatic), ends it now and
    // returns it finished. A manual unload runs until stopped and is logged whatever its size.
    // Does nothing (unfinished Event) if there is no valid weight yet.
    Event toggle_manual();

    bool active() const { return active_; }
    bool manual() const { return active_ && manual_; }
    bool auto_enabled() const { return cfg_.enabled; }
    // Worth showing the operator: a manual unload, or an automatic one already past the minimum size.
    bool showing() const { return active_ && (manual_ || running_ >= cfg_.min_unload_lb); }
    double start_lb() const { return start_; }
    double running_lb() const { return active_ ? running_ : 0.0; }

    bool has_last() const { return has_last_; }
    double last_lb() const { return last_; }
    void set_last(double lb) { last_ = lb; has_last_ = true; }

private:
    void push(double w);
    double peak() const;
    double ago(size_t ticks) const;   // weight `ticks` ticks before the newest sample
    Event finish(double end, double now, bool log_it);

    double hist_[kHist];
    size_t head_, n_;
    UnloadConfig cfg_;
    bool active_, manual_;
    bool have_cur_;
    double cur_;
    double start_, low_, running_;
    bool has_last_;
    double last_;
};
