#include "TestHarness.h"
#include "racer/AI.h"

#include <cmath>

using namespace racer;

namespace
{
    // Rounded rectangle, anticlockwise from above, starting at (0,100) heading +Z.
    TrackRoute RoundedRect(float w = 160, float h = 400, float r = 25)
    {
        std::vector<Vec2> pts;
        auto arc = [&](float cx, float cz, float a0) {
            for (int i = 0; i <= 8; ++i)
            {
                const float a = a0 - i * (3.14159265f / 2) / 8;
                pts.push_back({ cx + r * std::cos(a), cz + r * std::sin(a) });
            }
        };
        pts.push_back({ 0, 100 }); // start/finish on the left straight, heading +Z
        arc(r, h - r, 3.14159265f);            // top-left, turning right toward +X
        arc(w - r, h - r, 3.14159265f / 2);    // top-right
        arc(w - r, r, 0.0f);                   // bottom-right
        arc(r, r, -3.14159265f / 2);           // bottom-left
        return TrackRoute(pts, 7.0f);
    }

    VehicleDefinition TestCar()
    {
        VehicleDefinition d;
        d.wheelbase_m = 2.53f;
        d.top_speed_target_kmh = 170;
        d.steering = { 32, 9, 150, 4, 6 };
        return d;
    }

    // Kinematic bicycle model standing in for the physics car.
    struct KinematicCar
    {
        Vec2 p{ 0, 85 };
        float heading = 0, v = 0, steer_state = 0;
        DriveAssist assist{ TestCar().steering };
        void Step(const VehicleCommand& c, float dt)
        {
            const DriverInput in = assist.Apply(c, v * 3.6f, dt);
            const float delta = in.steer * 32.0f * 3.14159265f / 180.0f;
            v = std::max(0.0f, v + (in.forward * 4.0f - in.brake * 8.0f) * dt);
            heading += v * std::tan(delta) / 2.53f * dt;
            p.x += std::sin(heading) * v * dt;
            p.z += std::cos(heading) * v * dt;
        }
    };

    struct LapResult { bool finished; float time_s; float max_abs_lateral; float mean_lateral; };

    LapResult RunLaps(const AIParams& params, int laps)
    {
        TrackRoute route = RoundedRect();
        RaceConfig cfg;
        cfg.laps = laps;
        cfg.checkpoints_s = { 0, route.Length() * 0.25f, route.Length() * 0.5f, route.Length() * 0.75f };
        RaceRules rules(route, cfg);
        KinematicCar car;
        const int id = rules.AddParticipant(car.p);
        rules.BeginGrid();
        rules.Tick(0);
        while (rules.Phase() != RacePhase::Racing)
            rules.Tick(1.0f / 120);
        AIDriver ai(route, TestCar(), params);
        const float dt = 1.0f / 120.0f;
        LapResult r{ false, 0, 0, 0 };
        int n = 0;
        for (uint64_t tick = 1; tick < 120 * 600 && !rules.Participant(id).finished; ++tick)
        {
            AIObservation o{ car.p, car.heading, car.v, rules.Participant(id).s };
            car.Step(ai.Drive(o, dt), dt);
            rules.Update(id, car.p, tick);
            rules.Rank();
            const float lat = route.Project(car.p, rules.Participant(id).s).lateral;
            if (rules.Participant(id).started)
            {
                r.max_abs_lateral = std::max(r.max_abs_lateral, std::fabs(lat));
                r.mean_lateral += lat;
                ++n;
            }
            r.time_s = tick * dt;
        }
        r.finished = rules.Participant(id).finished;
        r.mean_lateral /= std::max(1, n);
        return r;
    }
}

TEST_CASE("ai: speed profile is fast on straights and sqrt(a*R) in corners")
{
    TrackRoute route = RoundedRect(160, 400, 25);
    SpeedProfile prof(route, 8.0f, 7.0f, 3.5f, 170 / 3.6f);
    const float corner = std::sqrt(8.0f * 25.0f);
    std::printf("  INFO profile: corner min %.1f m/s (sqrt(aR) = %.1f), mid-straight %.1f m/s\n", prof.Min(), corner, prof.At(200));
    CHECK(prof.Min() > corner * 0.8f && prof.Min() < corner * 1.2f);
    CHECK(prof.At(200) > prof.Min() * 1.8f);
    // Approaching the first corner (around s = 375) the target must already be falling.
    CHECK(prof.At(340) < prof.At(250));
}

TEST_CASE("ai: completes three laps inside the road edges")
{
    LapResult r = RunLaps(MakeAIParams(AIDifficulty::Normal, 1), 3);
    std::printf("  INFO normal AI: finished %d in %.1f s, max |lateral| %.2f m (half width 7)\n", r.finished, r.time_s, r.max_abs_lateral);
    CHECK(r.finished);
    CHECK(r.max_abs_lateral < 7.0f);
}

TEST_CASE("ai: holds a requested lane offset")
{
    AIParams p = MakeAIParams(AIDifficulty::Normal, 2);
    p.lane_offset_m = 3.0f;
    LapResult r = RunLaps(p, 1);
    std::printf("  INFO lane +3 m: mean lateral %.2f m, max %.2f m\n", r.mean_lateral, r.max_abs_lateral);
    CHECK(r.finished);
    CHECK(r.mean_lateral > 2.0f && r.mean_lateral < 4.0f);
}

TEST_CASE("ai: harder difficulty laps faster with the same car")
{
    const LapResult easy = RunLaps(MakeAIParams(AIDifficulty::Easy, 3), 2);
    const LapResult hard = RunLaps(MakeAIParams(AIDifficulty::Hard, 3), 2);
    std::printf("  INFO 2 laps: easy %.1f s, hard %.1f s\n", easy.time_s, hard.time_s);
    CHECK(easy.finished && hard.finished);
    CHECK(hard.time_s < easy.time_s);
}

TEST_CASE("ai: pinned against something, it reverses with mirrored steering, then drives on")
{
    TrackRoute route = RoundedRect();
    AIDriver ai(route, TestCar(), MakeAIParams(AIDifficulty::Normal, 5));
    // Standing still, pointing 60 degrees right of the route: the driver wants to steer left.
    AIObservation o{ { 0, 150 }, 1.05f, 0.0f, route.Project({ 0, 150 }).s };
    const float dt = 1.0f / 120.0f;
    VehicleCommand c;
    int steps = 0;
    for (; steps < 240 && !ai.Reversing(); ++steps)
        c = ai.Drive(o, dt);
    CHECK(ai.Reversing());
    CHECK(steps >= 119 && steps <= 121); // after 1 s without moving
    c = ai.Drive(o, dt);
    CHECK(c.brake == 1.0f && c.throttle == 0.0f); // brake at standstill = reverse
    CHECK(c.steer > 0.0f);                        // mirrored: wanted left, reverses steering right
    for (int i = 0; i < 150; ++i)
        c = ai.Drive(o, dt);
    CHECK(!ai.Reversing());
    CHECK(c.throttle > 0.0f);
}

TEST_CASE("ai: difficulty parameters stay inside the plan's reaction windows")
{
    for (uint32_t seed = 0; seed < 200; ++seed)
    {
        const float e = MakeAIParams(AIDifficulty::Easy, seed).reaction_s;
        const float n = MakeAIParams(AIDifficulty::Normal, seed).reaction_s;
        const float h = MakeAIParams(AIDifficulty::Hard, seed).reaction_s;
        CHECK(e >= 0.45f && e <= 0.75f);
        CHECK(n >= 0.30f && n <= 0.50f);
        CHECK(h >= 0.18f && h <= 0.35f);
    }
}

TEST_CASE("ai: pace scale lowers the target speed and is clamped (duel rival, D-044)")
{
    TrackRoute route = RoundedRect();
    AIDriver ai(route, TestCar(), MakeAIParams(AIDifficulty::Hard, 1));
    AIObservation o{ { 0, 200 }, 0, 20, route.Project({ 0, 200 }).s };
    ai.Drive(o, 1.0f / 120);
    const float full = ai.TargetSpeed();
    ai.SetPaceScale(0.85f);
    ai.Drive(o, 1.0f / 120);
    CHECK(std::fabs(ai.TargetSpeed() - full * 0.85f) < 0.01f);
    ai.SetPaceScale(0.2f); // never crawls: an obvious "waiting" rival breaks the fight
    ai.Drive(o, 1.0f / 120);
    CHECK(std::fabs(ai.TargetSpeed() - full * 0.75f) < 0.01f);
    ai.SetPaceScale(1.5f); // never exceeds the car's own profile
    ai.Drive(o, 1.0f / 120);
    CHECK(std::fabs(ai.TargetSpeed() - full) < 0.01f);
}