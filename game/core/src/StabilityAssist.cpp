#include "racer/StabilityAssist.h"

#include <algorithm>
#include <cmath>

namespace racer
{
    void StabilityAssist::Compute(const StabilityInput& in, float dt, float out[3])
    {
        out[0] = out[1] = out[2] = 0.0f;
        const StabilityTuning& t = tuning_;

        if (in.airborne)
        {
            // Level the car gently toward horizontal and damp tumbling; yaw is left to the player.
            const float ax = -t.air_level_gain * in.pitch_rad - t.air_damping * in.angular_velocity[0];
            const float az = -t.air_level_gain * in.roll_rad - t.air_damping * in.angular_velocity[2];
            out[0] = std::clamp(ax, -t.max_air_accel, t.max_air_accel);
            out[2] = std::clamp(az, -t.max_air_accel, t.max_air_accel);
            yaw_count_ = 0; // a landing's yaw change is not a wall hit
            return;
        }

        if (in.handbrake > 0.1f)
            authority_ = 0.0f;
        else
            authority_ = std::min(1.0f, authority_ + dt / t.drift_recovery_s);

        // Kinematic yaw rate the driver asked for: v * tan(steer angle) / wheelbase.
        const float steer_angle = in.steer * in.max_steer_rad;
        const float intended = in.forward_speed_ms * std::tan(steer_angle) / in.wheelbase_m;
        const float allowed = std::fabs(intended) * t.yaw_allowance + 0.15f; // small floor for straight-line wobble
        const float yaw = in.angular_velocity[1];
        const float excess = std::fabs(yaw) - allowed;

        // A hit: within 3 steps the yaw rate gained more than steering can make, it is still there,
        // and it is far past what the wheels ask for.
        const float yaw_3ago = yaw_hist_[(yaw_head_ + 1) % 4];
        if (yaw_count_ >= 3 && in.forward_speed_ms > t.impact_min_speed && excess > t.impact_min_excess &&
            std::fabs(yaw) - std::fabs(yaw_3ago) > t.impact_yaw_change)
            impact_left_ = t.impact_window_s;
        else
            impact_left_ = std::max(0.0f, impact_left_ - dt);
        yaw_hist_[yaw_head_] = yaw;
        yaw_head_ = (yaw_head_ + 1) % 4;
        yaw_count_ = std::min(4, yaw_count_ + 1);
        if (excess > 0.0f && std::fabs(in.forward_speed_ms) > 3.0f)
        {
            // after a hit the spin is taken out hard: walls slide the car, not spin it. A handbrake
            // drift keeps its freedom (the AI flicks into hairpins; D-067 duel measurement)
            const float correction = (impact_left_ > 0.0f ? std::min(t.impact_gain * excess, t.impact_max_accel)
                                                          : std::min(t.yaw_gain * excess, t.max_yaw_accel)) * authority_;
            out[1] = yaw > 0.0f ? -correction : correction;
        }
    }
}
