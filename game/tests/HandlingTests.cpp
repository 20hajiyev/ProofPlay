// M1 handling and physics-stability tests (plan 2.6, 5.2) on real Jolt, no GPU.
#include "TestHarness.h"
#include "racer/CombatTuning.h"
#include "racer/AI.h"
#include "racer/Race.h"
#include "racer/SimClock.h"
#include "racer/Vehicle.h"
#include "racer/Performance.h"
#include "racer/Weather.h"
#include "racer/runtime/HandlingTrack.h"
#include "racer/runtime/PhysicsStepper.h"
#include "racer/runtime/PadRace.h"
#include "racer/runtime/RaceSession.h"
#include "racer/runtime/TrackBuilder.h"
#include "racer/Track.h"
#include "racer/Event.h"
#include "racer/runtime/VehicleRuntime.h"

#include "wiGraphicsDevice_DX12.h"
#include "wiJobSystem.h"
#include "wiPhysics.h"

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <memory>
#include <sstream>

using namespace racer;
using namespace racer::runtime;
using L = HandlingTrackLayout;

namespace
{
    constexpr float kDt = PhysicsStepper::kStepSeconds;

    // Scene::Update touches GPU buffers, so the render-loop test needs a (windowless) device.
    std::unique_ptr<wi::graphics::GraphicsDevice_DX12> g_device;

    static test::SetupRegistrar setup([] {
        wi::jobsystem::Initialize();
        g_device = std::make_unique<wi::graphics::GraphicsDevice_DX12>();
        wi::graphics::GetDevice() = g_device.get();
        InitializePhysicsRuntime();
    });

    const VehicleDefinition& D01()
    {
        static VehicleDefinition d = [] {
            std::ifstream f(RACER_CONTENT_DIR "/vehicles/D01.json", std::ios::binary);
            std::stringstream s;
            s << f.rdbuf();
            VehicleDefinition def;
            std::vector<ValidationIssue> issues;
            ParseVehicleDefinition(s.str(), def, issues);
            return def;
        }();
        return d;
    }

    VehicleCommand Throttle(float throttle, float steer = 0.0f, float brake = 0.0f)
    {
        VehicleCommand c;
        c.throttle = throttle;
        c.steer = steer;
        c.brake = brake;
        return c;
    }

    struct Session
    {
        wi::scene::Scene scene;
        PhysicsStepper stepper{ scene };
        std::vector<std::unique_ptr<VehicleRuntime>> cars;
        CommandBuffer commands;
        uint64_t tick = 0;
        int insane_steps = 0;
        float min_up = 1.0f;

        explicit Session(XMFLOAT3 spawn = { 0, 0, 0 }, int car_count = 1, float spacing = 8.0f, float yaw = 0.0f, const VehicleDefinition* def = nullptr)
        {
            BuildHandlingTrack(scene, false);
            for (int i = 0; i < car_count; ++i)
                cars.push_back(std::make_unique<VehicleRuntime>(scene, def ? *def : D01(), XMFLOAT3(spawn.x + (i % 4) * 4.0f, spawn.y, spawn.z + (i / 4) * spacing), yaw));
            stepper.Prepare();
            for (auto& c : cars)
                tuned &= c->ApplyTuning();
        }
        bool tuned = true;

        VehicleRuntime& Car(size_t i = 0) { return *cars[i]; }

        const VehicleTelemetry& Step(const VehicleCommand& command, const VehicleCommand* others = nullptr)
        {
            commands.Submit(command);
            cars[0]->PrePhysics(commands.ForStep(), kDt);
            for (size_t i = 1; i < cars.size(); ++i)
                cars[i]->PrePhysics(others ? *others : VehicleCommand{}, kDt);
            stepper.Step();
            ++tick;
            for (auto& c : cars)
            {
                const VehicleTelemetry& t = c->PostPhysics(tick);
                insane_steps += !IsTelemetrySane(t);
                min_up = std::min(min_up, t.up_y);
            }
            return cars[0]->Telemetry();
        }

        template <typename F>
        void Run(float seconds, F command)
        {
            const int steps = static_cast<int>(seconds / kDt + 0.5f);
            for (int i = 0; i < steps; ++i)
                Step(command(tick * kDt));
        }

        // Runs until pred(telemetry) holds; returns elapsed seconds or -1 on timeout.
        template <typename F, typename P>
        float RunUntil(float max_seconds, F command, P pred)
        {
            const uint64_t start = tick;
            while ((tick - start) * kDt < max_seconds)
                if (pred(Step(command(tick * kDt))))
                    return (tick - start) * kDt;
            return -1.0f;
        }

        void AccelerateTo(float kmh)
        {
            RunUntil(40.0f, [](float) { return Throttle(1); }, [kmh](const VehicleTelemetry& t) { return t.forward_speed_kmh >= kmh; });
        }
    };

    float Distance(const float a[3], const float b[3])
    {
        const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    float BrakingDistance(float lane_x, float from_kmh)
    {
        Session s({ lane_x, 0, 0 });
        s.AccelerateTo(from_kmh);
        const float start[3] = { s.Car().Telemetry().position[0], s.Car().Telemetry().position[1], s.Car().Telemetry().position[2] };
        s.RunUntil(20.0f, [](float) { return Throttle(0, 0, 1); }, [](const VehicleTelemetry& t) { return t.forward_speed_kmh < 1.0f; });
        return Distance(start, s.Car().Telemetry().position);
    }

    uint64_t PrivateBytes()
    {
        PROCESS_MEMORY_COUNTERS_EX c{};
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&c), sizeof(c));
        return c.PrivateUsage;
    }
}

TEST_CASE("physics: each Step() advances exactly one 1/120 s step")
{
    wi::scene::Scene scene;
    PhysicsStepper stepper(scene);
    wi::ecs::Entity e = wi::ecs::CreateEntity();
    auto& t = scene.transforms.Create(e);
    t.Translate(XMFLOAT3(0, 100, 0));
    t.UpdateTransform();
    auto& rb = scene.rigidbodies.Create(e);
    rb.mass = 1.0f;
    rb.damping_linear = 0.0f;
    rb.SetDisableDeactivation(true);
    stepper.Prepare();
    for (int i = 0; i < 120; ++i)
        stepper.Step();
    const float vy = wi::physics::GetVelocity(rb).y;
    const float g = scene.weather.gravity.y;
    std::printf("  INFO free fall after 120 steps: vy = %.4f m/s (gravity %.2f, two steps per call would give %.2f)\n", vy, g, 2 * g);
    CHECK(std::fabs(vy - g) < 0.01f);
}

TEST_CASE("handling: definition tuning reaches the Jolt controller")
{
    Session s;
    CHECK(s.tuned);
    float max_rpm = 0;
    s.Run(6.0f, [](float) { return Throttle(1); });
    s.RunUntil(4.0f, [](float) { return Throttle(1); }, [&max_rpm](const VehicleTelemetry& t) { max_rpm = std::max(max_rpm, t.rpm); return false; });
    std::printf("  INFO peak rpm %.0f (definition shift-up %.0f, Jolt default max 6000)\n", max_rpm, D01().shift_up_rpm);
    CHECK(max_rpm > 6100.0f);
}

TEST_CASE("render loop: Scene::Update between steps never pulls the car back")
{
    // Regression: with simulation disabled Wicked teleported bodies to their stale transforms
    // during Scene::Update, so the sandbox car reached 97 km/h while staying at its spawn point.
    Session s;
    for (int frame = 0; frame < 5 * 60; ++frame) // 5 s at 60 fps, two steps per frame
    {
        s.Step(Throttle(1));
        s.Step(Throttle(1));
        s.scene.Update(1.0f / 60.0f);
    }
    const VehicleTelemetry& t = s.Car().Telemetry();
    const XMFLOAT3 visual = s.scene.transforms.GetComponent(s.Car().Body())->GetPosition();
    std::printf("  INFO after 5 s with Scene::Update: z %.1f m at %.1f km/h, transform z %.1f m\n", t.position[2], t.forward_speed_kmh, visual.z);
    CHECK(t.position[2] > 30.0f);
    CHECK(std::fabs(visual.z - t.position[2]) < 0.5f);
}

TEST_CASE("visual wheels: placed at the corners, front wheels steer, all roll forward")
{
    Session s;
    wi::ecs::Entity w[4];
    for (auto& e : w)
    {
        e = wi::ecs::CreateEntity();
        s.scene.transforms.Create(e);
    }
    s.Car().SetWheelEntities(w[0], w[1], w[2], w[3]);
    auto pose = [&](int i) { return *s.scene.transforms.GetComponent(w[i]); };
    auto heading = [](const XMFLOAT4& q) { // yaw of the wheel's forward axis
        XMVECTOR f = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), XMLoadFloat4(&q));
        return std::atan2(XMVectorGetX(f), XMVectorGetZ(f));
    };

    s.Run(1.0f, [](float) { return Throttle(0, 1.0f); }); // full right lock, standing
    s.Car().UpdateWheelVisuals(0.0f);
    const char* names[4] = { "FL", "FR", "RL", "RR" };
    const float sx[4] = { -1, 1, -1, 1 }, sz[4] = { 1, 1, -1, -1 };
    for (int i = 0; i < 4; ++i)
    {
        const wi::scene::TransformComponent t = pose(i);
        std::printf("  INFO %s at (%.2f, %.2f, %.2f), steer %.1f deg\n", names[i], t.translation_local.x, t.translation_local.y, t.translation_local.z, XMConvertToDegrees(heading(t.rotation_local)));
        CHECK(t.translation_local.x * sx[i] > 0.5f && t.translation_local.z * sz[i] > 0.8f);
    }
    CHECK(XMConvertToDegrees(heading(pose(0).rotation_local)) > 20.0f);
    CHECK(XMConvertToDegrees(heading(pose(1).rotation_local)) > 20.0f);
    CHECK(std::fabs(heading(pose(2).rotation_local)) < 0.01f);

    s.AccelerateTo(20.0f);
    s.Car().UpdateWheelVisuals(0.0f);
    const XMFLOAT4 q0 = pose(2).rotation_local;
    s.Step(Throttle(0.2f));
    s.Car().UpdateWheelVisuals(0.0f);
    const XMFLOAT4 q1 = pose(2).rotation_local;
    XMFLOAT4 rel;
    XMStoreFloat4(&rel, XMQuaternionMultiply(XMQuaternionInverse(XMLoadFloat4(&q0)), XMLoadFloat4(&q1)));
    const float spin = 2.0f * std::atan2(rel.x, rel.w); // about the axle (+X)
    std::printf("  INFO rear-left spin per step at 20 km/h: %.3f rad (expected ~ +%.3f)\n", spin, 20 / 3.6f / D01().wheel_radius_m * kDt);
    CHECK(spin > 0.05f && spin < 0.25f); // positive about +X = top of the tyre moves forward
}

TEST_CASE("handling: parked car settles and stays still for 10 s")
{
    Session s;
    s.Run(2.0f, [](float) { return VehicleCommand{}; });
    const VehicleTelemetry settled = s.Car().Telemetry();
    s.Run(8.0f, [](float) { return VehicleCommand{}; });
    const VehicleTelemetry& end = s.Car().Telemetry();
    const float drift = Distance(settled.position, end.position);
    std::printf("  INFO rest height %.3f m, drift over 8 s %.4f m\n", end.position[1], drift);
    CHECK(drift < 0.02f);
    CHECK(end.wheel_contact[0] && end.wheel_contact[1] && end.wheel_contact[2] && end.wheel_contact[3]);
    CHECK(end.up_y > 0.99f);
    CHECK(s.insane_steps == 0);
}

TEST_CASE("handling: identical simulation at 30, 60 and 144 fps render rates")
{
    float reference[3] = {};
    for (double fps : { 30.0, 60.0, 144.0 })
    {
        Session s;
        SimClock clock;
        auto script = [](uint64_t tick) { return Throttle(1.0f, (tick >= 360 && tick < 600) ? 0.6f : 0.0f); };
        while (s.tick < 900)
        {
            const uint32_t steps = clock.Advance(1.0 / fps).steps;
            for (uint32_t i = 0; i < steps && s.tick < 900; ++i)
                s.Step(script(s.tick));
        }
        const float* p = s.Car().Telemetry().position;
        std::printf("  INFO %.0f fps -> tick 900 at (%.4f, %.4f, %.4f)\n", fps, p[0], p[1], p[2]);
        if (fps == 30.0)
            std::copy(p, p + 3, reference);
        else
            CHECK(Distance(reference, p) < 1e-4f);
    }
}

TEST_CASE("handling: 0-100 km/h and top speed are in the D-class band")
{
    Session s;
    int last_gear = 0;
    float next_mark = 20.0f;
    const float t100 = s.RunUntil(30.0f, [](float) { return Throttle(1); }, [&](const VehicleTelemetry& t) {
        if (t.gear != last_gear || t.forward_speed_kmh >= next_mark)
        {
            std::printf("  TRACE t=%5.2f s  %5.1f km/h  gear %d  rpm %4.0f  front slip %.2f/%.2f\n", s.tick * kDt, t.forward_speed_kmh, t.gear, t.rpm, t.wheel_longitudinal_slip[0], t.wheel_longitudinal_slip[1]);
            last_gear = t.gear;
            if (t.forward_speed_kmh >= next_mark)
                next_mark += 20.0f;
        }
        return t.forward_speed_kmh >= 100.0f;
    });
    float top = 0.0f;
    s.RunUntil(90.0f, [](float) { return Throttle(1); }, [&top](const VehicleTelemetry& t) {
        top = std::max(top, t.forward_speed_kmh);
        return t.position[2] > L::kStraightEndZ - 100.0f; // stay on the straight
    });
    const VehicleTelemetry& t = s.Car().Telemetry();
    std::printf("  INFO 0-100 km/h %.2f s; top speed %.1f km/h (target %.0f), gear %d, rpm %.0f\n", t100, top, D01().top_speed_target_kmh, t.gear, t.rpm);
    CHECK(t100 > 7.0f && t100 < 14.0f);
    CHECK(top > D01().top_speed_target_kmh * 0.9f && top < D01().top_speed_target_kmh * 1.1f);
    CHECK(s.insane_steps == 0);
}

TEST_CASE("performance (D-071): a full build is quicker to 100 and stops shorter on real physics")
{
    CarSetup build;
    for (const auto& [slot, level] : { std::pair{ "engine", "stage3" }, { "turbo", "race" }, { "intake", "cold_air" }, { "ecu", "tuned" },
                                       { "exhaust_sys", "straight" }, { "gearbox", "sequential" }, { "tyres", "semi_slick" },
                                       { "brakes", "race" }, { "suspension", "race" }, { "weight", "stripped" } })
        build.parts[std::string(kPerformancePrefix) + slot] = level;
    const VehicleDefinition tuned = ApplyPerformance(D01(), build);
    auto run = [](const VehicleDefinition* def, float& t100, float& stop_m) {
        Session s({ 0, 0, 0 }, 1, 8.0f, 0.0f, def);
        CHECK(s.tuned);
        t100 = s.RunUntil(30.0f, [](float) { return Throttle(1); }, [](const VehicleTelemetry& t) { return t.forward_speed_kmh >= 100.0f; });
        const float z0 = s.Car().Telemetry().position[2];
        s.RunUntil(15.0f, [](float) { return Throttle(0, 0, 1); }, [](const VehicleTelemetry& t) { return t.forward_speed_kmh < 1.0f; });
        stop_m = s.Car().Telemetry().position[2] - z0;
        CHECK(s.insane_steps == 0);
    };
    float t_stock, t_tuned, d_stock, d_tuned;
    run(nullptr, t_stock, d_stock);
    run(&tuned, t_tuned, d_tuned);
    std::printf("  INFO 0-100: stock %.2f s, full build %.2f s; 100-0: stock %.1f m, full build %.1f m; PI %d -> %d\n", t_stock, t_tuned, d_stock, d_tuned,
                PerformanceIndex(D01()), PerformanceIndex(tuned));
    CHECK(t_tuned < t_stock * 0.85f);
    CHECK(d_tuned < d_stock * 0.92f);
}

TEST_CASE("handling: 100-0 km/h braking is short and straight")
{
    Session s;
    s.AccelerateTo(100.0f);
    const float x0 = s.Car().Telemetry().position[0];
    const float start[3] = { x0, s.Car().Telemetry().position[1], s.Car().Telemetry().position[2] };
    const float time = s.RunUntil(20.0f, [](float) { return Throttle(0, 0, 1); }, [](const VehicleTelemetry& t) { return t.forward_speed_kmh < 1.0f; });
    const float dist = Distance(start, s.Car().Telemetry().position);
    const float lateral = std::fabs(s.Car().Telemetry().position[0] - x0);
    std::printf("  INFO 100-0: %.1f m in %.2f s, lateral drift %.2f m\n", dist, time, lateral);
    CHECK(dist > 30.0f && dist < 60.0f);
    CHECK(lateral < 1.5f);
}

TEST_CASE("handling: positive steer turns right (+X when facing +Z)")
{
    Session s;
    s.AccelerateTo(40.0f);
    const float x0 = s.Car().Telemetry().position[0];
    s.Run(2.0f, [](float) { return Throttle(0.3f, 1.0f); });
    const float dx = s.Car().Telemetry().position[0] - x0;
    std::printf("  INFO lateral displacement after 2 s full right: %.2f m\n", dx);
    CHECK(dx > 3.0f);
}

TEST_CASE("handling: sustained full-lock circle stays upright")
{
    Session s({ -60, 0, 400 });
    s.AccelerateTo(60.0f);
    s.Run(15.0f, [](float) { return Throttle(0.6f, 1.0f); });
    std::printf("  INFO circle: min chassis up.y %.3f, final speed %.1f km/h\n", s.min_up, s.Car().Telemetry().forward_speed_kmh);
    CHECK(s.min_up > 0.8f);
    CHECK(s.insane_steps == 0);
}

TEST_CASE("handling: ramp jump lands upright on four wheels")
{
    Session s({ L::kRampX, 0, L::kRampZ - 200.0f });
    int airborne_steps = 0;
    s.RunUntil(40.0f, [](float) { return Throttle(1); }, [&](const VehicleTelemetry& t) {
        airborne_steps += !(t.wheel_contact[0] || t.wheel_contact[1] || t.wheel_contact[2] || t.wheel_contact[3]);
        return t.position[2] > L::kRampZ + 120.0f;
    });
    s.Run(2.0f, [](float) { return Throttle(0.2f); });
    const VehicleTelemetry& t = s.Car().Telemetry();
    std::printf("  INFO ramp: airtime %.2f s, min up.y %.3f, speed after %.1f km/h\n", airborne_steps * kDt, s.min_up, t.forward_speed_kmh);
    CHECK(airborne_steps * kDt > 0.3f);
    CHECK(s.min_up > 0.6f);
    CHECK(t.wheel_contact[0] && t.wheel_contact[1] && t.wheel_contact[2] && t.wheel_contact[3]);
    CHECK(s.insane_steps == 0);
}

TEST_CASE("handling: 8 cm curbs at speed do not upset the car")
{
    Session s({ L::kCurbX, 0, L::kStraightStartZ + 10.0f });
    s.RunUntil(30.0f, [](float) { return Throttle(1); }, [](const VehicleTelemetry& t) { return t.position[2] > L::kCurbZ0 - 5.0f; });
    const float v_in = s.Car().Telemetry().forward_speed_kmh;
    s.RunUntil(10.0f, [](float) { return Throttle(0.5f); }, [](const VehicleTelemetry& t) { return t.position[2] > L::kCurbZ0 + 80.0f; });
    const float v_out = s.Car().Telemetry().forward_speed_kmh;
    std::printf("  INFO curbs: %.1f -> %.1f km/h, min up.y %.3f\n", v_in, v_out, s.min_up);
    CHECK(v_in > 70.0f);
    CHECK(v_out > v_in * 0.8f);
    CHECK(s.min_up > 0.9f);
}

TEST_CASE("handling: scraping the wall is never faster than open road")
{
    // A player-like lane controller steers toward target_x. Pressing into the wall uses a target
    // 2 m inside it, so the car keeps grinding along it; the control run holds a line on open road.
    auto run = [](float start_x, float target_x, int& contact_steps) {
        Session s({ start_x, 0, L::kWallZ0 + 20.0f });
        const VehicleTelemetry* last = &s.Car().Telemetry();
        float min_x_seen = start_x;
        s.RunUntil(40.0f, [&](float) {
            const float error = target_x - last->position[0];
            const float steer = std::clamp(0.08f * error - 0.25f * last->velocity[0], -0.3f, 0.3f);
            return Throttle(1, steer);
        }, [&](const VehicleTelemetry& t) {
            contact_steps += t.position[0] < L::kWallX + 0.4f + 1.1f; // wall half-thickness + chassis half width + margin
            min_x_seen = std::min(min_x_seen, t.position[0]);
            return t.position[2] > L::kWallZ0 + 520.0f;
        });
        std::printf("  TRACE start x %.1f target %.1f: closest x %.2f, final x %.2f\n", start_x, target_x, min_x_seen, s.Car().Telemetry().position[0]);
        return std::make_pair(s.Car().Telemetry().forward_speed_kmh, s.min_up);
    };
    int open_contact = 0, wall_contact = 0;
    auto [open_kmh, open_up] = run(-100.0f, -100.0f, open_contact);
    auto [wall_kmh, wall_up] = run(L::kWallX + 2.5f, L::kWallX - 2.0f, wall_contact);
    std::printf("  INFO speed at +500 m: open %.1f km/h, wall scrape %.1f km/h (%.1f s near wall, min up.y %.3f)\n", open_kmh, wall_kmh, wall_contact * kDt, wall_up);
    CHECK(wall_contact * kDt > 3.0f); // the scenario actually scraped
    CHECK(wall_kmh <= open_kmh + 0.5f);
    CHECK(wall_up > 0.8f);
    (void)open_up;
}

TEST_CASE("handling: lower-grip surfaces lengthen braking")
{
    const float asphalt = BrakingDistance(0.0f, 80.0f);
    const float wet = BrakingDistance(L::kWetLaneX, 80.0f);
    const float gravel = BrakingDistance(L::kGravelLaneX, 80.0f);
    std::printf("  INFO 80-0 braking: asphalt %.1f m, wet %.1f m, gravel %.1f m\n", asphalt, wet, gravel);
    CHECK(wet > asphalt * 1.05f);
    CHECK(gravel > asphalt * 1.2f);
}

TEST_CASE("handling: rear-end contact between two cars stays stable")
{
    Session s({ 0, 0, 0 }, 1);
    s.cars.push_back(std::make_unique<VehicleRuntime>(s.scene, D01(), XMFLOAT3(0, 0, 40), 0.0f));
    s.stepper.Prepare();
    CHECK(s.cars[1]->ApplyTuning());
    s.Run(1.0f, [](float) { return VehicleCommand{}; });
    const float front_z0 = s.Car(1).Telemetry().position[2];
    s.Run(8.0f, [](float) { return Throttle(1); });
    const float pushed = s.Car(1).Telemetry().position[2] - front_z0;
    std::printf("  INFO rear-end: parked car pushed %.2f m, min up.y %.3f\n", pushed, s.min_up);
    CHECK(pushed > 1.0f);
    CHECK(s.min_up > 0.8f);
    CHECK(s.insane_steps == 0);
}

TEST_CASE("assist: tumbling launch is levelled before landing")
{
    auto run = [](bool assist) {
        Session s({ -60, 0, 600 });
        s.Car().SetStabilityEnabled(assist);
        s.Run(1.0f, [](float) { return VehicleCommand{}; });
        auto& rb = *s.scene.rigidbodies.GetComponent(s.Car().Body());
        wi::physics::SetPosition(rb, XMFLOAT3(-60, 8, 600));
        wi::physics::SetLinearVelocity(rb, XMFLOAT3(0, 4, 15));
        wi::physics::SetAngularVelocity(rb, XMFLOAT3(1.2f, 0, 0.8f)); // pitch + roll tumble
        s.min_up = 1.0f;
        s.RunUntil(5.0f, [](float) { return VehicleCommand{}; }, [](const VehicleTelemetry& t) { return t.AnyWheelContact(); });
        const float landing_up = s.Car().Telemetry().up_y;
        s.Run(2.0f, [](float) { return VehicleCommand{}; });
        return std::make_pair(landing_up, s.Car().Telemetry().up_y);
    };
    auto [off_land, off_end] = run(false);
    auto [on_land, on_end] = run(true);
    std::printf("  INFO tumble landing up.y: assist off %.3f (settled %.3f), on %.3f (settled %.3f)\n", off_land, off_end, on_land, on_end);
    CHECK(on_land > off_land);
    CHECK(on_end > 0.95f);
}

TEST_CASE("assist: handbrake turn rotates more, then recovers to grip")
{
    auto run = [](bool handbrake, float& yaw_change, float& recovered_slip, float& total_yaw) {
        Session s({ -60, 0, 400 });
        s.AccelerateTo(70.0f);
        const float h0 = s.Car().Telemetry().heading_rad;
        s.Run(0.8f, [handbrake](float) { VehicleCommand c = Throttle(0.3f, 0.7f); c.handbrake = handbrake ? 1.0f : 0.0f; return c; });
        yaw_change = std::fabs(s.Car().Telemetry().heading_rad - h0);
        for (int i = 0; i < 4; ++i)
        {
            s.Run(0.5f, [](float) { return Throttle(0.5f); });
            const VehicleTelemetry& t = s.Car().Telemetry();
            std::printf("  TRACE %s +%.1f s after release: heading %.0f deg, yaw rate %.2f, speed %.0f km/h\n", handbrake ? "drift" : "grip ", (i + 1) * 0.5f,
                XMConvertToDegrees(t.heading_rad - h0), t.yaw_rate, t.forward_speed_kmh);
        }
        const VehicleTelemetry& t = s.Car().Telemetry();
        recovered_slip = std::fabs(t.wheel_lateral_slip_rad[2]) + std::fabs(t.wheel_lateral_slip_rad[3]);
        total_yaw = std::fabs(t.heading_rad - h0);
        return s.min_up;
    };
    float grip_yaw, grip_slip, grip_total, drift_yaw, drift_slip, drift_total;
    run(false, grip_yaw, grip_slip, grip_total);
    const float up = run(true, drift_yaw, drift_slip, drift_total);
    std::printf("  INFO 0.8 s turn at 70 km/h: yaw %.1f deg grip vs %.1f deg handbrake; 2 s later rear slip %.3f rad, total %.0f deg\n",
        XMConvertToDegrees(grip_yaw), XMConvertToDegrees(drift_yaw), drift_slip, XMConvertToDegrees(drift_total));
    CHECK(drift_yaw > grip_yaw * 1.15f);
    CHECK(drift_slip < 0.1f);                                       // back to grip
    CHECK(drift_total - drift_yaw < XMConvertToRadians(60.0f));     // soft return, no spin-out
    CHECK(up > 0.8f);
}

TEST_CASE("assist: lifting off mid-corner at speed does not spin the car")
{
    Session s({ 0, 0, 0 });
    s.AccelerateTo(130.0f);
    const float h0 = s.Car().Telemetry().heading_rad;
    float max_yaw_rate = 0;
    s.Run(1.0f, [](float) { return Throttle(1, 0.6f); });
    s.RunUntil(3.0f, [](float) { return Throttle(0, 0.6f, 0.4f); }, [&](const VehicleTelemetry& t) { max_yaw_rate = std::max(max_yaw_rate, std::fabs(t.yaw_rate)); return false; });
    const float turned = std::fabs(s.Car().Telemetry().heading_rad - h0);
    std::printf("  INFO lift-off at 130 km/h: max yaw rate %.2f rad/s, heading change %.0f deg, min up.y %.3f\n", max_yaw_rate, XMConvertToDegrees(turned), s.min_up);
    CHECK(max_yaw_rate < 2.0f);
    CHECK(s.min_up > 0.8f);
}

TEST_CASE("gearbox: powering through a corner does not sit on the limiter")
{
    // The autotest situation that exposed it: brake down to 1st, then power out of a turn.
    auto run = [](bool assist, int& max_gear) {
        Session s({ -60, 0, 400 });
        s.Car().SetLimiterShiftEnabled(assist);
        s.AccelerateTo(60.0f);
        s.RunUntil(10.0f, [](float) { return Throttle(0, 0, 1); }, [](const VehicleTelemetry& t) { return t.forward_speed_kmh < 25.0f; });
        int pinned = 0;
        max_gear = 0;
        s.RunUntil(6.0f, [](float) { return Throttle(0.8f, -0.4f); }, [&](const VehicleTelemetry& t) {
            pinned += t.rpm >= D01().max_rpm - 50.0f && t.gear < int(D01().gear_ratios.size());
            max_gear = std::max(max_gear, t.gear);
            return false;
        });
        return pinned * kDt;
    };
    int gear_off = 0, gear_on = 0;
    const float off = run(false, gear_off);
    const float on = run(true, gear_on);
    std::printf("  INFO powered corner, 6 s: on the limiter %.2f s without assist (top gear %d), %.2f s with (top gear %d)\n", off, gear_off, on, gear_on);
    CHECK(off > 1.0f); // the scenario really reproduces the problem
    CHECK(on < 0.8f);
}

namespace
{
    // Rounded-rectangle route on open asphalt (away from ramp, curbs and wall), heading +Z at s = 0.
    TrackRoute PadRoute()
    {
        const float x0 = -30, z0 = 900, w = 100, h = 600, r = 30;
        std::vector<Vec2> pts;
        auto arc = [&](float cx, float cz, float a0) {
            for (int i = 0; i <= 10; ++i)
            {
                const float a = a0 - i * (XM_PIDIV2 / 10);
                pts.push_back({ x0 + cx + r * std::cos(a), z0 + cz + r * std::sin(a) });
            }
        };
        pts.push_back({ x0, z0 + 150 });
        arc(r, h - r, XM_PI);
        arc(w - r, h - r, XM_PIDIV2);
        arc(w - r, r, 0.0f);
        arc(r, r, -XM_PIDIV2);
        return TrackRoute(pts, 7.0f);
    }

    struct AIRace
    {
        TrackRoute route = PadRoute();
        std::unique_ptr<Session> session;
        std::unique_ptr<RaceRules> rules;
        std::vector<std::unique_ptr<AIDriver>> drivers;
        float max_abs_lateral = 0;
        std::vector<float> gaps; // 2-car races: distance between the cars along the route, per step
        size_t rival_ahead = 0;  // steps with car 1 (the rival when car 0 stands in for the player) ahead
        VehicleCommand player_drive;
        CombatCommand player_combat;
        std::vector<double> step_ms;

        AIRace(int cars, int laps, AIDifficulty difficulty)
        {
            RaceConfig cfg;
            cfg.laps = laps;
            cfg.checkpoints_s = { 0, route.Length() * 0.25f, route.Length() * 0.5f, route.Length() * 0.75f };
            rules = std::make_unique<RaceRules>(route, cfg);
            // Grid: two columns, 8 m apart, behind the line on the straight.
            session = std::make_unique<Session>(XMFLOAT3(-30 - 2.5f, 0, 900 + 140), 0);
            for (int i = 0; i < cars; ++i)
            {
                const XMFLOAT3 spot(-30 + (i % 2 ? 2.5f : -2.5f), 0, 900 + 140 - (i / 2) * 9.0f - (i % 2) * 4.5f);
                session->cars.push_back(std::make_unique<VehicleRuntime>(session->scene, D01(), spot, 0.0f));
                rules->AddParticipant({ spot.x, spot.z });
                AIParams p = MakeAIParams(difficulty, uint32_t(i));
                p.lane_offset_m = (i % 2 ? 2.5f : -2.5f);
                drivers.push_back(std::make_unique<AIDriver>(route, D01(), p));
            }
            session->stepper.Prepare();
            for (auto& c : session->cars)
                session->tuned &= c->ApplyTuning();
            rules->BeginGrid();
            while (rules->Phase() != RacePhase::Racing)
                rules->Tick(kDt);
        }

        void Run(float max_seconds)
        {
            Session& s = *session;
            for (uint64_t tick = 1; tick < uint64_t(max_seconds / kDt) && rules->Phase() == RacePhase::Racing; ++tick)
            {
                const auto t0 = std::chrono::steady_clock::now();
                for (size_t i = 0; i < s.cars.size(); ++i)
                {
                    const VehicleTelemetry& t = s.cars[i]->Telemetry();
                    const ParticipantState& p = rules->Participant(int(i));
                    AIObservation o{ { t.position[0], t.position[2] }, t.heading_rad, t.forward_speed_kmh / 3.6f, p.s };
                    s.cars[i]->PrePhysics(drivers[i]->Drive(o, kDt), kDt);
                }
                s.stepper.Step();
                for (size_t i = 0; i < s.cars.size(); ++i)
                {
                    const VehicleTelemetry& t = s.cars[i]->PostPhysics(tick);
                    s.insane_steps += !IsTelemetrySane(t);
                    s.min_up = std::min(s.min_up, t.up_y);
                    rules->Update(int(i), { t.position[0], t.position[2] }, tick);
                    if (rules->Participant(int(i)).started)
                        max_abs_lateral = std::max(max_abs_lateral, std::fabs(route.Project({ t.position[0], t.position[2] }, rules->Participant(int(i)).s).lateral));
                }
                rules->Rank();
                step_ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
                s.tick = tick;
            }
        }

        int Finished() const
        {
            int n = 0;
            for (auto& p : rules->Participants())
                n += p.finished;
            return n;
        }
    };
}

TEST_CASE("ai physics: one AI car laps the pad route on real physics")
{
    AIRace race(1, 2, AIDifficulty::Normal);
    CHECK(race.session->tuned);
    race.Run(240.0f);
    std::printf("  INFO 1 AI car, 2 laps of %.0f m: finished %d in %.1f s, max |lateral| %.2f m, min up.y %.3f\n",
        race.route.Length(), race.Finished(), race.session->tick * kDt, race.max_abs_lateral, race.session->min_up);
    CHECK(race.Finished() == 1);
    CHECK(race.max_abs_lateral < 7.0f);
    CHECK(race.session->insane_steps == 0);
}

TEST_CASE("ai physics: 20-car race stays stable and within the physics budget")
{
    AIRace race(20, 2, AIDifficulty::Normal);
    race.Run(300.0f);
    std::vector<double> ms = race.step_ms;
    std::sort(ms.begin(), ms.end());
    double mean = 0;
    for (double v : ms)
        mean += v / ms.size();
    std::printf("  INFO 20 AI cars: %d/20 finished in %.1f s, min up.y %.3f, step (physics+AI+rules) mean %.3f ms, p99 %.3f ms\n",
        race.Finished(), race.session->tick * kDt, race.session->min_up, mean, ms[ms.size() * 99 / 100]);
    CHECK(race.session->insane_steps == 0);
    CHECK(mean * 2 <= 4.0); // plan 2.17: physics for 20 cars <= 4 ms per 60 fps frame
    CHECK(race.Finished() >= 16);
}

namespace
{
    RaceSetup CombatRaceSetup(int cars, int laps) { return MakePadRaceSetup(std::vector<VehicleDefinition>(size_t(cars), D01()), laps); }
}

TEST_CASE("combat race: 20 AI cars, all eight abilities, on real physics")
{
    wi::scene::Scene scene;
    BuildHandlingTrack(scene, false);
    RaceSession race(scene, CombatRaceSetup(20, 2));
    CHECK(race.Ready());
    race.Start();
    std::vector<double> ms;
    for (int i = 0; i < 120 * 300 && race.Rules().Phase() != RacePhase::Finished; ++i)
    {
        const auto t0 = std::chrono::steady_clock::now();
        race.Step({}, {}, kDt);
        ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
    int finished = 0;
    for (auto& p : race.Rules().Participants())
        finished += p.finished;
    std::sort(ms.begin(), ms.end());
    double mean = 0;
    for (double v : ms)
        mean += v / ms.size();
    const auto& st = race.GetStats();
    std::printf("  INFO combat race: %d/20 finished in %.1f s; pickups %u, uses %u, hits %u, blocks %u, wrecks %u, recoveries %u\n",
        finished, race.Tick() * kDt, st.pickups, st.uses, st.hits, st.blocks, st.wrecks, st.recoveries);
    std::printf("  INFO step (physics+AI+combat+rules) mean %.3f ms, p99 %.3f ms -> %.2f ms per 60 fps frame\n", mean, ms[ms.size() * 99 / 100], mean * 2);
    std::printf("  INFO recoveries: fell %u, upside-down %u, stuck %u; uses:", st.recover_fell, st.recover_upside, st.recover_stuck);
    for (int a = 1; a < int(Ability::Count); ++a)
        std::printf(" %s %u", ToString(Ability(a)), st.uses_by_ability[a]);
    std::printf("\n");
    CHECK(st.insane_steps == 0);
    CHECK(finished >= 16);
    CHECK(st.uses > 40 && st.hits > 20);
    CHECK(mean * 2 <= 4.0);
}

namespace
{
    TrackDefinition LoadTrackFile(const char* file)
    {
        std::ifstream f(std::string(RACER_CONTENT_DIR "/tracks/") + file, std::ios::binary);
        std::stringstream s;
        s << f.rdbuf();
        TrackDefinition t;
        std::vector<ValidationIssue> issues;
        if (!ParseTrackDefinition(s.str(), t, issues) || !ValidateTrackLayout(t, issues))
            for (auto& i : issues)
                std::printf("  ISSUE %s %s: %s\n", file, i.path.c_str(), i.message.c_str());
        return t;
    }
    // Per-ability use counts for balance work (design/balance/combat-tuning-proposal.md).
    void PrintUsesByAbility(const char* tag, const RaceSession& race)
    {
        std::printf("  INFO %s uses by ability:", tag);
        for (int a = 1; a < int(Ability::Count); ++a)
            std::printf(" %s %u", ToString(Ability(a)), race.GetStats().uses_by_ability[size_t(a)]);
        std::printf("  blocks %u\n", race.GetStats().blocks);
    }
    const CombatTuning& GameTuning()
    {
        static const CombatTuning t = [] {
            std::ifstream f(RACER_CONTENT_DIR "/combat/tuning.json", std::ios::binary);
            std::stringstream s;
            s << f.rdbuf();
            CombatTuning out;
            std::vector<ValidationIssue> issues;
            if (!ParseCombatTuning(s.str(), out, issues))
                std::printf("  ISSUE combat/tuning.json invalid\n");
            return out;
        }();
        return t;
    }
    const TrackDefinition& S01()
    {
        static const TrackDefinition d = LoadTrackFile("S01_dock_loop.json");
        return d;
    }
    const TrackDefinition& L01()
    {
        static const TrackDefinition d = LoadTrackFile("L01_container_run.json");
        return d;
    }

    VehicleDefinition LoadCar(const std::string& id);

    // Region R01 races the class D roster: D01-D05 in grid order, repeating (as RacePath builds it).
    std::vector<VehicleDefinition> Roster(int cars)
    {
        static const std::vector<VehicleDefinition> all = { LoadCar("D01"), LoadCar("D02"), LoadCar("D03"), LoadCar("D04"), LoadCar("D05") };
        std::vector<VehicleDefinition> out;
        for (int i = 0; i < cars; ++i)
            out.push_back(all[size_t(i) % all.size()]);
        return out;
    }

    struct TrackRace
    {
        wi::scene::Scene scene;
        TrackPhysicsStats physics;
        std::unique_ptr<RaceSession> race;
        std::vector<double> ms;
        float max_abs_lateral = 0;
        std::vector<float> gaps; // 2-car races: distance between the cars along the route, per step
        size_t rival_ahead = 0;  // steps with car 1 (the rival when car 0 stands in for the player) ahead
        VehicleCommand player_drive;
        CombatCommand player_combat;

        TrackRace(int cars, int laps, const TrackDefinition& track = S01(), EventMode mode = EventMode::CombatRace, float time_limit_s = 0.0f, int player = -1, std::vector<Ability> abilities = {}, std::vector<AIDifficulty> skills = {}, int pickup_rotation = 0, WeatherState weather = {})
        {
            physics = BuildTrackPhysics(scene, track);
            std::vector<VehicleDefinition> roster = Roster(cars);
            if (mode == EventMode::RivalDuel)
                roster.assign(roster.size(), roster[0]); // mirror match, as in the game
            RaceSetup setup = MakeTrackRaceSetup(track, roster, laps);
            setup.tuning = GameTuning(); // the shipped combat numbers, not code defaults
            setup.mode = mode;
            setup.time_limit_s = time_limit_s;
            setup.player_index = player;
            setup.car_difficulty = skills;
            setup.weather = weather;
            if (!abilities.empty())
            {
                std::vector<Ability> authored;
                for (auto& pk : setup.pickups)
                    authored.push_back(pk.ability);
                const auto filtered = FilterPickupAbilities(authored, abilities);
                for (size_t k = 0; k < setup.pickups.size(); ++k)
                    setup.pickups[k].ability = filtered[k];
            }
            // Variation for behavioural gates: rotate which ability each pad hands out (same pads,
            // same abilities overall), so one chaotic combat sequence cannot decide a gate.
            if (pickup_rotation > 0 && !setup.pickups.empty())
            {
                std::vector<Ability> a;
                for (auto& pk : setup.pickups)
                    a.push_back(pk.ability);
                for (size_t k = 0; k < setup.pickups.size(); ++k)
                    setup.pickups[k].ability = a[(k + size_t(pickup_rotation)) % a.size()];
            }
            race = std::make_unique<RaceSession>(scene, setup);
            race->Start();
        }
        void Run(float max_seconds)
        {
            const TrackRoute& route = race->Route();
            for (int i = 0; i < int(max_seconds / kDt) && race->Rules().Phase() != RacePhase::Finished && !race->TimeUp(); ++i)
            {
                const auto t0 = std::chrono::steady_clock::now();
                race->Step(player_drive, player_combat, kDt);
                ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
                if (race->CarCount() == 2 && race->Rules().Phase() == RacePhase::Racing)
                {
                    const float lead1 = race->Rules().TotalProgress(race->Rules().Participant(1)) - race->Rules().TotalProgress(race->Rules().Participant(0));
                    gaps.push_back(std::fabs(lead1));
                    rival_ahead += lead1 > 0.0f;
                }
                // Diagnostics: RACER_TRACE_S=a,b prints both cars every 0.25 s while car 1 is inside [a, b].
                if (const char* tr = std::getenv("RACER_TRACE_S"); tr && race->CarCount() == 2 && i % 30 == 0)
                {
                    float a = 0, b = 0;
                    std::sscanf(tr, "%f,%f", &a, &b);
                    for (size_t c = 0; c < 2; ++c)
                    {
                        const float s = race->Rules().Participant(int(c)).s;
                        const VehicleTelemetry& t = race->Car(c).Telemetry();
                        if (s >= a && s <= b)
                            std::printf("  TRACE t=%6.2f car%zu s=%6.1f kmh=%5.1f y=%5.2f vy=%5.2f contact=%d yaw=%5.2f\n", i * kDt, c, s, t.forward_speed_kmh,
                                        t.position[1], t.velocity[1], int(t.AnyWheelContact()), t.yaw_rate);
                    }
                }
                for (size_t c = 0; c < race->CarCount(); ++c)
                {
                    const VehicleTelemetry& t = race->Car(c).Telemetry();
                    if (race->Rules().Participant(int(c)).started)
                        max_abs_lateral = std::max(max_abs_lateral, std::fabs(route.Project({ t.position[0], t.position[2] }, race->Rules().Participant(int(c)).s).lateral));
                }
            }
        }
        int Finished() const
        {
            int n = 0;
            for (auto& p : race->Rules().Participants())
                n += p.finished;
            return n;
        }
        double Mean() const
        {
            double m = 0;
            for (double v : ms)
                m += v / ms.size();
            return m;
        }
    };
}

TEST_CASE("S01 Dock Loop: one AI car laps the real layout inside the walls")
{
    TrackRace t(1, 2);
    CHECK(t.race->Ready());
    t.Run(240.0f);
    const auto& st = t.race->GetStats();
    const float lap_s = t.race->Rules().Participant(0).finish_tick * kDt / 2.0f;
    std::printf("  INFO S01 physics: %zu wall boxes; 1 AI car finished %d, avg lap %.1f s (%.0f km/h avg), max |lateral| %.2f m (wall face at %.1f m), recoveries %u\n",
        t.physics.wall_boxes, t.Finished(), lap_s, 2200.0f / lap_s * 3.6f, t.max_abs_lateral, S01().half_width_m + S01().wall_offset_m, st.recoveries);
    CHECK(t.Finished() == 1);
    CHECK(t.max_abs_lateral < S01().half_width_m); // never left the road
    CHECK(st.recoveries == 0 && st.insane_steps == 0);
}

namespace
{
    VehicleDefinition LoadCar(const std::string& id)
    {
        std::ifstream f(std::string(RACER_CONTENT_DIR) + "/vehicles/" + id + ".json", std::ios::binary);
        std::stringstream s;
        s << f.rdbuf();
        VehicleDefinition d;
        std::vector<ValidationIssue> issues;
        if (!ParseVehicleDefinition(s.str(), d, issues))
            for (auto& i : issues)
                std::printf("  ISSUE %s %s: %s\n", id.c_str(), i.path.c_str(), i.message.c_str());
        return d;
    }

    struct CarProfile
    {
        std::string id;
        float t100 = -1, top = 0, brake100 = 0, skidpad_g = 0, handbrake_deg = 0, s01_lap = 0;
        bool tuned = false;
    };

    // One single-car session on the tuning ground with an arbitrary definition.
    struct Solo
    {
        wi::scene::Scene scene;
        PhysicsStepper stepper{ scene };
        std::unique_ptr<VehicleRuntime> car;
        bool tuned = false;
        float min_up = 1;
        Solo(const VehicleDefinition& d, XMFLOAT3 at, float yaw = 0)
        {
            BuildHandlingTrack(scene, false);
            car = std::make_unique<VehicleRuntime>(scene, d, at, yaw);
            stepper.Prepare();
            tuned = car->ApplyTuning();
        }
        const VehicleTelemetry& Step(const VehicleCommand& c)
        {
            car->PrePhysics(c, kDt);
            stepper.Step();
            const VehicleTelemetry& t = car->PostPhysics(0);
            min_up = std::min(min_up, t.up_y);
            return t;
        }
        template <typename P> float Until(float max_s, const VehicleCommand& c, P pred)
        {
            for (int i = 0; i < int(max_s / kDt); ++i)
                if (pred(Step(c)))
                    return i * kDt;
            return -1;
        }
    };

    CarProfile Measure(const std::string& id)
    {
        CarProfile p;
        p.id = id;
        const VehicleDefinition d = LoadCar(id);
        {
            Solo s(d, { 0, 0, 0 });
            p.tuned = s.tuned;
            p.t100 = s.Until(30, Throttle(1), [](const VehicleTelemetry& t) { return t.forward_speed_kmh >= 100; });
            s.Until(90, Throttle(1), [&](const VehicleTelemetry& t) { p.top = std::max(p.top, t.forward_speed_kmh); return t.position[2] > L::kStraightEndZ - 100; });
        }
        {
            Solo s(d, { 0, 0, 0 });
            s.Until(30, Throttle(1), [](const VehicleTelemetry& t) { return t.forward_speed_kmh >= 100; });
            const float z0 = s.car->Telemetry().position[2];
            s.Until(20, Throttle(0, 0, 1), [](const VehicleTelemetry& t) { return t.forward_speed_kmh < 1; });
            p.brake100 = s.car->Telemetry().position[2] - z0;
        }
        {
            // Skidpad: fixed steering, throttle rising slowly; best 1 s average of v * yaw rate.
            Solo s(d, { -60, 0, 1200 });
            std::vector<float> lat;
            for (int i = 0; i < 120 * 25; ++i)
            {
                VehicleCommand c = Throttle(std::min(1.0f, 0.15f + i * kDt * 0.03f), 0.45f);
                const VehicleTelemetry& t = s.Step(c);
                lat.push_back(std::fabs(t.forward_speed_kmh / 3.6f * t.yaw_rate));
            }
            for (size_t i = 120; i < lat.size(); ++i)
            {
                float avg = 0;
                for (size_t k = i - 120; k < i; ++k)
                    avg += lat[k] / 120.0f;
                p.skidpad_g = std::max(p.skidpad_g, avg / 9.81f);
            }
        }
        {
            Solo s(d, { -60, 0, 400 });
            s.Until(30, Throttle(1), [](const VehicleTelemetry& t) { return t.forward_speed_kmh >= 70; });
            const float h0 = s.car->Telemetry().heading_rad;
            VehicleCommand c = Throttle(0.3f, 0.7f);
            c.handbrake = 1;
            for (int i = 0; i < 96; ++i)
                s.Step(c);
            p.handbrake_deg = XMConvertToDegrees(std::fabs(s.car->Telemetry().heading_rad - h0));
        }
        {
            wi::scene::Scene scene;
            BuildTrackPhysics(scene, S01());
            RaceSession race(scene, MakeTrackRaceSetup(S01(), { d }, 1));
            race.Start();
            for (int i = 0; i < 120 * 120 && race.Rules().Phase() != RacePhase::Finished; ++i)
                race.Step({}, {}, kDt);
            p.s01_lap = race.Rules().Participant(0).finished ? race.Rules().Participant(0).finish_tick * kDt : -1;
        }
        return p;
    }
}

TEST_CASE("cars: D01-D05 each have a measurable role, none dominates")
{
    std::vector<CarProfile> cars;
    for (const char* id : { "D01", "D02", "D03", "D04", "D05" })
        cars.push_back(Measure(id));
    std::printf("  INFO car   0-100   top    100-0   skidpad  handbrake  S01 lap (AI, incl. start)\n");
    for (const CarProfile& c : cars)
        std::printf("  INFO %s  %5.2f s %5.1f  %5.1f m  %4.2f g   %5.1f deg   %5.1f s\n", c.id.c_str(), c.t100, c.top, c.brake100, c.skidpad_g, c.handbrake_deg, c.s01_lap);
    const CarProfile &grip = cars[0], &drift = cars[1], &balanced = cars[2], &heavy = cars[3], &sprint = cars[4];
    for (const CarProfile& c : cars)
    {
        CHECK(c.tuned);
        CHECK(c.t100 > 7.0f && c.t100 < 13.0f);
        CHECK(c.top > 170 * 0.9f && c.top < 170 * 1.1f);
        CHECK(c.s01_lap > 0);
    }
    // Roles (plan 2.7): drift rotates most on the handbrake; heavy stops longest; grip corners hardest.
    CHECK(drift.handbrake_deg > grip.handbrake_deg && drift.handbrake_deg > balanced.handbrake_deg && drift.handbrake_deg > heavy.handbrake_deg);
    CHECK(heavy.brake100 > grip.brake100 && heavy.brake100 > drift.brake100);
    CHECK(grip.skidpad_g >= drift.skidpad_g);
    // sprint (D-081, the modern AWD turbo hatch): the quickest launch to 100 km/h
    CHECK(sprint.t100 < grip.t100 && sprint.t100 < drift.t100 && sprint.t100 < balanced.t100 && sprint.t100 < heavy.t100);
    // Balance: the class stays within 6% on the region's short track.
    float best = 1e9f, worst = 0;
    for (const CarProfile& c : cars)
    {
        best = std::min(best, c.s01_lap);
        worst = std::max(worst, c.s01_lap);
    }
    std::printf("  INFO S01 lap spread %.1f%%\n", (worst / best - 1) * 100);
    CHECK(worst / best < 1.06f);
}

TEST_CASE("weather: rain and storm stop longer and lap slower, and the AI still drives the wet track (D-083)")
{
    // Plan gate: 100-0 longer in the wet; the AI must stay on a wet road (it plans with the same grip).
    for (const char* id : { "D02", "D05" })
    {
        float dist[3] = {};
        const WeatherKind kinds[3] = { WeatherKind::Clear, WeatherKind::Rain, WeatherKind::Storm };
        for (int w = 0; w < 3; ++w)
        {
            Solo s(ApplyWeather(LoadCar(id), EffectsOf({ kinds[w], 1.0f })), { 0, 0, 0 });
            s.Until(30, Throttle(1), [](const VehicleTelemetry& t) { return t.forward_speed_kmh >= 100; });
            const float z0 = s.car->Telemetry().position[2];
            s.Until(20, Throttle(0, 0, 1), [](const VehicleTelemetry& t) { return t.forward_speed_kmh < 1; });
            dist[w] = s.car->Telemetry().position[2] - z0;
        }
        std::printf("  INFO %s 100-0: dry %.1f m, rain %.1f m (+%.0f%%), storm %.1f m (+%.0f%%)\n", id, dist[0], dist[1], (dist[1] / dist[0] - 1) * 100,
                    dist[2], (dist[2] / dist[0] - 1) * 100);
        CHECK(dist[1] > dist[0] * 1.08f);
        CHECK(dist[2] > dist[1]);
    }
    float lap[3] = {};
    unsigned insane = 0;
    int finished[3] = {};
    const WeatherKind kinds[3] = { WeatherKind::Clear, WeatherKind::Rain, WeatherKind::Storm };
    for (int w = 0; w < 3; ++w)
    {
        TrackRace t(12, 1, S01(), EventMode::CombatRace, 0.0f, -1, {}, {}, 0, { kinds[w], 1.0f });
        t.Run(200.0f);
        finished[w] = t.Finished();
        insane += t.race->GetStats().insane_steps;
        lap[w] = t.race->RaceTime(0);
        std::printf("  INFO S01 12 cars, 1 lap, %s: %d/12 finished, car0 %.1f s, recoveries %u\n", ToString(kinds[w]), t.Finished(), lap[w], t.race->GetStats().recoveries);
    }
    CHECK(insane == 0);
    for (int w = 0; w < 3; ++w)
        CHECK(finished[w] >= 11); // the wet field still gets round
    // Pace: solo Hard-AI laps without pickups, the median of three cars per weather (in the field,
    // combat hits decide car 0's time - measured: storm 60.7 s "faster" than clear 70.6 s; and one
    // car's single lap can hold a spin - measured: a clear D02 lap of 72.3 s against 62.0).
    for (int w = 0; w < 3; ++w)
    {
        std::vector<float> laps;
        for (const char* id : { "D01", "D02", "D03" })
        {
            wi::scene::Scene scene;
            BuildTrackPhysics(scene, S01());
            RaceSetup setup = MakeTrackRaceSetup(S01(), { LoadCar(id) }, 1);
            setup.pickups.clear();
            setup.difficulty = AIDifficulty::Hard;
            setup.weather = { kinds[w], 1.0f };
            RaceSession race(scene, setup);
            race.Start();
            for (int i = 0; i < 120 * 200 && race.Rules().Phase() != RacePhase::Finished; ++i)
                race.Step({}, {}, kDt);
            laps.push_back(race.Rules().Participant(0).finished ? race.Rules().Participant(0).finish_tick * kDt - 3.0f : 1e9f);
        }
        std::sort(laps.begin(), laps.end());
        lap[w] = laps[1];
    }
    std::printf("  INFO solo Hard-AI S01 lap, median of D01-D03: clear %.1f s, rain %.1f s (+%.1f%%), storm %.1f s (+%.1f%%)\n", lap[0], lap[1], (lap[1] / lap[0] - 1) * 100,
                lap[2], (lap[2] / lap[0] - 1) * 100);
    // On S01 the lap is set by the straights: rain's grip moves it inside the noise (measured -0.5%
    // to +1.4% over runs), so only the storm's ordering is gated; grip itself is gated on the skidpad.
    CHECK(lap[2] > lap[0]);
    CHECK(lap[2] < lap[0] * 1.25f); // slower, not crawling
    float g[3] = {};
    for (int w = 0; w < 3; ++w)
    {
        // Skidpad as in Measure(): fixed steering, throttle rising slowly; best 1 s average lateral g.
        Solo sp(ApplyWeather(LoadCar("D02"), EffectsOf({ kinds[w], 1.0f })), { -60, 0, 1200 });
        std::vector<float> lat;
        for (int i = 0; i < 120 * 25; ++i)
        {
            const VehicleTelemetry& t = sp.Step(Throttle(std::min(1.0f, 0.15f + i * kDt * 0.03f), 0.45f));
            lat.push_back(std::fabs(t.forward_speed_kmh / 3.6f * t.yaw_rate));
        }
        for (size_t i = 120; i < lat.size(); ++i)
        {
            float avg = 0;
            for (size_t k = i - 120; k < i; ++k)
                avg += lat[k] / 120.0f;
            g[w] = std::max(g[w], avg / 9.81f);
        }
    }
    std::printf("  INFO D02 skidpad: clear %.2f g, rain %.2f g, storm %.2f g\n", g[0], g[1], g[2]);
    CHECK(g[1] < g[0] * 0.95f && g[2] < g[1]);
}

TEST_CASE("E03 reference time: stays within 3% of a measured Hard-AI run")
{
    // Plan 2.12 wants a validated reference driver time T without mods. Until playtest data
    // exists, T is a measured Hard-AI solo run (D01, no pickups); this keeps R01.json honest.
    std::ifstream f(RACER_CONTENT_DIR "/events/R01.json", std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    RegionEvents events;
    std::vector<ValidationIssue> issues;
    CHECK(ParseRegionEvents(s.str(), events, issues));
    const EventDefinition* e = events.Find("R01_E03");
    CHECK(e != nullptr);
    wi::scene::Scene scene;
    BuildTrackPhysics(scene, S01());
    RaceSetup setup = MakeTrackRaceSetup(S01(), { D01() }, e->laps);
    setup.pickups.clear();
    setup.difficulty = AIDifficulty::Hard;
    RaceSession race(scene, setup);
    race.Start();
    for (int i = 0; i < 120 * 400 && race.Rules().Phase() != RacePhase::Finished; ++i)
        race.Step({}, {}, kDt);
    // Race time from the green light, not including the countdown.
    const float measured = race.Rules().Participant(0).finish_tick * kDt - 3.0f;
    std::printf("  INFO E03 measured Hard-AI 2-lap time %.1f s, reference in R01.json %.1f s\n", measured, e->reference_time_s);
    CHECK(race.Rules().Participant(0).finished);
    CHECK(std::fabs(measured / e->reference_time_s - 1.0f) < 0.03f);
}

TEST_CASE("determinism: the same 20-car S01 race twice gives identical state")
{
    // Plan 2.16: replays within one build rely on the simulation being repeatable.
    auto run = [] {
        TrackRace t(20, 3);
        t.Run(25.0f);
        std::vector<float> state;
        for (size_t c = 0; c < t.race->CarCount(); ++c)
        {
            const VehicleTelemetry& v = t.race->Car(c).Telemetry();
            state.insert(state.end(), { v.position[0], v.position[1], v.position[2], v.velocity[0], v.velocity[2] });
            state.push_back(t.race->Combat().Get(int(c)).health);
        }
        return state;
    };
    const std::vector<float> a = run(), b = run();
    size_t differing = 0;
    float worst = 0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        differing += a[i] != b[i];
        worst = std::max(worst, std::fabs(a[i] - b[i]));
    }
    std::printf("  INFO determinism after 25 s: %zu/%zu values differ, worst difference %.4f\n", differing, a.size(), worst);
    CHECK(differing == 0);
}

TEST_CASE("S01 Dock Loop: R01_E05 - 20 cars, 3 laps, full combat")
{
    TrackRace t(20, 3);
    t.race->SetDebugLogRecoveries(std::getenv("RACER_LOG_RECOVERIES") != nullptr);
    t.Run(600.0f);
    std::vector<double> ms = t.ms;
    std::sort(ms.begin(), ms.end());
    const auto& st = t.race->GetStats();
    std::printf("  INFO E05: %d/20 finished in %.1f s; pickups %u uses %u hits %u blocks %u wrecks %u; recoveries fell %u upside %u stuck %u\n",
        t.Finished(), t.race->Tick() * kDt, st.pickups, st.uses, st.hits, st.blocks, st.wrecks, st.recover_fell, st.recover_upside, st.recover_stuck);
    PrintUsesByAbility("E05", *t.race);
    std::printf("  INFO E05 step mean %.3f ms, p99 %.3f ms -> %.2f ms per 60 fps frame (budget 4.0)\n", t.Mean(), ms[ms.size() * 99 / 100], t.Mean() * 2);
    CHECK(st.insane_steps == 0);
    CHECK(t.Finished() >= 18);
    CHECK(t.Mean() * 2 <= 4.0);
}

TEST_CASE("L01 Container Run: one AI car laps the real layout inside the walls")
{
    TrackRace t(1, 1, L01());
    CHECK(t.race->Ready());
    t.Run(300.0f);
    const auto& st = t.race->GetStats();
    const float lap_s = t.race->Rules().Participant(0).finish_tick * kDt;
    std::printf("  INFO L01 physics: %zu wall boxes; 1 AI car finished %d, lap %.1f s incl. start (%.0f km/h avg), max |lateral| %.2f m (wall face at %.1f m), recoveries %u\n",
        t.physics.wall_boxes, t.Finished(), lap_s, 3600.0f / lap_s * 3.6f, t.max_abs_lateral, L01().half_width_m + L01().wall_offset_m, st.recoveries);
    CHECK(t.Finished() == 1);
    CHECK(t.max_abs_lateral < L01().half_width_m);
    CHECK(st.recoveries == 0 && st.insane_steps == 0);
}

TEST_CASE("E04 reference time: stays within 3% of a measured Hard-AI run on L01")
{
    // Same rule as E03 (plan 2.12): T is a measured Hard-AI solo run until playtest data exists.
    std::ifstream f(RACER_CONTENT_DIR "/events/R01.json", std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    RegionEvents events;
    std::vector<ValidationIssue> issues;
    CHECK(ParseRegionEvents(s.str(), events, issues));
    const EventDefinition* e = events.Find("R01_E04");
    CHECK(e != nullptr);
    wi::scene::Scene scene;
    BuildTrackPhysics(scene, L01());
    RaceSetup setup = MakeTrackRaceSetup(L01(), { D01() }, e->laps);
    setup.pickups.clear();
    setup.difficulty = AIDifficulty::Hard;
    RaceSession race(scene, setup);
    race.Start();
    for (int i = 0; i < 120 * 400 && race.Rules().Phase() != RacePhase::Finished; ++i)
        race.Step({}, {}, kDt);
    const float measured = race.Rules().Participant(0).finish_tick * kDt - 3.0f;
    std::printf("  INFO E04 measured Hard-AI 1-lap time %.1f s, reference in R01.json %.1f s\n", measured, e->reference_time_s);
    CHECK(race.Rules().Participant(0).finished);
    CHECK(e->reference_time_s > 0 && std::fabs(measured / e->reference_time_s - 1.0f) < 0.03f);
}

TEST_CASE("L01 Container Run: R01_E07 - 20 cars, 3 laps, full combat")
{
    TrackRace t(20, 3, L01());
    t.Run(900.0f);
    std::vector<double> ms = t.ms;
    std::sort(ms.begin(), ms.end());
    const auto& st = t.race->GetStats();
    std::printf("  INFO E07: %d/20 finished in %.1f s; pickups %u uses %u hits %u wrecks %u; recoveries fell %u upside %u stuck %u\n",
        t.Finished(), t.race->Tick() * kDt, st.pickups, st.uses, st.hits, st.wrecks, st.recover_fell, st.recover_upside, st.recover_stuck);
    PrintUsesByAbility("E07", *t.race);
    std::printf("  INFO E07 step mean %.3f ms, p99 %.3f ms -> %.2f ms per 60 fps frame (budget 4.0)\n", t.Mean(), ms[ms.size() * 99 / 100], t.Mean() * 2);
    CHECK(st.insane_steps == 0);
    CHECK(t.Finished() >= 18);
    CHECK(t.Mean() * 2 <= 4.0);
}
TEST_CASE("modes: Destruction (E06) - 8 cars, clock ends it at 90 s, targets never attack")
{
    // All-AI stand-in for the player: with player_index -1 the last grid slot is the hunter (AI driver and
    // combat planner); the other seven are targets without planners. Five runs that differ only in which
    // pad hands out which ability, as for E08 (D-059): one run is one chaotic combat sequence, and the
    // five-car roster (D-081) moved the hunter slot from D04 to D03, which swung a single run 10 -> 6 hits.
    int hunter_hits = 0, best_run = 0;
    for (int variant = 0; variant < 5; ++variant)
    {
        TrackRace t(8, 99, S01(), EventMode::Destruction, 90.0f, -1, { Ability::Lance, Ability::Needle, Ability::Pulse }, {}, variant * 5); // E06 rules
        t.Run(200.0f);
        const size_t H = t.race->CarCount() - 1; // hunter: last grid slot
        const RaceSummary s0 = t.race->Summary(H);
        int target_uses = 0;
        for (size_t i = 0; i + 1 < t.race->CarCount(); ++i)
            target_uses += t.race->Summary(i).uses + t.race->Summary(i).hits_dealt;
        std::printf("  INFO E06 run %d: time up %d after %.1f s; hunter %s hits %d uses %d wrecks caused %d, rank %d; targets' uses+hits %d\n", variant,
            int(t.race->TimeUp()), t.race->Tick() * kDt, t.race->Car(H).Definition().id.c_str(), s0.hits_dealt, s0.uses, t.race->WrecksCaused(H), s0.position, target_uses);
        CHECK(t.race->TimeUp());
        CHECK(std::fabs(t.race->Tick() * kDt - 93.0f) < 0.1f); // 3 s countdown + 90 s
        CHECK(s0.finished);                                      // Destruction finishes on the clock
        CHECK(target_uses == 0);                                 // targets are prey, not fighters
        CHECK(t.race->GetStats().insane_steps == 0);
        hunter_hits += s0.hits_dealt;
        best_run = std::max(best_run, s0.hits_dealt);
    }
    std::printf("  INFO E06 hunter hits over 5 runs: %d, best run %d (objective 5)\n", hunter_hits, best_run);
    // The objective is 5 hits (D-088): the old 7 was reached only while the targets' cruising pack
    // sank into each other and one Pulse hit several at once; with car-to-car bumpers 5 runs gave
    // 5/3/5/3/5 hits.
    CHECK(best_run >= 5);          // an AI hunter reaches the objective
    CHECK(hunter_hits >= 5 * 4);   // and keeps landing hits across pickup layouts
}

TEST_CASE("modes: Rival Duel (E08) - the Hard rival keeps the fight close to a slower player")
{
    // Car 0 stands in for the player at Normal skill (a human is rarely Hard-AI fast); car 1 is the
    // Hard Aggressor rival. Without pacing the rival led by a median 68 m+ and landed 2 hits.
    // Three runs that differ only in which pad hands out which ability (D-059): a single run is
    // one chaotic combat sequence - one hit into a hairpin spin decided it. Same thresholds, on the
    // three runs together: 2+ hits per race, 2 of 3 finishes within 10 s, half the time within 50 m.
    std::vector<float> g;
    size_t rival_ahead = 0;
    unsigned hits = 0;
    int close_finishes = 0;
    for (int variant = 0; variant < 3; ++variant)
    {
        TrackRace t(2, 2, L01(), EventMode::RivalDuel, 0.0f, -1, {}, { AIDifficulty::Normal, AIDifficulty::Hard }, variant * 5);
        t.Run(400.0f);
        std::printf("  INFO E08 run %d: %d/2 finished; uses %u hits %u; recoveries %u; finish car0 %.1f s, car1 %.1f s\n", variant, t.Finished(),
                    t.race->GetStats().uses, t.race->GetStats().hits, t.race->GetStats().recoveries, t.race->RaceTime(0), t.race->RaceTime(1));
        CHECK(t.Finished() == 2);
        CHECK(t.race->GetStats().insane_steps == 0);
        hits += t.race->GetStats().hits;
        close_finishes += std::fabs(t.race->RaceTime(0) - t.race->RaceTime(1)) < 10.0f;
        g.insert(g.end(), t.gaps.begin(), t.gaps.end());
        rival_ahead += t.rival_ahead;
    }
    std::sort(g.begin(), g.end());
    size_t close = 0;
    for (float v : g)
        close += v < 50.0f;
    std::printf("  INFO E08 all runs: hits %u, close finishes %d/3, gap median %.0f m, p90 %.0f m, within 50 m %.0f%%, rival ahead %.0f%%\n", hits, close_finishes,
                g.empty() ? 0.0f : g[g.size() / 2], g.empty() ? 0.0f : g[g.size() * 9 / 10], g.empty() ? 0.0 : 100.0 * double(close) / double(g.size()),
                g.empty() ? 0.0 : 100.0 * double(rival_ahead) / double(g.size()));
    // 2+ hits per race: the same three runs on the flat L01 (before D-059) land 8 hits in total - the
    // earlier single run's 5 hits was one lucky sequence, never a stable rate (D-059).
    CHECK(hits >= 6);
    CHECK(close_finishes >= 2);
    CHECK(g.size() > 0 && double(close) / double(g.size()) >= 0.5); // the fight stays within Lance/Needle range
}
TEST_CASE("performance: 20 cars, physics step cost within the 4 ms/frame budget")
{
    Session s({ -40, 0, 0 }, 20, 10.0f);
    const VehicleCommand all = Throttle(1);
    s.Run(1.0f, [](float) { return VehicleCommand{}; });
    std::vector<double> ms;
    for (int i = 0; i < 600; ++i)
    {
        const auto t0 = std::chrono::steady_clock::now();
        s.Step(all, &all);
        ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
    std::sort(ms.begin(), ms.end());
    double mean = 0;
    for (double v : ms)
        mean += v / ms.size();
    const double p99 = ms[ms.size() * 99 / 100];
    // At 60 fps a render frame runs 2 physics steps.
    std::printf("  INFO 20 cars: step mean %.3f ms, p99 %.3f ms -> %.2f ms per 60 fps frame (budget 4.0)\n", mean, p99, mean * 2);
    CHECK(mean * 2 <= 4.0);
    CHECK(s.insane_steps == 0);
}

TEST_CASE("memory: S01 race sessions (12 cars, combat) created and destroyed repeatedly")
{
    auto cycle = [] {
        TrackRace t(12, 3);
        t.Run(5.0f);
    };
    cycle();
    cycle();
    std::vector<double> mib;
    for (int i = 0; i < 10; ++i)
    {
        cycle();
        mib.push_back(PrivateBytes() / (1024.0 * 1024.0));
    }
    std::printf("  INFO S01 session cycles RAM MiB:");
    for (double m : mib)
        std::printf(" %.1f", m);
    std::printf("  (growth %.2f MiB over 9 cycles)\n", mib.back() - mib.front());
    CHECK(mib.back() - mib.front() < 4.0);
}

TEST_CASE("memory: creating and destroying race sessions does not leak")
{
    auto cycle = [] {
        Session s({ 0, 0, 0 }, 4);
        s.Run(1.0f, [](float) { return Throttle(1); });
    };
    for (int i = 0; i < 3; ++i)
        cycle();
    const uint64_t before = PrivateBytes();
    for (int i = 0; i < 30; ++i)
        cycle();
    const double growth = (double(PrivateBytes()) - double(before)) / (1024.0 * 1024.0);
    std::printf("  INFO 30 sessions x 4 cars: RAM growth %.2f MiB (limit 4)\n", growth);
    CHECK(growth < 4.0);
}

TEST_CASE("bumpers: in a 20-car pack cars do not sink into each other, and the race still runs (D-088)")
{
    float overlap[2] = {};
    unsigned deep[2] = {};
    int finished[2] = {};
    for (int on = 0; on < 2; ++on)
    {
        wi::scene::Scene scene;
        BuildTrackPhysics(scene, S01());
        RaceSetup setup = MakeTrackRaceSetup(S01(), Roster(20), 1);
        setup.tuning = GameTuning();
        setup.disable_bumpers = on == 0;
        RaceSession race(scene, setup);
        race.Start();
        for (int i = 0; i < 120 * 200 && race.Rules().Phase() != RacePhase::Finished; ++i)
            race.Step({}, {}, kDt);
        overlap[on] = race.GetStats().max_car_overlap_m;
        deep[on] = race.GetStats().deep_overlap_steps;
        for (size_t c = 0; c < race.CarCount(); ++c)
            finished[on] += race.Rules().Participant(int(c)).finished;
        CHECK(race.GetStats().insane_steps == 0);
        std::printf("  INFO 20-car S01 lap, bumpers %s: deepest car-into-car overlap %.2f m, %u contacts, %u pair-steps sunk > 0.3 m, %d/20 finished\n", on ? "on" : "off",
                    overlap[on], race.GetStats().bumper_contacts, deep[on], finished[on]);
    }
    CHECK(deep[1] * 10 < deep[0]); // sinking deep into another car becomes rare
    CHECK(finished[1] >= 18);
}
