#include "TestHarness.h"
#include "racer/StabilityAssist.h"

#include <cmath>

using namespace racer;

namespace
{
    StabilityInput Driving(float speed, float steer, float yaw_rate)
    {
        StabilityInput in;
        in.forward_speed_ms = speed;
        in.steer = steer;
        in.angular_velocity[1] = yaw_rate;
        in.max_steer_rad = 0.558f; // 32 degrees
        in.wheelbase_m = 2.53f;
        return in;
    }
}

TEST_CASE("stability: yaw the driver asked for is left alone")
{
    StabilityAssist a;
    float out[3];
    // 20 m/s with 0.2 lock -> kinematic ~0.9 rad/s; car yawing at exactly that.
    const float intended = 20.0f * std::tan(0.2f * 0.558f) / 2.53f;
    a.Compute(Driving(20, 0.2f, intended), 1.0f / 120, out);
    CHECK(out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f);
}

TEST_CASE("stability: unrequested spin is countered, bounded")
{
    StabilityAssist a;
    float out[3];
    a.Compute(Driving(30, 0.0f, 2.5f), 1.0f / 120, out); // spinning right with straight wheels
    CHECK(out[1] < 0.0f);
    CHECK(out[1] >= -6.0f);
    a.Compute(Driving(30, 0.0f, -2.5f), 1.0f / 120, out);
    CHECK(out[1] > 0.0f);
}

TEST_CASE("stability: handbrake turns yaw correction off, recovery ramps it back")
{
    StabilityAssist a;
    float out[3];
    StabilityInput drift = Driving(20, 0.5f, 3.0f);
    drift.handbrake = 1.0f;
    a.Compute(drift, 1.0f / 120, out);
    CHECK(out[1] == 0.0f);
    CHECK(a.YawAuthority() == 0.0f);
    drift.handbrake = 0.0f;
    for (int i = 0; i < 36; ++i) // 0.3 s, half the recovery time
        a.Compute(drift, 1.0f / 120, out);
    CHECK(a.YawAuthority() > 0.4f && a.YawAuthority() < 0.6f);
    for (int i = 0; i < 60; ++i)
        a.Compute(drift, 1.0f / 120, out);
    CHECK(a.YawAuthority() == 1.0f);
}

TEST_CASE("stability: airborne car is levelled gently, never full authority")
{
    StabilityAssist a;
    float out[3];
    StabilityInput air = Driving(30, 0, 0);
    air.airborne = true;
    air.pitch_rad = 0.6f;  // nose high
    air.roll_rad = -0.4f;  // left side down
    a.Compute(air, 1.0f / 120, out);
    CHECK(out[0] < 0.0f);  // pitch nose down
    CHECK(out[2] > 0.0f);  // roll back right
    CHECK(out[1] == 0.0f); // yaw untouched in the air
    air.pitch_rad = 3.0f;
    a.Compute(air, 1.0f / 120, out);
    CHECK(out[0] >= -4.0f);
}

TEST_CASE("stability: standing car gets no correction")
{
    StabilityAssist a;
    float out[3];
    a.Compute(Driving(1.0f, 0, 2.0f), 1.0f / 120, out);
    CHECK(out[1] == 0.0f);
}

TEST_CASE("stability: a wall hit's sudden spin is caught hard, then normal assist returns (D-067)")
{
    StabilityAssist a;
    float out[3];
    const float dt = 1.0f / 120;
    for (int i = 0; i < 4; ++i)
        a.Compute(Driving(30, 0.0f, 0.0f), dt, out); // straight
    // the wall kicks the car to 3 rad/s within two steps: far faster than any steering input
    a.Compute(Driving(30, 0.0f, 1.5f), dt, out);
    a.Compute(Driving(30, 0.0f, 3.0f), dt, out);
    CHECK(a.ImpactActive());
    CHECK(out[1] < -10.0f); // twice the normal 6 rad/s^2 cap (harder cost the AI laps: D-067)
    // simulate: the spin is removed within 0.3 s (the normal assist needs 0.5 s)
    float yaw = 3.0f;
    for (int i = 0; i < 36; ++i)
    {
        a.Compute(Driving(30, 0.0f, yaw), dt, out);
        yaw += out[1] * dt;
    }
    CHECK(std::fabs(yaw) < 0.8f); // the normal assist alone would still be at ~1.5 rad/s
    // after the impact window the ordinary bounded assist is back
    for (int i = 0; i < 120; ++i)
        a.Compute(Driving(30, 0.0f, 0.0f), dt, out);
    CHECK(!a.ImpactActive());
    a.Compute(Driving(30, 0.0f, 0.4f), dt, out);
    CHECK(out[1] >= -6.0f);
}

TEST_CASE("stability: a steady driver-made slide does not trigger impact mode")
{
    StabilityAssist a;
    float out[3];
    const float dt = 1.0f / 120;
    float yaw = 0.0f;
    for (int i = 0; i < 60; ++i) // yaw builds up over half a second (a steering flick): 4 rad/s^2
    {
        yaw += 4.0f * dt;
        a.Compute(Driving(25, 0.6f, yaw), dt, out);
        CHECK(!a.ImpactActive());
    }
}

TEST_CASE("stability: one-step yaw noise at low speed is not a hit")
{
    StabilityAssist a;
    float out[3];
    const float dt = 1.0f / 120;
    for (int i = 0; i < 60; ++i) // the duel's false triggers: 5 m/s, yaw flickering 0.05 <-> 0.6
    {
        a.Compute(Driving(5, -0.2f, (i % 2) ? 0.6f : 0.05f), dt, out);
        CHECK(!a.ImpactActive());
    }
    for (int i = 0; i < 60; ++i) // same flicker at speed: it comes back each step, never held
    {
        a.Compute(Driving(30, 0.0f, (i % 2) ? 0.9f : 0.0f), dt, out);
        CHECK(!a.ImpactActive());
    }
}
