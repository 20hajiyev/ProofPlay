#include "racer/AI.h"

#include <algorithm>
#include <cmath>

namespace racer
{
    namespace
    {
        // Curvature from three points (1 / circumradius).
        float Curvature(Vec2 a, Vec2 b, Vec2 c)
        {
            const float ab = std::hypot(b.x - a.x, b.z - a.z);
            const float bc = std::hypot(c.x - b.x, c.z - b.z);
            const float ca = std::hypot(a.x - c.x, a.z - c.z);
            const float cross = (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
            const float denom = ab * bc * ca;
            return denom > 1e-6f ? 2.0f * std::fabs(cross) / denom : 0.0f;
        }

        // Deterministic per-car variation without a global RNG.
        float Hash01(uint32_t x)
        {
            x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
            return float(x & 0xFFFFFF) / float(0xFFFFFF);
        }
    }

    SpeedProfile::SpeedProfile(const TrackRoute& route, float lat, float brake, float drive, float vmax, float sample_m)
        : sample_m_(sample_m), length_(route.Length())
    {
        const size_t n = std::max<size_t>(3, size_t(length_ / sample_m));
        sample_m_ = length_ / float(n);
        speed_.resize(n);
        const float span = 12.0f; // curvature measured over +-12 m to smooth polyline corners
        for (size_t i = 0; i < n; ++i)
        {
            const float s = i * sample_m_;
            const float k = Curvature(route.PointAt(s - span), route.PointAt(s), route.PointAt(s + span));
            speed_[i] = k > 1e-5f ? std::min(vmax, std::sqrt(lat / k)) : vmax;
        }
        // Two laps of passes so limits propagate across the start/finish seam.
        for (int pass = 0; pass < 2; ++pass)
        {
            for (size_t j = 0; j < n; ++j) // backward: brake in time for the next corner
            {
                const size_t i = (n - 1 - j), next = (i + 1) % n;
                speed_[i] = std::min(speed_[i], std::sqrt(speed_[next] * speed_[next] + 2.0f * brake * sample_m_));
            }
            for (size_t i = 0; i < n; ++i) // forward: cannot accelerate faster than the car does
            {
                const size_t prev = (i + n - 1) % n;
                speed_[i] = std::min(speed_[i], std::sqrt(speed_[prev] * speed_[prev] + 2.0f * drive * sample_m_));
            }
        }
    }

    float SpeedProfile::At(float s) const
    {
        if (speed_.empty())
            return 0.0f;
        float u = std::fmod(s, length_);
        if (u < 0.0f)
            u += length_;
        const float f = u / sample_m_;
        const size_t i = size_t(f) % speed_.size();
        const size_t j = (i + 1) % speed_.size();
        const float t = f - std::floor(f);
        return speed_[i] + (speed_[j] - speed_[i]) * t;
    }

    float SpeedProfile::Min() const
    {
        return speed_.empty() ? 0.0f : *std::min_element(speed_.begin(), speed_.end());
    }

    AIParams MakeAIParams(AIDifficulty difficulty, uint32_t seed)
    {
        AIParams p;
        const float r = Hash01(seed * 2654435761u + 1u);
        switch (difficulty)
        {
        case AIDifficulty::Easy:
            p.reaction_s = 0.45f + 0.30f * r;
            p.lateral_accel = 6.0f;
            p.brake_decel = 5.5f;
            break;
        case AIDifficulty::Normal:
            p.reaction_s = 0.30f + 0.20f * r;
            p.lateral_accel = 7.5f;
            p.brake_decel = 6.5f;
            break;
        case AIDifficulty::Hard:
            p.reaction_s = 0.18f + 0.17f * r;
            p.lateral_accel = 8.5f;
            p.brake_decel = 7.5f;
            break;
        }
        return p;
    }

    AIDriver::AIDriver(const TrackRoute& route, const VehicleDefinition& car, AIParams params)
        : route_(route), car_(car), params_(params), steering_model_(car.steering)
    {
        const float drive_accel = 3.5f; // conservative average for the forward pass; the car decides the rest
        profile_ = SpeedProfile(route, params.lateral_accel, params.brake_decel, drive_accel, car.top_speed_target_kmh / 3.6f);
    }

    VehicleCommand AIDriver::Drive(const AIObservation& o, float dt)
    {
        VehicleCommand c;
        if (reverse_left_ > 0.0f)
        {
            // Backing off a wall: foot brake at standstill = reverse (DriveAssist), with the
            // steering mirrored so the nose swings toward the racing line.
            reverse_left_ -= dt;
            c.brake = 1.0f;
            c.steer = reverse_steer_;
            return c;
        }
        const float v = std::max(0.0f, o.speed_ms);

        // Steering: pure pursuit toward a point ahead on the (offset) line.
        const float lookahead = params_.lookahead_base_m + params_.lookahead_per_ms * v;
        const float target_s = o.route_s + lookahead;
        const Vec2 on_line = route_.PointAt(target_s);
        const Vec2 dir = route_.DirectionAt(target_s);
        const Vec2 goal{ on_line.x + dir.z * params_.lane_offset_m, on_line.z - dir.x * params_.lane_offset_m };
        const float fx = std::sin(o.heading_rad), fz = std::cos(o.heading_rad); // forward
        const float rx = fz, rz = -fx;                                          // right of travel
        const float dx = goal.x - o.position.x, dz = goal.z - o.position.z;
        const float local_x = dx * rx + dz * rz;
        const float local_z = dx * fx + dz * fz;
        const float ld2 = std::max(1.0f, local_x * local_x + local_z * local_z);
        const float wheel_angle = std::atan(2.0f * car_.wheelbase_m * local_x / ld2);
        const float max_lock = car_.steering.max_angle_deg * 3.14159265f / 180.0f;
        // DriveAssist will scale the command by its speed limit; divide it back out so the
        // requested wheel angle is what reaches the tyres (up to the limit itself).
        const float limit = steering_model_.SteerLimit(v * 3.6f);
        c.steer = std::clamp(params_.steer_gain * wheel_angle / (max_lock * limit), -1.0f, 1.0f);
        if (local_z < 0.0f) // goal behind (e.g. just recovered facing backwards): full lock toward it
            c.steer = local_x >= 0.0f ? 1.0f : -1.0f;

        // Speed: what the profile allows where the car will be after the reaction time.
        last_target_ = profile_.At(o.route_s + v * params_.reaction_s) * pace_;
        const float error = last_target_ - v;
        if (error > 0.0f)
            c.throttle = std::clamp(0.3f + error * 0.25f, 0.0f, 1.0f);
        else if (error < -1.0f)
            c.brake = std::clamp(-error * 0.15f, 0.0f, 1.0f);

        // Pinned (e.g. nose against a wall after a hit): wanting to go but not moving.
        stuck_s_ = (c.throttle > 0.2f && std::fabs(o.speed_ms) < 0.5f) ? stuck_s_ + dt : 0.0f;
        if (stuck_s_ >= kStuckSeconds)
        {
            stuck_s_ = 0.0f;
            reverse_left_ = kReverseSeconds;
            reverse_steer_ = c.steer >= 0.0f ? -1.0f : 1.0f;
        }
        return c;
    }
}
