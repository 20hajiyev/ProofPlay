#include "TestHarness.h"
#include "racer/CameraFeel.h"

#include <cmath>
#include <cstdio>

using namespace racer;

namespace
{
    constexpr float kDt = 1.0f / 60.0f;
    CameraFeelInput At(float kmh, float yaw_rate = 0.0f)
    {
        CameraFeelInput in;
        in.speed_kmh = kmh;
        in.yaw_rate = yaw_rate;
        return in;
    }
    void Run(CameraFeel& c, const CameraFeelInput& in, float seconds)
    {
        for (float t = 0; t < seconds; t += kDt)
            c.Update(in, kDt);
    }
}

TEST_CASE("camera feel: field of view widens with speed and is capped")
{
    CameraFeel slow, fast, flat_out;
    Run(slow, At(40), 3);
    Run(fast, At(160), 3);
    Run(flat_out, At(400), 3);
    CHECK(fast.Output().fov_deg > slow.Output().fov_deg + 5.0f);
    CHECK(flat_out.Output().fov_deg <= slow.Params().max_fov_deg + 0.01f);
}

TEST_CASE("camera feel: boost punches the field of view and pulls the camera back")
{
    CameraFeel plain, boosted;
    CameraFeelInput in = At(150);
    Run(plain, in, 2);
    in.boost = true;
    Run(boosted, in, 2);
    CHECK(boosted.Output().fov_deg > plain.Output().fov_deg + 3.0f);
    CHECK(boosted.Output().distance > plain.Output().distance);
}

TEST_CASE("camera feel: in a right turn the camera swings out left and rolls into the turn")
{
    CameraFeel c;
    Run(c, At(120, 0.6f), 2);
    CHECK(c.Output().lateral < -0.1f);
    CHECK(c.Output().roll_deg > 0.2f);
}

TEST_CASE("camera feel: a hit shakes the camera and the shake dies away")
{
    CameraFeel c;
    Run(c, At(100), 1);
    c.AddTrauma(0.8f);
    float peak = 0;
    for (int i = 0; i < 12; ++i)
    {
        c.Update(At(100), kDt);
        peak = std::fmax(peak, std::fabs(c.Output().shake_x) + std::fabs(c.Output().shake_y));
    }
    CHECK(peak > 0.05f);
    Run(c, At(100), 2);
    CHECK(std::fabs(c.Output().shake_x) + std::fabs(c.Output().shake_y) < 0.01f);
}

TEST_CASE("camera feel: the reduced-shake setting cuts the shake to a third or less")
{
    CameraFeel full, calm;
    CameraFeelInput in = At(100);
    CameraFeelInput calm_in = in;
    calm_in.reduce_shake = true;
    float a = 0, b = 0;
    full.AddTrauma(1.0f);
    calm.AddTrauma(1.0f);
    for (int i = 0; i < 30; ++i)
    {
        full.Update(in, kDt);
        calm.Update(calm_in, kDt);
        a += std::fabs(full.Output().shake_x) + std::fabs(full.Output().shake_y);
        b += std::fabs(calm.Output().shake_x) + std::fabs(calm.Output().shake_y);
    }
    CHECK(b <= a / 3.0f + 1e-4f);
}

TEST_CASE("camera feel: speed lines appear only when fast and never with reduced motion")
{
    CameraFeel c;
    Run(c, At(80), 1);
    CHECK(c.Output().speed_lines < 0.01f);
    Run(c, At(220), 1);
    CHECK(c.Output().speed_lines > 0.5f);
    CameraFeelInput calm = At(220);
    calm.reduce_shake = true;
    Run(c, calm, 1);
    CHECK(c.Output().speed_lines < 0.01f);
}

TEST_CASE("camera feel: the same drive gives the same camera at 30 and 144 fps")
{
    CameraFeel a, b;
    for (float t = 0; t < 2.0f; t += 1.0f / 30.0f)
        a.Update(At(60 + 60 * t, 0.3f), 1.0f / 30.0f);
    for (float t = 0; t < 2.0f; t += 1.0f / 144.0f)
        b.Update(At(60 + 60 * t, 0.3f), 1.0f / 144.0f);
    CHECK(std::fabs(a.Output().fov_deg - b.Output().fov_deg) < 0.5f);
    CHECK(std::fabs(a.Output().lateral - b.Output().lateral) < 0.05f);
}

TEST_CASE("camera feel: flat out, the car still fills the screen as at a standstill (D-078)")
{
    // Owner (2026-10-03): "when it accelerates a lot the camera goes far from the car". What the eye
    // reads is the car's size on screen ~ 1 / (distance * tan(fov / 2)): a wide FOV shrinks the car
    // as much as pulling back does.
    CameraFeel still, fast;
    Run(still, At(0), 3);
    Run(fast, At(200), 3);
    auto size = [](const CameraFeel& c) {
        return 1.0f / (c.Output().distance * std::tan(c.Output().fov_deg * 3.14159265f / 360.0f));
    };
    std::printf("  INFO on-screen size at 200 km/h: %.0f%% of standstill (fov %.1f -> %.1f, distance %.2f -> %.2f)\n",
                100.0f * size(fast) / size(still), still.Output().fov_deg, fast.Output().fov_deg, still.Output().distance, fast.Output().distance);
    CHECK(size(fast) >= 0.85f * size(still));
    CHECK(still.Output().distance <= 5.2f); // closer than the old 6 m
    CHECK(still.Output().height <= 1.9f);   // and lower than the old 2.3 m
}
