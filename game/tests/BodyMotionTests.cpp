#include "TestHarness.h"
#include "racer/BodyMotion.h"

#include <algorithm>
#include <cmath>

using namespace racer;

namespace
{
    constexpr float kDt = 1.0f / 60.0f;
    BodyMotionInput Cruise(float speed, float yaw_rate)
    {
        BodyMotionInput in;
        in.forward_speed = speed;
        in.yaw_rate = yaw_rate;
        in.grounded = true;
        return in;
    }
}

TEST_CASE("body motion: at rest nothing moves")
{
    BodyMotion m;
    for (int i = 0; i < 240; ++i)
        m.Update(Cruise(0, 0), kDt);
    CHECK(std::fabs(m.Pose().roll) < 1e-5f && std::fabs(m.Pose().pitch) < 1e-5f && std::fabs(m.Pose().squash - 1.0f) < 1e-5f);
}

TEST_CASE("body motion: a right turn leans the body left (outward), settling at the target")
{
    BodyMotion m;
    for (int i = 0; i < 300; ++i)
        m.Update(Cruise(20, 0.3f), kDt); // 6 m/s^2 lateral
    const float expected = -m.Params().roll_per_accel * 6.0f;
    CHECK(m.Pose().roll < 0.0f);
    CHECK(std::fabs(m.Pose().roll - expected) < 0.002f);
}

TEST_CASE("body motion: lean is capped however hard the turn")
{
    BodyMotion m;
    for (int i = 0; i < 300; ++i)
        m.Update(Cruise(60, 1.0f), kDt);
    CHECK(std::fabs(m.Pose().roll) <= m.Params().max_roll + 1e-4f);
}

TEST_CASE("body motion: accelerating lifts the nose, braking dives it")
{
    BodyMotion up, down;
    for (int i = 0; i < 30; ++i)
    {
        up.Update(Cruise(10.0f + i * 0.08f, 0), kDt);   // +4.8 m/s^2
        down.Update(Cruise(30.0f - i * 0.15f, 0), kDt); // -9 m/s^2
    }
    CHECK(up.Pose().pitch > 0.0f);
    CHECK(down.Pose().pitch < 0.0f);
}

TEST_CASE("body motion: a landing squashes, springs past rest (toon bounce) and settles")
{
    BodyMotion m;
    BodyMotionInput air = Cruise(20, 0);
    air.grounded = false;
    air.vertical_speed = -6.0f;
    for (int i = 0; i < 30; ++i)
        m.Update(air, kDt);
    float lowest = 1.0f, highest = 1.0f;
    for (int i = 0; i < 90; ++i)
    {
        m.Update(Cruise(20, 0), kDt);
        lowest = std::min(lowest, m.Pose().squash);
        highest = std::max(highest, m.Pose().squash);
    }
    CHECK(lowest < 0.99f);  // a small but visible squash (arch clearance caps it)
    CHECK(highest > 1.0f);  // overshoot: the bounce
    CHECK(std::fabs(m.Pose().squash - 1.0f) < 0.01f); // settled after 1.5 s
}

TEST_CASE("body motion: a hit kicks the body and it recovers")
{
    BodyMotion m;
    m.Hit(1.0f, 0.0f); // from the right
    float peak = 0;
    for (int i = 0; i < 20; ++i)
    {
        m.Update(Cruise(20, 0), kDt);
        peak = std::max(peak, std::fabs(m.Pose().roll));
    }
    CHECK(peak > 0.01f);
    for (int i = 0; i < 120; ++i)
        m.Update(Cruise(20, 0), kDt);
    CHECK(std::fabs(m.Pose().roll) < 0.002f);
}

TEST_CASE("body motion: the worst lean, dive and squash never drop a wheel arch onto the tyre")
{
    // Regression (owner report 2026-09-29: "wheels go into the body"). The body mesh rotates and
    // scales about its ground-level origin; an arch at half-track x and arch height z drops by
    // x*sin(roll) + y*sin(pitch) + z*(1 - squash). It must stay inside the arch clearance
    // (tools/generate_tuner_cars.py cuts arches at r + 0.08) with a margin.
    const float half_track = 0.93f, half_wb = 1.48f, arch_z = 0.85f, budget = 0.07f;
    BodyMotion m;
    float worst = 0.0f;
    auto sample = [&] {
        const BodyPose& p = m.Pose();
        const float drop = half_track * std::sin(std::fabs(p.roll)) + half_wb * std::sin(std::fabs(p.pitch)) + arch_z * std::max(0.0f, 1.0f - p.squash);
        worst = std::max(worst, drop);
    };
    for (int i = 0; i < 240; ++i) { m.Update(Cruise(60.0f, 1.2f), kDt); sample(); }            // flat-out hairpin
    for (int i = 0; i < 60; ++i) { m.Update(Cruise(60.0f - i * 0.8f, -1.2f), kDt); sample(); } // brake + flick
    BodyMotionInput air = Cruise(40, 0);
    air.grounded = false;
    air.vertical_speed = -12.0f;                                                                // big jump
    for (int i = 0; i < 40; ++i) { m.Update(air, kDt); sample(); }
    m.Hit(1.0f, 1.0f, 2.0f);                                                                    // heavy hit on landing
    for (int i = 0; i < 120; ++i) { m.Update(Cruise(40, 0.8f), kDt); sample(); }
    CHECK(worst < budget);
}

TEST_CASE("body motion: the same drive looks the same at 30, 60 and 144 fps")
{
    float roll[3];
    const float rates[3] = { 30, 60, 144 };
    for (int r = 0; r < 3; ++r)
    {
        BodyMotion m;
        const float dt = 1.0f / rates[r];
        for (float t = 0; t < 1.0f; t += dt)
            m.Update(Cruise(20, 0.3f), dt);
        roll[r] = m.Pose().roll;
    }
    CHECK(std::fabs(roll[0] - roll[2]) < 0.004f && std::fabs(roll[1] - roll[2]) < 0.002f);
}
