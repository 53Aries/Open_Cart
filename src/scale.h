// Open_Cart
#pragma once
// Pure scale logic: filtering, motion detection, tare, piecewise-linear calibration.
// No ESP-IDF dependencies, so it builds and tests on a host (see test/test_scale.cpp).

#include <cstddef>
#include <cstdint>
#include "config.h"

// Load cell layout. n_cells identical cells in parallel through a junction box.
struct CellConfig {
    uint8_t n_cells;
    float capacity_lb;     // rated capacity of ONE cell
    float mv_per_v;        // rated output of one cell at capacity
    double total_capacity_lb() const { return n_cells * static_cast<double>(capacity_lb); }
    bool valid() const { return n_cells >= 1 && n_cells <= 12 && capacity_lb >= 100 && capacity_lb <= 1000000 && mv_per_v >= 0.5 && mv_per_v <= 5.0; }
    static CellConfig defaults() { return {DEFAULT_CELL_COUNT, (float)DEFAULT_CELL_CAPACITY_LB, (float)DEFAULT_CELL_MV_PER_V}; }
};

struct CalPoint {
    double raw;   // filtered ADC counts
    double lb;    // known weight in pounds (gross, tare-free)
};

class Scale {
public:
    static constexpr size_t kMaxWindow = 64;

    Scale();

    // window: samples to average (1..64). increment_lb: display rounding. motion_lb: spread that counts as motion.
    void configure(size_t window, double increment_lb, double motion_lb);

    // Feed every ADC sample (signed 24-bit result as int32).
    void push_raw(int32_t raw);

    // True once the window is full of samples.
    bool ready() const { return count_ >= window_; }

    // Filtered ADC counts (trimmed mean over the window).
    double filtered_raw() const;

    // Calibrated weight without tare. 0 if there is no calibration yet.
    double gross_lb() const;
    // gross minus tare.
    double net_lb() const;
    // net rounded to the display increment.
    double display_lb() const;

    // True if the window's spread, converted to pounds, exceeds the motion threshold.
    bool in_motion() const;

    // Tare
    void tare();                    // tare to current gross
    void clear_tare();
    // Zero: make the current reading 0 lb gross by shifting the calibration's zero offset (the span
    // is kept), and clear the tare. For correcting drift or buildup on an empty cart. Unlike tare it
    // changes the saved calibration. With no calibration it adds a 0 lb point. False if not ready.
    bool zero();
    double tare_lb() const { return tare_lb_; }
    void set_tare_lb(double v) { tare_lb_ = v; }

    // Calibration. Points are kept sorted by raw. Weights must be strictly
    // monotonic against raw (all increasing or all decreasing).
    bool add_cal_point(double known_lb);   // uses the current filtered raw; false if not ready/full/non-monotonic
    // Calibrate from a comparison with a certified scale: the cart displayed cart_lb (net, as
    // shown) for a load that the certified scale weighed at certified_lb (net). The raw counts
    // that produced cart_lb are recovered from the current calibration, so the load does not
    // have to be on the cart now. False if there is no calibration, a weight is invalid, or
    // the result is not monotonic. Only valid if the calibration has not changed since the
    // cart reading was taken.
    bool add_cal_from_reading(double cart_lb, double certified_lb);
    // Inverse of raw_to_lb (exposed for tests). Needs at least one calibration point.
    double lb_to_raw(double lb) const;
    void clear_calibration();
    void set_calibration(const CalPoint* pts, size_t n);   // replaces everything; false-safe (ignored if invalid)
    size_t get_calibration(CalPoint* out, size_t max) const;
    size_t cal_count() const { return n_cal_; }

    // With only one point (a zero) the scale uses a nominal slope and is "approximate".
    bool has_calibration() const { return n_cal_ >= 1; }
    bool is_approximate() const { return n_cal_ == 1; }
    void set_nominal_counts_per_lb(double v) { nominal_counts_per_lb_ = v; }
    double nominal_counts_per_lb() const { return nominal_counts_per_lb_; }

    // Nominal slope from datasheet numbers: ratiometric, so excitation cancels.
    //   counts/lb = (mV/V * 1e-3) * gain * 2^23 / capacity_lb
    static double nominal_slope(double mv_per_v, double gain, double capacity_lb);
    static double nominal_slope(const CellConfig& c) { return nominal_slope(c.mv_per_v, ADC_GAIN, c.total_capacity_lb()); }

    // Convert raw counts to pounds using calibration points (exposed for tests).
    double raw_to_lb(double raw) const;

private:
    static bool monotonic(const CalPoint* pts, size_t n);
    bool add_cal_point_raw(double raw, double known_lb);

    int32_t buf_[kMaxWindow];
    size_t window_;
    size_t head_;
    size_t count_;
    double increment_lb_;
    double motion_lb_;

    CalPoint cal_[MAX_CAL_POINTS];
    size_t n_cal_;
    double tare_lb_;
    double nominal_counts_per_lb_;
};
