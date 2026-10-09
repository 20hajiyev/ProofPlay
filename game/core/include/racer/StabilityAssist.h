#pragma once

namespace racer
{
    // Body-space state the assist reads each step. Axes: +X right, +Y up, +Z forward.
    struct StabilityInput
    {
        float forward_speed_ms = 0.0f;
        float angular_velocity[3] = {}; // rad/s, body space
        float pitch_rad = 0.0f;         // nose up positive
        float roll_rad = 0.0f;          // right side down positive
        float steer = 0.0f;             // applied steering, -1..1 of full lock
        float handbrake = 0.0f;
        bool airborne = false;
        float max_steer_rad = 0.0f;
        float wheelbase_m = 0.0f;
    };

    struct StabilityTuning
    {
        // Yaw rate may exceed the kinematic (bicycle model) rate by this factor before correcting.
        float yaw_allowance = 1.35f;
        float yaw_gain = 3.0f;           // corrective angular accel per rad/s of excess yaw rate
        float max_yaw_accel = 6.0f;      // rad/s^2
        float drift_recovery_s = 0.6f;   // assist ramps back in after the handbrake is released
        float air_level_gain = 3.0f;     // rad/s^2 per rad of pitch/roll error
        float air_damping = 2.0f;        // rad/s^2 per rad/s
        float max_air_accel = 4.0f;      // rad/s^2, kept small: "limited" orientation help
        // Wall / car hits (D-067, owner: "the car still spins a lot on a wall hit"): a yaw rate that
        // jumps faster than steering can make it opens a short window of strong yaw damping.
        float impact_yaw_change = 1.2f;  // rad/s of yaw gained within 3 steps (25 ms), held, = a hit
        float impact_min_excess = 0.8f;  // and the spin must be this far past what the steering asks
        float impact_min_speed = 8.0f;   // m/s
        float impact_window_s = 0.30f;
        // Measured (D-067): gain 14 / 40 rad/s^2 pinned cars nose-in to the wall (E04 lap +10 s, duel
        // 35% close); 6 / 12 keeps E04 at 133.3 s and the duel at 84% while halving the spin.
        float impact_gain = 6.0f;
        float impact_max_accel = 12.0f;  // rad/s^2
    };

    // Returns a corrective angular acceleration (body space, rad/s^2). Never adds speed or grip:
    // it only removes rotation the driver did not ask for, so the player and AI stay equal.
    class StabilityAssist
    {
    public:
        explicit StabilityAssist(StabilityTuning tuning = {}) : tuning_(tuning) {}

        void Compute(const StabilityInput& in, float dt, float out_angular_accel[3]);
        float YawAuthority() const { return authority_; }
        bool ImpactActive() const { return impact_left_ > 0.0f; }

    private:
        StabilityTuning tuning_;
        float authority_ = 1.0f; // 0 while drifting on the handbrake, ramps back to 1
        float impact_left_ = 0.0f; // seconds of strong damping left after a hit
        float yaw_hist_[4] = {};   // the last yaw rates, one per step (single-step physics noise
        int yaw_count_ = 0;        // at low speed read as hits: 1843 false ones in one duel)
        int yaw_head_ = 0;
    };
}
