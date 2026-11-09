// Open_Cart
// Host-side tests for the unload detector. Build and run:
//   g++ -std=c++17 -I src -o /tmp/test_unload test/test_unload.cpp src/unload.cpp && /tmp/test_unload
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "unload.h"

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++g_fail; } } while (0)

static double noise(double amp) { return (std::rand() / (double)RAND_MAX - 0.5) * 2.0 * amp; }

// Runs a weight profile (one value per 100 ms tick) and returns the finished unloads.
static std::vector<double> run(UnloadTracker& t, const std::vector<double>& p) {
    std::vector<double> out;
    for (double w : p) {
        auto ev = t.update(w + noise(15));
        if (ev.finished) out.push_back(ev.total_lb);
    }
    return out;
}
static void ramp(std::vector<double>& p, double per_tick, int n) {
    double w = p.back();
    for (int i = 0; i < n; ++i) p.push_back(w -= per_tick);
}
static void hold(std::vector<double>& p, int n) { p.insert(p.end(), n, p.back()); }

int main() {
    std::srand(1);

    {   // 40 s unload at 800 lb/s from 60,000 lb: about 32,000 lb out
        UnloadTracker t;
        std::vector<double> p(200, 60000.0);
        ramp(p, 80, 400);
        hold(p, 200);
        auto r = run(t, p);
        CHECK(r.size() == 1);
        CHECK(r.size() == 1 && std::fabs(r[0] - 32000.0) < 100.0);
        CHECK(t.has_last() && std::fabs(t.last_lb() - 32000.0) < 100.0);
        CHECK(!t.active());
    }
    {   // loading only: nothing
        UnloadTracker t;
        std::vector<double> p;
        for (int i = 0; i < 400; ++i) p.push_back(20000.0 + 100.0 * i);
        CHECK(run(t, p).empty());
    }
    {   // two unloads with a pause between: two events, no retrigger from old peak
        UnloadTracker t;
        std::vector<double> p(200, 60000.0);
        ramp(p, 80, 100);
        hold(p, 100);
        ramp(p, 80, 100);
        hold(p, 200);
        auto r = run(t, p);
        CHECK(r.size() == 2);
    }
    {   // below the minimum size: ignored
        UnloadTracker t;
        std::vector<double> p(200, 60000.0);
        ramp(p, 2, 80);   // 160 lb
        hold(p, 200);
        CHECK(run(t, p).empty());
    }
    {   // running total is visible while unloading
        UnloadTracker t;
        std::vector<double> p(200, 60000.0);
        ramp(p, 80, 100);
        for (double w : p) t.update(w);
        CHECK(t.active() && t.showing() && t.running_lb() > 7000.0);
    }
    {   // auto-detect off: a real unload is ignored
        UnloadTracker t;
        UnloadConfig c = UnloadTracker::default_config();
        c.enabled = false;
        t.configure(c);
        std::vector<double> p(200, 60000.0);
        ramp(p, 80, 400);
        hold(p, 200);
        CHECK(run(t, p).empty());
        CHECK(!t.active());
    }
    {   // manual unload with auto-detect off: start, run, stop; logged whatever the size
        UnloadTracker t;
        UnloadConfig c = UnloadTracker::default_config();
        c.enabled = false;
        t.configure(c);
        CHECK(!t.toggle_manual().finished && !t.active());   // no weight yet: nothing happens
        t.update(50000.0);
        t.toggle_manual();
        CHECK(t.manual() && t.showing());
        for (int i = 0; i < 100; ++i) t.update(50000.0 - i * 10.0);
        CHECK(t.active());   // does not end by itself, even though it is quiet
        for (int i = 0; i < 200; ++i) t.update(49010.0);
        CHECK(t.manual());
        auto ev = t.toggle_manual();
        CHECK(ev.finished && std::fabs(ev.total_lb - 990.0) < 1.0);
        CHECK(!t.active() && t.has_last());
    }
    {   // manual start with auto-detect on: auto end is suppressed until stopped
        UnloadTracker t;
        for (int i = 0; i < 50; ++i) t.update(60000.0);
        t.toggle_manual();
        for (int i = 0; i < 100; ++i) t.update(60000.0 - i * 80.0);
        for (int i = 0; i < 100; ++i) t.update(52080.0);
        CHECK(t.manual());
        CHECK(t.toggle_manual().finished);
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "all unload tests passed\n", g_fail);
    return g_fail != 0;
}
