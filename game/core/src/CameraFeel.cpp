#include "racer/CameraFeel.h"

#include <algorithm>
#include <cmath>

namespace racer
{
    namespace
    {
        float Approach(float current, float target, float hz, float dt)
        {
            return target + (current - target) * std::exp(-6.2831853f * hz * dt);
        }
        // Smooth pseudo-noise in -1..1 (sum of incommensurate sines): no RNG, deterministic.
        float Noise(float t, float seed)
        {
            return 0.5f * std::sin(t * 23.0f + seed) + 0.3f * std::sin(t * 37.3f + seed * 2.1f) + 0.2f * std::sin(t * 61.7f + seed * 3.7f);
        }
    }

    void CameraFeel::AddTrauma(float amount)
    {
        trauma_ = std::clamp(trauma_ + amount, 0.0f, 1.0f);
    }

    void CameraFeel::Update(const CameraFeelInput& in, float dt)
    {
        if (dt <= 0.0f)
            return;
        time_ += dt;
        const float kmh = std::max(0.0f, in.speed_kmh);
        const float fov_target = std::min(p_.max_fov_deg, p_.base_fov_deg + p_.fov_per_kmh * kmh + (in.boost ? p_.boost_fov_deg : 0.0f));
        const float dist_target = std::min(p_.max_distance, p_.base_distance + p_.distance_per_kmh * kmh + (in.boost ? p_.boost_distance : 0.0f));
        const float speed_factor = std::clamp(kmh / 120.0f, 0.0f, 1.0f); // no swing when crawling
        const float lat_target = std::clamp(-p_.lateral_per_yaw * in.yaw_rate * speed_factor, -p_.max_lateral, p_.max_lateral);
        const float roll_target = std::clamp(p_.roll_per_yaw * in.yaw_rate * speed_factor, -p_.max_roll_deg, p_.max_roll_deg);
        if (!primed_)
        {
            out_.fov_deg = fov_target;
            out_.distance = dist_target;
            primed_ = true;
        }
        out_.fov_deg = Approach(out_.fov_deg, fov_target, p_.response_hz, dt);
        out_.distance = Approach(out_.distance, dist_target, p_.response_hz, dt);
        out_.lateral = Approach(out_.lateral, lat_target, p_.response_hz * 0.7f, dt);
        out_.roll_deg = Approach(out_.roll_deg, roll_target, p_.response_hz * 0.7f, dt);
        out_.height = p_.height;

        trauma_ = std::max(0.0f, trauma_ - p_.trauma_decay * dt);
        const float amp = trauma_ * trauma_ * p_.max_shake * (in.reduce_shake ? p_.reduced_shake_scale : 1.0f);
        out_.shake_x = amp * Noise(time_, 1.3f);
        out_.shake_y = amp * Noise(time_, 4.7f);

        const float lines_target = in.reduce_shake ? 0.0f
                                                   : std::clamp((kmh - p_.speed_lines_from_kmh) / (p_.speed_lines_full_kmh - p_.speed_lines_from_kmh), 0.0f, 1.0f) +
                                                         (in.boost ? 0.35f : 0.0f);
        out_.speed_lines = in.reduce_shake ? 0.0f : Approach(out_.speed_lines, std::min(1.0f, lines_target), 4.0f, dt);
    }
}
