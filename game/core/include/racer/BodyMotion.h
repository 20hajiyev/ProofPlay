#pragma once

namespace racer
{
    // Toon art direction (D-050): visual-only body animation - lean in corners, squat and dive
    // under throttle and brake, squash-and-stretch on landings, a kick when hit. Springs with low
    // damping give the bouncy, readable motion of cartoon racers. Physics never sees it: the
    // body mesh moves under an unmoved chassis, wheels stay on the ground.
    struct BodyMotionInput
    {
        float forward_speed = 0.0f;  // m/s, signed
        float yaw_rate = 0.0f;       // rad/s, positive turning right
        float vertical_speed = 0.0f; // m/s, positive up
        bool grounded = true;
    };

    struct BodyPose
    {
        float roll = 0.0f;   // rad, positive leans right
        float pitch = 0.0f;  // rad, positive lifts the nose
        float squash = 1.0f; // vertical scale (width compensates in the renderer)
    };

    struct BodyMotionParams
    {
        // Limits keep every wheel arch clear of its tyre (the arch sits 8 cm over the tyre; see the
        // "never drop a wheel arch onto the tyre" test): ~1.4 deg lean, ~0.6 deg dive, 2% squash.
        float roll_per_accel = 0.004f;  // rad per m/s^2 lateral
        float pitch_per_accel = 0.003f; // rad per m/s^2 longitudinal
        float max_roll = 0.025f, max_pitch = 0.01f;
        float frequency_hz = 2.2f;      // lean spring
        float damping = 0.35f;          // < 1: overshoot
        float squash_per_speed = 0.004f;// per m/s of landing speed
        float max_squash = 0.02f;
        float squash_frequency_hz = 3.2f, squash_damping = 0.3f;
        float air_stretch = 0.008f;
    };

    class BodyMotion
    {
    public:
        explicit BodyMotion(BodyMotionParams p = {}) : p_(p) {}
        void Update(const BodyMotionInput& in, float dt);
        // side: +1 hit from the right, -1 from the left; front: +1 from ahead. Kicks the springs.
        void Hit(float side, float front, float strength = 1.0f);
        const BodyPose& Pose() const { return pose_; }
        const BodyMotionParams& Params() const { return p_; }

    private:
        struct Spring
        {
            float x = 0, v = 0;
            void Step(float target, float hz, float zeta, float dt);
        };
        BodyMotionParams p_;
        BodyPose pose_;
        Spring roll_, pitch_, squash_;
        float last_speed_ = 0.0f;
        bool have_speed_ = false;
        bool was_grounded_ = true;
        float air_fall_speed_ = 0.0f;
    };
}
