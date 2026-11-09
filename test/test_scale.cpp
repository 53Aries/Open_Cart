// Open_Cart
// Host-side tests for the pure scale logic. Build and run:
//   g++ -std=c++17 -I src -o /tmp/test_scale test/test_scale.cpp src/scale.cpp && /tmp/test_scale
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "scale.h"

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++g_fail; } } while (0)

// Simulated ADC: offset + counts_per_lb * weight + noise
static double g_offset = 12345.0;
static double g_cpl = 10.737;   // counts per lb (nominal)
static double noise(double amp) { return (std::rand() / (double)RAND_MAX - 0.5) * 2.0 * amp; }
static void feed(Scale& s, double lb, int n, double noise_counts = 20.0) {
    for (int i = 0; i < n; ++i) s.push_raw((int32_t)std::lround(g_offset + g_cpl * lb + noise(noise_counts)));
}

int main() {
    std::srand(1);

    // nominal slope matches the hand calculation (about 10.74 counts/lb)
    double ns = Scale::nominal_slope(1.55, 128.0, 155000.0);
    CHECK(std::fabs(ns - 10.737) < 0.01);

    Scale s;
    s.configure(20, 10.0, 15.0);
    CHECK(!s.has_calibration());
    CHECK(s.gross_lb() == 0.0);

    // Not ready until the window fills; can't add a cal point yet
    feed(s, 0.0, 5);
    CHECK(!s.ready());
    CHECK(!s.add_cal_point(0.0));
    feed(s, 0.0, 20);
    CHECK(s.ready());

    // One point = zero + nominal slope (approximate)
    CHECK(s.add_cal_point(0.0));
    CHECK(s.is_approximate());
    feed(s, 20000.0, 20);
    CHECK(std::fabs(s.gross_lb() - 20000.0) < 60.0);   // nominal slope is exact in this sim

    // Two-point calibration with a "real" scale slope that differs from nominal
    g_cpl = 10.90;
    Scale t;
    t.configure(20, 10.0, 15.0);
    feed(t, 0.0, 25);
    CHECK(t.add_cal_point(0.0));
    feed(t, 30000.0, 25);
    CHECK(t.add_cal_point(30000.0));
    CHECK(!t.is_approximate());
    CHECK(t.cal_count() == 2);
    feed(t, 12345.0, 25);
    CHECK(std::fabs(t.gross_lb() - 12345.0) < 10.0);
    feed(t, 45000.0, 25);                     // extrapolation beyond the last point
    CHECK(std::fabs(t.gross_lb() - 45000.0) < 20.0);

    // Tare
    feed(t, 8000.0, 25);
    t.tare();
    feed(t, 8000.0, 25);
    CHECK(std::fabs(t.net_lb()) < 10.0);
    feed(t, 28000.0, 25);
    CHECK(std::fabs(t.net_lb() - 20000.0) < 15.0);
    CHECK(std::fabs(std::fmod(t.display_lb(), 10.0)) < 1e-9);   // multiple of the increment
    t.clear_tare();
    CHECK(t.tare_lb() == 0.0);

    // Zero: empty cart drifts +300 lb; ZERO removes the offset and keeps the span
    {
        double keep = g_offset;
        g_offset += 300.0 * g_cpl;
        feed(t, 0.0, 25);
        CHECK(std::fabs(t.gross_lb() - 300.0) < 10.0);
        t.set_tare_lb(50.0);
        CHECK(t.zero());
        CHECK(t.tare_lb() == 0.0);
        CHECK(t.cal_count() == 2);
        CHECK(std::fabs(t.gross_lb()) < 10.0);
        feed(t, 30000.0, 25);
        CHECK(std::fabs(t.gross_lb() - 30000.0) < 15.0);
        g_offset = keep;
        // No calibration: zero adds a 0 lb point; not ready: refused
        Scale z;
        z.configure(20, 10.0, 15.0);
        feed(z, 0.0, 5);
        CHECK(!z.zero());
        feed(z, 0.0, 20);
        CHECK(z.zero() && z.is_approximate());
        feed(t, 0.0, 25);
        CHECK(t.zero());   // re-zero with the offset back to normal
        feed(t, 28000.0, 25);   // restore the state the next test expects
    }

    // Re-calibrating a point replaces it instead of adding a duplicate
    CHECK(t.add_cal_point(30000.0));
    CHECK(t.cal_count() == 2);

    // Non-monotonic points are rejected (a weight that goes the wrong way)
    Scale u;
    u.configure(10, 10.0, 15.0);
    g_cpl = 10.9;
    feed(u, 0.0, 12);
    CHECK(u.add_cal_point(0.0));
    feed(u, 10000.0, 12);
    CHECK(u.add_cal_point(10000.0));
    feed(u, 20000.0, 12);
    CHECK(!u.add_cal_point(5000.0));            // 5000 lb at the raw of 20000 breaks monotonicity
    CHECK(u.cal_count() == 2);

    // Motion detection
    Scale m;
    m.configure(20, 10.0, 15.0);
    g_cpl = 10.737;
    feed(m, 0.0, 25);
    CHECK(m.add_cal_point(0.0));
    feed(m, 10000.0, 25, 5.0);
    CHECK(!m.in_motion());
    for (int i = 0; i < 10; ++i) feed(m, 10000.0 + (i % 2 ? 400.0 : -400.0), 1, 5.0);   // swinging load
    CHECK(m.in_motion());

    // Calibration points round-trip
    CalPoint pts[MAX_CAL_POINTS];
    size_t n = t.get_calibration(pts, MAX_CAL_POINTS);
    Scale v;
    v.set_calibration(pts, n);
    CHECK(v.cal_count() == n);
    CHECK(std::fabs(v.raw_to_lb(pts[0].raw) - pts[0].lb) < 1e-6);

    // Negative net values round without "-0"
    Scale w;
    w.configure(5, 10.0, 15.0);
    feed(w, 0.0, 6, 0.0);
    w.add_cal_point(0.0);
    w.set_tare_lb(2.0);
    CHECK(w.display_lb() == 0.0 && !std::signbit(w.display_lb()));

    // Calibrate from a certified-scale comparison: cart showed 20000, certified said 20400
    {
        g_cpl = 10.90;
        Scale c;
        c.configure(20, 1.0, 15.0);
        feed(c, 0.0, 25, 0.0);
        CHECK(c.add_cal_point(0.0));                 // zero + nominal slope (10.737)
        CHECK(std::fabs(c.raw_to_lb(c.lb_to_raw(20000.0)) - 20000.0) < 1e-6);
        CHECK(c.add_cal_from_reading(20000.0, 20400.0));
        CHECK(c.cal_count() == 2);
        // the raw that used to read 20000 now reads 20400
        double raw_old = g_offset + 20000.0 * 10.737;
        CHECK(std::fabs(c.raw_to_lb(raw_old) - 20400.0) < 1.0);
        CHECK(std::fabs(c.raw_to_lb(g_offset)) < 1.0);   // zero unchanged
        // a second comparison below the first, inside the range
        CHECK(c.add_cal_from_reading(10000.0, 10150.0));
        CHECK(c.cal_count() == 3);
        CHECK(std::fabs(c.raw_to_lb(g_offset + 10000.0 * 10.737) - 10150.0) < 1.0);
        // inverse round trip across and beyond the points
        for (double w : {-500.0, 5000.0, 15000.0, 30000.0})
            CHECK(std::fabs(c.raw_to_lb(c.lb_to_raw(w)) - w) < 1e-6);
        // rejects: no calibration, non-monotonic result
        Scale e;
        CHECK(!e.add_cal_from_reading(100.0, 100.0));
        CHECK(!c.add_cal_from_reading(20000.0, 5000.0));
        CHECK(!c.add_cal_from_reading(std::nan(""), 5.0));
    }

    if (g_fail == 0) std::printf("ALL TESTS PASSED\n");
    return g_fail ? 1 : 0;
}
