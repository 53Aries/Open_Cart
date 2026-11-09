// Open_Cart
#include "scale.h"

#include <algorithm>
#include <cmath>

Scale::Scale()
    : window_(DEFAULT_FILTER_SAMPLES), head_(0), count_(0),
      increment_lb_(DEFAULT_DISPLAY_INCREMENT_LB), motion_lb_(DEFAULT_MOTION_THRESHOLD_LB),
      n_cal_(0), tare_lb_(0.0),
      // 1.55 mV/V, gain 128, 5 cells x 31,000 lb: about 10.7 counts per lb
      nominal_counts_per_lb_(nominal_slope(1.55, 128.0, 155000.0)) {
    for (size_t i = 0; i < kMaxWindow; ++i) buf_[i] = 0;
}

double Scale::nominal_slope(double mv_per_v, double gain, double capacity_lb) {
    return (mv_per_v * 1e-3) * gain * 8388608.0 / capacity_lb;
}

void Scale::configure(size_t window, double increment_lb, double motion_lb) {
    if (window < 1) window = 1;
    if (window > kMaxWindow) window = kMaxWindow;
    window_ = window;
    increment_lb_ = increment_lb > 0 ? increment_lb : 1.0;
    motion_lb_ = motion_lb;
    head_ = 0;
    count_ = 0;
}

void Scale::push_raw(int32_t raw) {
    buf_[head_] = raw;
    head_ = (head_ + 1) % window_;
    if (count_ < window_) ++count_;
}

double Scale::filtered_raw() const {
    if (count_ == 0) return 0.0;
    int32_t tmp[kMaxWindow];
    for (size_t i = 0; i < count_; ++i) tmp[i] = buf_[i];
    std::sort(tmp, tmp + count_);
    // Trimmed mean: drop 10% from each end (at least one when there are >= 5 samples).
    size_t trim = count_ / 10;
    if (trim == 0 && count_ >= 5) trim = 1;
    double sum = 0.0;
    size_t used = 0;
    for (size_t i = trim; i + trim < count_; ++i) {
        sum += tmp[i];
        ++used;
    }
    return used ? sum / static_cast<double>(used) : static_cast<double>(tmp[count_ / 2]);
}

bool Scale::monotonic(const CalPoint* pts, size_t n) {
    if (n < 2) return true;
    int dir = 0;
    for (size_t i = 1; i < n; ++i) {
        if (pts[i].raw == pts[i - 1].raw) return false;
        double d = pts[i].lb - pts[i - 1].lb;
        if (d == 0.0) return false;
        int s = d > 0 ? 1 : -1;
        if (dir == 0) dir = s;
        else if (s != dir) return false;
    }
    return true;
}

double Scale::raw_to_lb(double raw) const {
    if (n_cal_ == 0) return 0.0;
    if (n_cal_ == 1) {
        // Zero reference plus the nominal datasheet slope.
        return cal_[0].lb + (raw - cal_[0].raw) / nominal_counts_per_lb_;
    }
    // Pick the segment containing raw; extrapolate from the end segments outside the range.
    size_t i = 0;
    while (i + 2 < n_cal_ && raw > cal_[i + 1].raw) ++i;
    const CalPoint& a = cal_[i];
    const CalPoint& b = cal_[i + 1];
    double slope = (b.lb - a.lb) / (b.raw - a.raw);
    return a.lb + (raw - a.raw) * slope;
}

double Scale::gross_lb() const {
    if (count_ == 0) return 0.0;
    return raw_to_lb(filtered_raw());
}

double Scale::net_lb() const { return gross_lb() - tare_lb_; }

double Scale::display_lb() const {
    double v = std::floor(net_lb() / increment_lb_ + 0.5) * increment_lb_;
    return v == 0.0 ? 0.0 : v;   // avoid "-0"
}

bool Scale::in_motion() const {
    if (!ready() || n_cal_ == 0) return false;
    int32_t lo = buf_[0], hi = buf_[0];
    for (size_t i = 1; i < count_; ++i) {
        lo = std::min(lo, buf_[i]);
        hi = std::max(hi, buf_[i]);
    }
    double spread = std::fabs(raw_to_lb(hi) - raw_to_lb(lo));
    return spread > motion_lb_;
}

void Scale::tare() { tare_lb_ = gross_lb(); }
void Scale::clear_tare() { tare_lb_ = 0.0; }

bool Scale::zero() {
    if (!ready()) return false;
    double raw = filtered_raw();
    if (n_cal_ == 0) {
        // No calibration yet: the empty scale becomes the zero point (nominal slope).
        if (!add_cal_point_raw(raw, 0.0)) return false;
    } else {
        // Slide the whole calibration along the raw axis so the current reading is 0 lb.
        // Slopes (and so the span) are untouched; only the empty-scale offset moves.
        double delta = raw - lb_to_raw(0.0);
        for (size_t i = 0; i < n_cal_; ++i) cal_[i].raw += delta;
    }
    tare_lb_ = 0.0;
    return true;
}

double Scale::lb_to_raw(double lb) const {
    if (n_cal_ == 0) return 0.0;
    if (n_cal_ == 1) return cal_[0].raw + (lb - cal_[0].lb) * nominal_counts_per_lb_;
    // Segment whose weight range contains lb; otherwise extrapolate from the nearer end segment.
    size_t seg = 0;
    bool found = false;
    for (size_t i = 0; i + 1 < n_cal_; ++i) {
        double lo = std::min(cal_[i].lb, cal_[i + 1].lb);
        double hi = std::max(cal_[i].lb, cal_[i + 1].lb);
        if (lb >= lo && lb <= hi) { seg = i; found = true; break; }
    }
    if (!found) {
        bool increasing = cal_[n_cal_ - 1].lb > cal_[0].lb;
        bool above = lb > std::max(cal_[0].lb, cal_[n_cal_ - 1].lb);
        // Increasing: above the range -> last segment. Decreasing: above -> first segment.
        seg = (above == increasing) ? n_cal_ - 2 : 0;
    }
    const CalPoint& a = cal_[seg];
    const CalPoint& b = cal_[seg + 1];
    return a.raw + (lb - a.lb) * (b.raw - a.raw) / (b.lb - a.lb);
}

bool Scale::add_cal_from_reading(double cart_lb, double certified_lb) {
    if (n_cal_ == 0) return false;
    if (!std::isfinite(cart_lb) || !std::isfinite(certified_lb)) return false;
    // Readings are net; calibration points are gross, so add the tare back to both.
    double raw = lb_to_raw(cart_lb + tare_lb_);
    return add_cal_point_raw(raw, certified_lb + tare_lb_);
}

bool Scale::add_cal_point(double known_lb) {
    if (!ready()) return false;
    return add_cal_point_raw(filtered_raw(), known_lb);
}

bool Scale::add_cal_point_raw(double raw, double known_lb) {
    CalPoint np{raw, known_lb};

    CalPoint tmp[MAX_CAL_POINTS + 1];
    size_t n = 0;
    // Drop an existing point with (nearly) the same weight so re-calibrating a point replaces it.
    for (size_t i = 0; i < n_cal_; ++i) {
        if (std::fabs(cal_[i].lb - known_lb) < 0.5) continue;
        tmp[n++] = cal_[i];
    }
    if (n >= MAX_CAL_POINTS) return false;
    tmp[n++] = np;
    std::sort(tmp, tmp + n, [](const CalPoint& x, const CalPoint& y) { return x.raw < y.raw; });
    if (!monotonic(tmp, n)) return false;

    for (size_t i = 0; i < n; ++i) cal_[i] = tmp[i];
    n_cal_ = n;
    return true;
}

void Scale::clear_calibration() { n_cal_ = 0; }

void Scale::set_calibration(const CalPoint* pts, size_t n) {
    if (n > MAX_CAL_POINTS) return;
    CalPoint tmp[MAX_CAL_POINTS];
    for (size_t i = 0; i < n; ++i) tmp[i] = pts[i];
    std::sort(tmp, tmp + n, [](const CalPoint& x, const CalPoint& y) { return x.raw < y.raw; });
    if (!monotonic(tmp, n)) return;
    for (size_t i = 0; i < n; ++i) cal_[i] = tmp[i];
    n_cal_ = n;
}

size_t Scale::get_calibration(CalPoint* out, size_t max) const {
    size_t n = n_cal_ < max ? n_cal_ : max;
    for (size_t i = 0; i < n; ++i) out[i] = cal_[i];
    return n;
}
