#pragma once
#include "racer/Customization.h"
#include "racer/Vehicle.h"

#include <string>
#include <vector>

namespace racer
{
    // Performance upgrades (D-071, owner: "many realistic mods, down to the engine"). Unlike the
    // visual parts these change the car's physics definition. The choices live in the same
    // CarSetup::parts map under "perf_<slot>" keys, so the save format does not change and a stock
    // car stores nothing.
    constexpr const char* kPerformancePrefix = "perf_";
    constexpr float kMaxTorqueGain = 1.85f; // a full build tops out here (keeps class D cars racing class D)

    struct PerformanceLevel
    {
        std::string id;          // localisation key "perf.<id>"
        float torque = 1.0f;     // x max engine torque
        float low_end = 1.0f;    // turbo lag: torque fraction at idle, ramping to 1 at boost
        float rpm = 0.0f;        // + rev limit and shift point
        float inertia = 1.0f;    // x engine inertia (lighter internals rev faster)
        float mass = 0.0f;       // + kg
        float grip = 1.0f;       // x tyre grip (both directions)
        float brake = 1.0f;      // x brake torque
        float susp = 1.0f;       // x spring frequency, both axles
        float final_drive = 1.0f;
        float shift = 1.0f;      // x shift time
        float health = 0.0f;     // + HP
        float ride = 0.0f;       // + suspension travel at full droop (m): negative lowers the car
    };

    struct PerformanceSlot
    {
        std::string id;          // "engine", "turbo", ... ; localisation key "perfslot.<id>"
        std::vector<PerformanceLevel> levels; // levels[0] is stock
    };

    const std::vector<PerformanceSlot>& PerformanceSlots();
    std::string ChosenPerformance(const CarSetup& s, const std::string& slot);
    void CyclePerformance(CarSetup& s, const std::string& slot, int dir);
    VehicleDefinition ApplyPerformance(const VehicleDefinition& base, const CarSetup& s);
    // One number for the garage (like a racing game's PI): power-to-weight, grip and brakes.
    int PerformanceIndex(const VehicleDefinition& d);
}
