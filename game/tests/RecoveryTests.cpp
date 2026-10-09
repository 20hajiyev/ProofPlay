#include "TestHarness.h"
#include "racer/Recovery.h"

#include <limits>

using namespace racer;

namespace
{
    VehicleTelemetry Grounded(float x, float z, float kmh = 60.0f)
    {
        VehicleTelemetry t;
        t.position[0] = x;
        t.position[1] = 0.56f;
        t.position[2] = z;
        t.forward_speed_kmh = kmh;
        for (bool& c : t.wheel_contact)
            c = true;
        return t;
    }
    constexpr float kDt = 1.0f / 120.0f;
}

TEST_CASE("recovery: falling off the world returns to the last safe anchor")
{
    RecoveryMonitor m;
    for (int i = 0; i < 240; ++i)
        CHECK(m.Update(Grounded(10, i * 0.2f), kDt, true).reason == RecoveryReason::None);
    VehicleTelemetry falling = Grounded(10, 60);
    for (bool& c : falling.wheel_contact)
        c = false;
    falling.position[1] = -6.0f;
    RecoveryRequest r = m.Update(falling, kDt, true);
    CHECK(r.reason == RecoveryReason::Fell);
    CHECK(r.position[0] == 10.0f && r.position[2] > 20.0f && r.position[2] < 48.0f);
}

TEST_CASE("recovery: NaN state is treated as a fall")
{
    RecoveryMonitor m;
    for (int i = 0; i < 200; ++i)
        m.Update(Grounded(0, 0), kDt, true);
    VehicleTelemetry bad = Grounded(0, 0);
    bad.velocity[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK(m.Update(bad, kDt, true).reason == RecoveryReason::Fell);
}

TEST_CASE("recovery: upside down for 2 s triggers, a brief flip does not")
{
    RecoveryMonitor m;
    for (int i = 0; i < 200; ++i)
        m.Update(Grounded(0, 0), kDt, true);
    VehicleTelemetry flipped = Grounded(0, 0, 0);
    flipped.up_y = -0.9f;
    for (int i = 0; i < 120; ++i) // 1 s
        CHECK(m.Update(flipped, kDt, false).reason == RecoveryReason::None);
    m.Update(Grounded(0, 0), kDt, false); // rights itself
    RecoveryRequest r;
    for (int i = 0; i < 250 && r.reason == RecoveryReason::None; ++i)
        r = m.Update(flipped, kDt, false);
    CHECK(r.reason == RecoveryReason::UpsideDown);
}

TEST_CASE("recovery: stuck only counts while the driver asks for progress")
{
    RecoveryMonitor m;
    for (int i = 0; i < 200; ++i)
        m.Update(Grounded(0, 0), kDt, true);
    for (int i = 0; i < 120 * 10; ++i) // parked 10 s without throttle
        CHECK(m.Update(Grounded(0, 0, 0), kDt, false).reason == RecoveryReason::None);
    RecoveryRequest r;
    int steps = 0;
    for (; steps < 120 * 6 && r.reason == RecoveryReason::None; ++steps)
        r = m.Update(Grounded(0, 0, 0.5f), kDt, true);
    CHECK(r.reason == RecoveryReason::Stuck);
    CHECK(steps >= 599 && steps <= 601); // plan 2.9: 5 s
}

TEST_CASE("recovery: a stuck spot never becomes the anchor it is sent back to")
{
    // Regression: anchors were recorded whenever grounded and upright, so a car stuck for 5 s
    // had its anchor moved onto the stuck spot and was "recovered" to it again and again.
    RecoveryMonitor m;
    for (int i = 0; i < 240; ++i)
        m.Update(Grounded(10, 100 + i * 0.2f), kDt, true); // driving: anchors near z = 100..148
    RecoveryRequest r;
    for (int i = 0; i < 120 * 6 && r.reason == RecoveryReason::None; ++i)
        r = m.Update(Grounded(40, 300, 0.5f), kDt, true); // stuck elsewhere, grounded and upright
    CHECK(r.reason == RecoveryReason::Stuck);
    CHECK(r.position[0] == 10.0f && r.position[2] < 150.0f);
}

TEST_CASE("recovery: manual reset returns the stored anchor, not the origin")
{
    RecoveryMonitor m;
    CHECK(m.AnchorRequest(RecoveryReason::Stuck).reason == RecoveryReason::None);
    for (int i = 0; i < 200; ++i)
        m.Update(Grounded(42, 17), kDt, true);
    RecoveryRequest r = m.AnchorRequest(RecoveryReason::Stuck);
    CHECK(r.reason == RecoveryReason::Stuck);
    CHECK(r.position[0] == 42.0f && r.position[2] == 17.0f);
}

TEST_CASE("recovery: no anchor yet means no teleport target")
{
    RecoveryMonitor m;
    VehicleTelemetry falling = Grounded(0, 0);
    falling.position[1] = -50.0f;
    CHECK(m.Update(falling, kDt, true).reason == RecoveryReason::None);
}
