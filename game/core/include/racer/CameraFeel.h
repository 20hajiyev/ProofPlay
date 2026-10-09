#pragma once

namespace racer
{
    // Chase-camera feel (D-058): what makes speed readable and hits physical - a field of view
    // that opens with speed and boost, a camera that falls back at speed, swings out and rolls a
    // little into turns, trauma-based shake on hits and landings, manga speed lines. Pure maths,
    // frame-rate independent; the renderer applies it.
    struct CameraFeelInput
    {
        float speed_kmh = 0.0f;
        float yaw_rate = 0.0f;       // rad/s, positive turning right
        bool boost = false;          // Surge active
        bool reduce_shake = false;   // accessibility: less shake, no speed lines
    };

    struct CameraFeelOutput
    {
        float fov_deg = 62.0f;
        float distance = 6.5f;       // behind the car
        float height = 2.4f;
        float lateral = 0.0f;        // metres, + right (swing-out is opposite to the turn)
        float roll_deg = 0.0f;       // + rolls the view clockwise (into a right turn)
        float shake_x = 0.0f, shake_y = 0.0f; // metres
        float speed_lines = 0.0f;    // 0..1 overlay strength
    };

    struct CameraFeelParams
    {
        // Owner (2026-09-30, 2026-10-03): "the camera drifts far from the car at speed", "lower and
        // closer". The FOV widening shrank the car on screen to 72% at 200 km/h; now speed reads from
        // the lines and the swing, the car keeps >= 85% of its size, and the chase sits low and near.
        float base_fov_deg = 62.0f, max_fov_deg = 72.0f, fov_per_kmh = 0.042f, boost_fov_deg = 5.0f;
        float base_distance = 5.0f, distance_per_kmh = 0.0f, boost_distance = 0.3f, max_distance = 5.4f;
        float height = 1.75f;
        float lateral_per_yaw = 1.1f, max_lateral = 1.3f;
        float roll_per_yaw = 3.0f, max_roll_deg = 3.5f;
        float response_hz = 3.0f;          // smoothing of fov/distance/lateral/roll
        float trauma_decay = 1.6f;         // per second
        float max_shake = 0.35f;           // metres at full trauma
        float reduced_shake_scale = 0.25f;
        float speed_lines_from_kmh = 130.0f, speed_lines_full_kmh = 200.0f;
    };

    class CameraFeel
    {
    public:
        explicit CameraFeel(CameraFeelParams p = {}) : p_(p) {}
        void Update(const CameraFeelInput& in, float dt);
        void AddTrauma(float amount); // 0..1; hits ~0.4-0.8, landings by fall speed
        const CameraFeelOutput& Output() const { return out_; }
        const CameraFeelParams& Params() const { return p_; }

    private:
        CameraFeelParams p_;
        CameraFeelOutput out_;
        float trauma_ = 0.0f;
        float time_ = 0.0f;
        bool primed_ = false;
    };
}
