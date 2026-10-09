#pragma once
#include "racer/Race.h"
#include "racer/Vehicle.h"

#include <vector>

namespace racer
{
    // Maximum speed along the route from curvature, then limited by braking into and
    // accelerating out of corners (forward/backward passes).
    class SpeedProfile
    {
    public:
        SpeedProfile() = default;
        SpeedProfile(const TrackRoute& route, float lateral_accel, float brake_decel, float drive_accel, float max_speed_ms, float sample_m = 4.0f);
        float At(float s) const;
        float Min() const;

    private:
        std::vector<float> speed_;
        float sample_m_ = 4.0f;
        float length_ = 0.0f;
    };

    enum class AIDifficulty { Easy, Normal, Hard };

    // Plan 2.10: difficulty changes reaction and margins only; the car is the same physics object.
    struct AIParams
    {
        float reaction_s = 0.4f;          // plan: easy 450-750 ms, normal 300-500, hard 180-350
        float lateral_accel = 8.0f;       // m/s^2 the planner is willing to use in corners
        float brake_decel = 7.0f;
        float lookahead_base_m = 6.0f;
        float lookahead_per_ms = 0.55f;
        float lane_offset_m = 0.0f;       // + = right of the racing line
        float steer_gain = 1.0f;
    };
    AIParams MakeAIParams(AIDifficulty difficulty, uint32_t seed);

    struct AIObservation
    {
        Vec2 position;
        float heading_rad = 0.0f;  // atan2(forward.x, forward.z)
        float speed_ms = 0.0f;     // signed, along the car's forward axis
        float route_s = 0.0f;
    };

    // Low-level driver, run every physics step (120 Hz). Emits the same VehicleCommand a player would.
    class AIDriver
    {
    public:
        AIDriver(const TrackRoute& route, const VehicleDefinition& car, AIParams params);

        VehicleCommand Drive(const AIObservation& obs, float dt);
        void SetLaneOffset(float metres) { params_.lane_offset_m = metres; }
        // Multiplies the speed target, clamped to [0.75, 1] (duel rival keeping the fight close).
        void SetPaceScale(float s) { pace_ = s < 0.75f ? 0.75f : s > 1.0f ? 1.0f : s; }
        const SpeedProfile& Profile() const { return profile_; }
        float TargetSpeed() const { return last_target_; }
        bool Reversing() const { return reverse_left_ > 0.0f; }

        // Unstick: wanting to drive but not moving for this long starts a reverse manoeuvre.
        static constexpr float kStuckSeconds = 1.0f;
        static constexpr float kReverseSeconds = 1.2f;

    private:
        const TrackRoute& route_;
        VehicleDefinition car_;
        AIParams params_;
        SpeedProfile profile_;
        DriveAssist steering_model_; // to invert the speed-sensitive steering limit
        float last_target_ = 0.0f;
        float pace_ = 1.0f;
        float stuck_s_ = 0.0f;
        float reverse_left_ = 0.0f;
        float reverse_steer_ = 0.0f;
    };
}
