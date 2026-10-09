#include "TestHarness.h"
#include "racer/SimClock.h"

using racer::SimClock;

static uint64_t RunFor(SimClock& clock, double fps, double seconds)
{
    const int frames = static_cast<int>(seconds * fps + 0.5);
    for (int i = 0; i < frames; ++i)
        clock.Advance(1.0 / fps);
    return clock.Tick();
}

TEST_CASE("one 60 fps frame runs two 120 Hz steps")
{
    SimClock clock;
    auto r = clock.Advance(1.0 / 60.0);
    CHECK(r.steps == 2);
    CHECK(clock.Tick() == 2);
}

TEST_CASE("simulation rate is independent of render rate (30/60/144 fps)")
{
    for (double fps : { 30.0, 60.0, 144.0 })
    {
        SimClock clock;
        const uint64_t ticks = RunFor(clock, fps, 10.0);
        CHECK(ticks >= 1199 && ticks <= 1200);
    }
}

TEST_CASE("exact multiples do not lose a step to floating-point error")
{
    SimClock clock;
    uint32_t total = 0;
    for (int i = 0; i < 30; ++i)
        total += clock.Advance(1.0 / 30.0).steps;
    CHECK(total == 120);
}

TEST_CASE("100 ms hitch is capped at 8 steps and the overflow is reported")
{
    SimClock clock;
    auto r = clock.Advance(0.100);
    CHECK(r.steps == 8);
    CHECK(r.dropped_seconds > 0.0);
}

TEST_CASE("after a capped hitch, the next normal frame does not burst")
{
    SimClock clock;
    clock.Advance(0.500);
    auto r = clock.Advance(1.0 / 60.0);
    CHECK(r.steps <= 3);
}

TEST_CASE("paused clock discards wall time and resumes without catch-up")
{
    SimClock clock;
    clock.SetPaused(true);
    CHECK(clock.Advance(5.0).steps == 0);
    clock.SetPaused(false);
    CHECK(clock.Advance(1.0 / 120.0).steps == 1);
    CHECK(clock.Tick() == 1);
}

TEST_CASE("re-asserting the same pause state every frame does not lose time")
{
    SimClock clock;
    for (int i = 0; i < 1000; ++i) // 6 ms frames, shorter than one 8.33 ms step
    {
        clock.SetPaused(false);
        clock.Advance(0.006);
    }
    CHECK(clock.Tick() >= 719 && clock.Tick() <= 720);
}

TEST_CASE("interpolation alpha stays within [0, 1)")
{
    SimClock clock;
    for (int i = 0; i < 500; ++i)
    {
        auto r = clock.Advance(1.0 / 144.0);
        CHECK(r.interpolation_alpha >= 0.0f && r.interpolation_alpha < 1.0f);
    }
}

TEST_CASE("negative or zero wall delta runs nothing")
{
    SimClock clock;
    CHECK(clock.Advance(0.0).steps == 0);
    CHECK(clock.Advance(-1.0).steps == 0);
    CHECK(clock.Tick() == 0);
}
