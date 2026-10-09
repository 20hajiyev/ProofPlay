#include "TestHarness.h"
#include "racer/Vehicle.h"

#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>

using namespace racer;

namespace
{
    std::string ReadFile(const char* path)
    {
        std::ifstream f(path, std::ios::binary);
        std::stringstream s;
        s << f.rdbuf();
        return s.str();
    }

    std::string D01() { return ReadFile(RACER_CONTENT_DIR "/vehicles/D01.json"); }

    bool HasIssue(const std::vector<ValidationIssue>& issues, const std::string& path)
    {
        for (auto& i : issues)
            if (i.path == path)
                return true;
        return false;
    }

    std::string Replace(std::string text, const std::string& from, const std::string& to)
    {
        const size_t at = text.find(from);
        if (at != std::string::npos)
            text.replace(at, from.size(), to);
        return text;
    }
}

TEST_CASE("vehicle: shipped D01 definition is valid")
{
    VehicleDefinition d;
    std::vector<ValidationIssue> issues;
    CHECK(ParseVehicleDefinition(D01(), d, issues));
    for (auto& i : issues)
        std::printf("  ISSUE %s: %s\n", i.path.c_str(), i.message.c_str());
    CHECK(d.id == "D01");
    CHECK(d.drivetrain == Drivetrain::FWD);
    CHECK(d.gear_ratios.size() == 5);
    CHECK(d.torque_curve.size() == 5);
}

TEST_CASE("vehicle: unknown field is rejected")
{
    VehicleDefinition d;
    std::vector<ValidationIssue> issues;
    CHECK(!ParseVehicleDefinition(Replace(D01(), "\"health\"", "\"turbo_boost\": 3, \"health\""), d, issues));
    CHECK(HasIssue(issues, "$.turbo_boost"));
}

TEST_CASE("vehicle: missing field and wrong unit range are rejected")
{
    VehicleDefinition d;
    std::vector<ValidationIssue> issues;
    CHECK(!ParseVehicleDefinition(Replace(Replace(D01(), "\"mass_kg\": 1150,", ""), "\"wheel_radius_m\": 0.36", "\"wheel_radius_m\": 36"), d, issues));
    CHECK(HasIssue(issues, "$.mass_kg"));
    CHECK(HasIssue(issues, "$.wheel_radius_m"));
}

TEST_CASE("vehicle: unknown enum value is rejected")
{
    VehicleDefinition d;
    std::vector<ValidationIssue> issues;
    CHECK(!ParseVehicleDefinition(Replace(D01(), "\"FWD\"", "\"4x4\""), d, issues));
    CHECK(HasIssue(issues, "$.drivetrain"));
}

TEST_CASE("vehicle: gear ratios must strictly decrease")
{
    VehicleDefinition d;
    std::vector<ValidationIssue> issues;
    CHECK(!ParseVehicleDefinition(Replace(D01(), "[3.1, 2.05, 1.5, 1.25, 1.1]", "[3.1, 3.2, 1.5, 1.25, 1.1]"), d, issues));
    CHECK(HasIssue(issues, "$.gear_ratios[1]"));
}

TEST_CASE("vehicle: malformed JSON is rejected")
{
    VehicleDefinition d;
    std::vector<ValidationIssue> issues;
    CHECK(!ParseVehicleDefinition("{ \"id\": ", d, issues));
    CHECK(HasIssue(issues, "$"));
}

TEST_CASE("command buffer: a press is delivered to exactly one of 8 catch-up steps")
{
    CommandBuffer buffer;
    VehicleCommand c;
    c.throttle = 1.0f;
    c.events = EVENT_RESET;
    buffer.Submit(c);
    int resets = 0;
    for (int i = 0; i < 8; ++i)
    {
        VehicleCommand step = buffer.ForStep();
        CHECK(step.throttle == 1.0f);
        resets += (step.events & EVENT_RESET) != 0;
    }
    CHECK(resets == 1);
}

TEST_CASE("command buffer: two presses in one frame are not lost")
{
    CommandBuffer buffer;
    VehicleCommand c;
    c.events = EVENT_CAMERA;
    buffer.Submit(c);
    c.events = EVENT_NONE;
    buffer.Submit(c); // released before the step ran
    CHECK((buffer.ForStep().events & EVENT_CAMERA) != 0);
}

static SteeringDef TestSteering()
{
    SteeringDef s;
    s.max_angle_deg = 32;
    s.high_speed_angle_deg = 8;
    s.high_speed_kmh = 160;
    s.rate_per_s = 4;
    s.return_rate_per_s = 6;
    return s;
}

TEST_CASE("assist: steering lock shrinks with speed")
{
    DriveAssist a(TestSteering());
    CHECK(std::fabs(a.SteerLimit(0) - 1.0f) < 1e-6f);
    CHECK(std::fabs(a.SteerLimit(160) - 0.25f) < 1e-6f);
    CHECK(std::fabs(a.SteerLimit(400) - 0.25f) < 1e-6f);
    CHECK(a.SteerLimit(80) < 1.0f && a.SteerLimit(80) > 0.25f);
}

TEST_CASE("assist: full steer input ramps in at rate_per_s")
{
    DriveAssist a(TestSteering());
    VehicleCommand c;
    c.steer = 1.0f;
    const float dt = 1.0f / 120.0f;
    DriverInput in = a.Apply(c, 0.0f, dt);
    CHECK(std::fabs(in.steer - 4.0f * dt) < 1e-5f);
    for (int i = 0; i < 120; ++i)
        in = a.Apply(c, 0.0f, dt);
    CHECK(std::fabs(in.steer - 1.0f) < 1e-5f);
}

TEST_CASE("assist: brake held at standstill engages reverse, not while rolling forward")
{
    DriveAssist a(TestSteering());
    VehicleCommand c;
    c.brake = 1.0f;
    DriverInput rolling = a.Apply(c, 40.0f, 1.0f / 120.0f);
    CHECK(rolling.forward == 0.0f && rolling.brake == 1.0f);
    DriverInput stopped = a.Apply(c, 0.5f, 1.0f / 120.0f);
    CHECK(stopped.forward == -1.0f && stopped.brake == 0.0f);
}

TEST_CASE("gearbox: sustained limiter under throttle asks for exactly one upshift")
{
    LimiterShiftAssist a;
    const float dt = 1.0f / 120.0f;
    int shifts = 0, steps = 0;
    for (; steps < 120; ++steps)
        shifts += a.Update(1, 5, 7000, 6600, 1.0f, dt);
    CHECK(shifts == 3); // every 0.3 s while still pinned (the caller changes gear in reality)
    LimiterShiftAssist b;
    int first = -1;
    for (int i = 0; i < 120 && first < 0; ++i)
        if (b.Update(1, 5, 7000, 6600, 1.0f, dt))
            first = i;
    CHECK(first >= 35 && first <= 36);
}

TEST_CASE("gearbox: no forced upshift in top gear, reverse, below the limit or off throttle")
{
    const float dt = 1.0f / 120.0f;
    auto any = [&](int gear, float rpm, float throttle) {
        LimiterShiftAssist a;
        bool shifted = false;
        for (int i = 0; i < 240; ++i)
            shifted |= a.Update(gear, 5, rpm, 6600, throttle, dt);
        return shifted;
    };
    CHECK(!any(5, 7000, 1.0f));
    CHECK(!any(-1, 7000, 1.0f));
    CHECK(!any(2, 6000, 1.0f));
    CHECK(!any(2, 7000, 0.2f));
}

TEST_CASE("telemetry: NaN and runaway speed are flagged")
{
    VehicleTelemetry t;
    CHECK(IsTelemetrySane(t));
    t.velocity[2] = std::numeric_limits<float>::quiet_NaN();
    CHECK(!IsTelemetrySane(t));
    t.velocity[2] = 200.0f;
    CHECK(!IsTelemetrySane(t));
}
