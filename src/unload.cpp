// Open_Cart
#include "unload.h"

#include <cmath>

UnloadConfig UnloadTracker::default_config() {
    return {DEFAULT_UNLOAD_START_DROP_LB, DEFAULT_UNLOAD_QUIET_S, DEFAULT_UNLOAD_MIN_LB, true};
}

UnloadTracker::UnloadTracker()
    : hist_{}, head_(0), n_(0), cfg_(default_config()), active_(false), manual_(false),
      have_cur_(false), cur_(0), start_(0), low_(0), running_(0), has_last_(false), last_(0) {}

void UnloadTracker::configure(const UnloadConfig& c) {
    cfg_ = c;
    if (cfg_.start_drop_lb < 1) cfg_.start_drop_lb = 1;
    if (cfg_.end_quiet_s < 1) cfg_.end_quiet_s = 1;
    if (cfg_.end_quiet_s > 9) cfg_.end_quiet_s = 9;   // must fit inside the history
    if (cfg_.min_unload_lb < 0) cfg_.min_unload_lb = 0;
}

void UnloadTracker::push(double w) {
    hist_[head_] = w;
    head_ = (head_ + 1) % kHist;
    if (n_ < kHist) ++n_;
}

double UnloadTracker::peak() const {
    double m = hist_[(head_ + kHist - 1) % kHist];
    for (size_t i = 0; i < n_; ++i) {
        double v = hist_[(head_ + kHist - 1 - i) % kHist];
        if (v > m) m = v;
    }
    return m;
}

double UnloadTracker::ago(size_t ticks) const {
    if (n_ == 0) return 0;
    if (ticks > n_ - 1) ticks = n_ - 1;
    return hist_[(head_ + kHist - 1 - ticks) % kHist];
}

void UnloadTracker::invalidate() {
    active_ = false;
    manual_ = false;
    have_cur_ = false;
    running_ = 0;
    n_ = 0;
    head_ = 0;
}

// Ends the running unload at weight `end`. History restarts so the old peak cannot retrigger.
UnloadTracker::Event UnloadTracker::finish(double end, double now, bool log_it) {
    Event ev;
    const double total = start_ - end;
    active_ = false;
    manual_ = false;
    running_ = 0;
    n_ = 0;
    head_ = 0;
    push(now);
    if (log_it && total > 0) {
        ev.finished = true;
        ev.start_lb = start_;
        ev.end_lb = end;
        ev.total_lb = total;
        set_last(total);
    }
    return ev;
}

UnloadTracker::Event UnloadTracker::toggle_manual() {
    if (!have_cur_) return Event();
    if (active_) return finish(manual_ ? cur_ : low_, cur_, true);
    active_ = true;
    manual_ = true;
    start_ = cur_;
    low_ = cur_;
    running_ = 0;
    return Event();
}

UnloadTracker::Event UnloadTracker::update(double w) {
    Event ev;
    cur_ = w;
    have_cur_ = true;

    if (manual_) {
        push(w);
        if (w < low_) low_ = w;
        running_ = start_ - w;
        if (running_ < 0) running_ = 0;
        return ev;   // ends only when the operator presses the button
    }

    if (!active_) {
        if (cfg_.enabled && n_ >= kWarmTicks) {
            const double ref = peak();
            if (ref - w >= cfg_.start_drop_lb) {
                active_ = true;
                start_ = ref;
                low_ = w;
                running_ = ref - w;
            }
        }
        push(w);
        return ev;
    }

    push(w);
    if (w < low_) low_ = w;
    running_ = start_ - w;
    if (running_ < 0) running_ = 0;

    const size_t quiet_ticks = static_cast<size_t>(std::lround(cfg_.end_quiet_s * 1000.0 / UNLOAD_TICK_MS));
    const bool stopped = (ago(quiet_ticks) - w) < cfg_.start_drop_lb * 0.5;   // barely falling any more
    const bool loading = (w - low_) >= cfg_.start_drop_lb;                    // rose back up
    if (!stopped && !loading) return ev;

    const double end = loading ? low_ : w;
    const double total = start_ - end;
    return finish(end, w, total >= cfg_.min_unload_lb);
}
