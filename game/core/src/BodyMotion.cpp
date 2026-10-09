#include "racer/BodyMotion.h"

#include <algorithm>
#include <cmath>

namespace racer
{
    namespace
    {
        constexpr float kTwoPi = 6.2831853f;
        constexpr float kSubstep = 1.0f / 240.0f; // springs step at a fixed rate: same look at any fps
    }

    void BodyMotion::Spring::Step(float target, float hz, float zeta, float dt)
    {
        const float w = kTwoPi * hz;
        // Semi-implicit Euler of x'' = w^2 (target - x) - 2 zeta w x'.
        v += (w * w * (target - x) - 2.0f * zeta * w * v) * dt;
        x += v * dt;
    }

    void BodyMotion::Update(const BodyMotionInput& in, float dt)
    {
        if (dt <= 0.0f)
            return;
        // Longitudinal acceleration from the speed change over this frame.
        const float accel = have_speed_ ? (in.forward_speed - last_speed_) / dt : 0.0f;
        last_speed_ = in.forward_speed;
        have_speed_ = true;
        const float lateral = in.forward_speed * in.yaw_rate; // centripetal, + for a right turn

        float roll_target = 0.0f, pitch_target = 0.0f, squash_target = 1.0f;
        if (in.grounded)
        {
            // The body leans away from the turn and squats under throttle (nose up).
            roll_target = std::clamp(-p_.roll_per_accel * lateral, -p_.max_roll, p_.max_roll);
            pitch_target = std::clamp(p_.pitch_per_accel * accel, -p_.max_pitch, p_.max_pitch);
            if (!was_grounded_)
            {
                // Landing: kick the squash spring downward in proportion to the fall speed.
                const float dip = std::min(p_.max_squash, p_.squash_per_speed * air_fall_speed_);
                squash_.v -= dip * kTwoPi * p_.squash_frequency_hz;
            }
            air_fall_speed_ = 0.0f;
        }
        else
        {
            squash_target = 1.0f + p_.air_stretch; // a little stretch while flying
            air_fall_speed_ = std::max(air_fall_speed_, -in.vertical_speed);
        }
        was_grounded_ = in.grounded;

        // Springs work around 0; squash is stored as an offset from 1.
        for (float left = dt; left > 1e-6f; left -= kSubstep)
        {
            const float h = std::min(kSubstep, left);
            roll_.Step(roll_target, p_.frequency_hz, p_.damping, h);
            pitch_.Step(pitch_target, p_.frequency_hz, p_.damping, h);
            squash_.Step(squash_target - 1.0f, p_.squash_frequency_hz, p_.squash_damping, h);
        }
        // Springs may overshoot the target by 20% (the toon bounce), never more: hard clamps keep
        // the arches clear of the tyres whatever the springs do.
        pose_.roll = std::clamp(roll_.x, -p_.max_roll * 1.2f, p_.max_roll * 1.2f);
        pose_.pitch = std::clamp(pitch_.x, -p_.max_pitch * 1.2f, p_.max_pitch * 1.2f);
        pose_.squash = std::clamp(1.0f + squash_.x, 1.0f - p_.max_squash * 1.2f, 1.0f + p_.max_squash);
    }

    void BodyMotion::Hit(float side, float front, float strength)
    {
        // A hit from the right shoves the body over to the left; from ahead, the nose rises.
        const float kick = 0.35f * strength;
        roll_.v -= side * kick;
        pitch_.v += front * kick;
    }
}
