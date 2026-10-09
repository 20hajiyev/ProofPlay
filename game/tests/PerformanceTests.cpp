#include "TestHarness.h"
#include "racer/Performance.h"

#include <cmath>
#include <fstream>
#include <sstream>

using namespace racer;

namespace
{
    VehicleDefinition D01()
    {
        std::ifstream f(RACER_CONTENT_DIR "/vehicles/D01.json", std::ios::binary);
        std::stringstream s;
        s << f.rdbuf();
        VehicleDefinition d;
        std::vector<ValidationIssue> issues;
        ParseVehicleDefinition(s.str(), d, issues);
        return d;
    }

    CarSetup With(std::initializer_list<std::pair<const char*, const char*>> picks)
    {
        CarSetup s;
        for (const auto& [slot, level] : picks)
            s.parts[std::string(kPerformancePrefix) + slot] = level;
        return s;
    }

    float TorqueAt(const VehicleDefinition& d, float rpm)
    {
        for (size_t i = 1; i < d.torque_curve.size(); ++i)
            if (rpm <= d.torque_curve[i].rpm)
            {
                const TorquePoint& a = d.torque_curve[i - 1];
                const TorquePoint& b = d.torque_curve[i];
                const float k = (rpm - a.rpm) / (b.rpm - a.rpm);
                return (a.normalized + (b.normalized - a.normalized) * k) * d.max_engine_torque_nm;
            }
        return d.torque_curve.back().normalized * d.max_engine_torque_nm;
    }
}

TEST_CASE("performance: every slot starts at stock and stock changes nothing")
{
    const VehicleDefinition base = D01();
    CHECK(!PerformanceSlots().empty());
    for (const PerformanceSlot& s : PerformanceSlots())
        CHECK(ChosenPerformance(CarSetup{}, s.id) == s.levels.front().id);
    const VehicleDefinition d = ApplyPerformance(base, CarSetup{});
    CHECK(d.max_engine_torque_nm == base.max_engine_torque_nm);
    CHECK(d.mass_kg == base.mass_kg);
    CHECK(d.tires.lateral_grip == base.tires.lateral_grip);
    CHECK(d.max_rpm == base.max_rpm);
    CHECK(d.final_drive == base.final_drive);
}

TEST_CASE("performance: engine, turbo, intake, ECU and exhaust stack into more torque, capped")
{
    const VehicleDefinition base = D01();
    const VehicleDefinition s1 = ApplyPerformance(base, With({ { "engine", "stage1" } }));
    const VehicleDefinition s3 = ApplyPerformance(base, With({ { "engine", "stage3" } }));
    CHECK(s1.max_engine_torque_nm > base.max_engine_torque_nm);
    CHECK(s3.max_engine_torque_nm > s1.max_engine_torque_nm);
    CHECK(s3.max_rpm > base.max_rpm);                         // a built engine revs higher
    CHECK(s3.shift_up_rpm > base.shift_up_rpm);
    const VehicleDefinition all = ApplyPerformance(base, With({ { "engine", "stage3" }, { "turbo", "race" }, { "intake", "cold_air" }, { "ecu", "tuned" }, { "exhaust_sys", "straight" } }));
    CHECK(all.max_engine_torque_nm > s3.max_engine_torque_nm);
    CHECK(all.max_engine_torque_nm <= base.max_engine_torque_nm * kMaxTorqueGain + 0.01f); // the whole build tops out
}

TEST_CASE("performance: a big turbo has lag - less torque low down, much more on boost")
{
    const VehicleDefinition base = D01();
    const VehicleDefinition t = ApplyPerformance(base, With({ { "turbo", "race" } }));
    const float low_rpm = base.min_rpm + 600.0f;
    const float high_rpm = base.max_rpm * 0.75f;
    CHECK(TorqueAt(t, low_rpm) < TorqueAt(base, low_rpm) * 1.0f);    // off boost
    CHECK(TorqueAt(t, high_rpm) > TorqueAt(base, high_rpm) * 1.2f);  // on boost
    for (size_t i = 1; i < t.torque_curve.size(); ++i)                // still a valid curve
        CHECK(t.torque_curve[i].rpm > t.torque_curve[i - 1].rpm);
}

TEST_CASE("performance: chassis parts - tyres, brakes, suspension, gearbox, weight")
{
    const VehicleDefinition base = D01();
    const VehicleDefinition d = ApplyPerformance(base, With({ { "tyres", "semi_slick" }, { "brakes", "race" }, { "suspension", "race" },
                                                             { "gearbox", "sequential" }, { "weight", "stripped" } }));
    CHECK(d.tires.lateral_grip > base.tires.lateral_grip * 1.1f);
    CHECK(d.tires.longitudinal_grip > base.tires.longitudinal_grip);
    CHECK(d.brake_torque_nm > base.brake_torque_nm * 1.3f);
    CHECK(d.front_suspension.frequency_hz > base.front_suspension.frequency_hz);
    CHECK(d.rear_suspension.frequency_hz > base.rear_suspension.frequency_hz);
    CHECK(d.shift_time_s < base.shift_time_s * 0.6f);
    CHECK(d.mass_kg < base.mass_kg - 100.0f);
    CHECK(d.health < base.health); // stripped panels: less to absorb a hit
}

TEST_CASE("performance: cycling stores the level in the setup, stock removes it, unknown falls back")
{
    CarSetup s;
    CyclePerformance(s, "engine", 1);
    CHECK(ChosenPerformance(s, "engine") == "stage1");
    CHECK(s.parts.count(std::string(kPerformancePrefix) + "engine") == 1);
    CyclePerformance(s, "engine", -1);
    CHECK(ChosenPerformance(s, "engine") == "stock");
    CHECK(s.parts.count(std::string(kPerformancePrefix) + "engine") == 0); // a stock car saves nothing
    CyclePerformance(s, "engine", -1);                                     // wraps to the top level
    CHECK(ChosenPerformance(s, "engine") == "stage3");
    s.parts[std::string(kPerformancePrefix) + "turbo"] = "jet_engine";     // a level a later update removed
    CHECK(ChosenPerformance(s, "turbo") == "none");
    const VehicleDefinition base = D01();
    CHECK(ApplyPerformance(base, s).max_engine_torque_nm > base.max_engine_torque_nm); // stage3 still applies
}

TEST_CASE("performance: the rating rises with upgrades and the estimated top speed with power")
{
    const VehicleDefinition base = D01();
    const VehicleDefinition tuned = ApplyPerformance(base, With({ { "engine", "stage2" }, { "turbo", "street" }, { "tyres", "sport" } }));
    CHECK(PerformanceIndex(tuned) > PerformanceIndex(base));
    CHECK(tuned.top_speed_target_kmh > base.top_speed_target_kmh);
}

TEST_CASE("performance: sport and race suspension lower the car, never past the bump stop")
{
    const VehicleDefinition base = D01();
    const VehicleDefinition sport = ApplyPerformance(base, With({ { "suspension", "sport" } }));
    const VehicleDefinition race = ApplyPerformance(base, With({ { "suspension", "race" } }));
    CHECK(sport.front_suspension.max_length_m < base.front_suspension.max_length_m);
    CHECK(race.front_suspension.max_length_m < sport.front_suspension.max_length_m);
    CHECK(race.rear_suspension.max_length_m < base.rear_suspension.max_length_m - 0.02f);
    CHECK(race.front_suspension.max_length_m > race.front_suspension.min_length_m + 0.1f);
}
