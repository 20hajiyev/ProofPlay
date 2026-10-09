#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace racer
{
    enum class VehicleClass { D, C, B, A };
    enum class Drivetrain { FWD, RWD, AWD };

    struct TorquePoint
    {
        float rpm = 0.0f;
        float normalized = 0.0f; // fraction of max_engine_torque_nm
    };

    struct SuspensionDef
    {
        float min_length_m = 0.0f;
        float max_length_m = 0.0f;
        float frequency_hz = 0.0f;
        float damping = 0.0f;
    };

    struct SteeringDef
    {
        float max_angle_deg = 0.0f;       // lock at standstill
        float high_speed_angle_deg = 0.0f; // lock at and above high_speed_kmh
        float high_speed_kmh = 0.0f;
        float rate_per_s = 0.0f;          // how fast the wheel turns in (normalized lock per second)
        float return_rate_per_s = 0.0f;   // how fast it recentres
    };

    struct TireDef
    {
        float longitudinal_grip = 0.0f;
        float lateral_grip = 0.0f;
    };

    struct VehicleDefinition
    {
        std::string id;
        VehicleClass vehicle_class = VehicleClass::D;
        Drivetrain drivetrain = Drivetrain::FWD;
        std::string visual_asset;
        std::string audio_family;

        float mass_kg = 0.0f;
        float length_m = 0.0f;
        float width_m = 0.0f;
        float height_m = 0.0f;
        float wheelbase_m = 0.0f;
        float track_m = 0.0f;
        float wheel_radius_m = 0.0f;
        float wheel_width_m = 0.0f;
        float com_height_m = 0.0f;

        float max_engine_torque_nm = 0.0f;
        float min_rpm = 0.0f;
        float max_rpm = 0.0f;
        // Reflected through the gearing (x ratio^2 / r^2); dominates first-gear acceleration.
        float engine_inertia_kgm2 = 0.0f;
        float shift_time_s = 0.0f;
        std::vector<TorquePoint> torque_curve;
        std::vector<float> gear_ratios;
        float reverse_ratio = 0.0f;
        float final_drive = 0.0f;
        float shift_up_rpm = 0.0f;
        float shift_down_rpm = 0.0f;

        SuspensionDef front_suspension;
        SuspensionDef rear_suspension;
        SteeringDef steering;
        TireDef tires;
        float brake_torque_nm = 0.0f;     // per wheel
        float handbrake_torque_nm = 0.0f; // per rear wheel
        // Rear lateral grip multiplier while the handbrake is held (plan 2.6 controlled drift);
        // lower = easier to rotate. Restored over drift_recovery_s after release.
        float handbrake_rear_grip = 1.0f;
        float drift_recovery_s = 0.0f;

        float top_speed_target_kmh = 0.0f;
        float health = 0.0f;
    };

    struct ValidationIssue
    {
        std::string path;
        std::string message;
    };

    // Strict parse: unknown fields, missing fields, wrong types and out-of-range units are all errors.
    bool ParseVehicleDefinition(const std::string& json_text, VehicleDefinition& out, std::vector<ValidationIssue>& issues);

    // One-shot inputs; each is delivered to exactly one simulation step.
    enum VehicleEvent : uint32_t
    {
        EVENT_NONE = 0,
        EVENT_RESET = 1u << 0,
        EVENT_CAMERA = 1u << 1,
    };

    struct VehicleCommand
    {
        float throttle = 0.0f;  // 0..1
        float brake = 0.0f;     // 0..1
        float steer = 0.0f;     // -1 (left) .. 1 (right)
        float handbrake = 0.0f; // 0..1
        uint32_t events = EVENT_NONE;
    };

    // Holds the latest continuous input and accumulates one-shot events until a step consumes them,
    // so a frame that runs 8 catch-up steps never applies a press 8 times (or zero times).
    class CommandBuffer
    {
    public:
        void Submit(const VehicleCommand& command);
        VehicleCommand ForStep();

    private:
        VehicleCommand latest_;
        uint32_t pending_events_ = EVENT_NONE;
    };

    // What the physics vehicle controller receives.
    struct DriverInput
    {
        float forward = 0.0f;   // -1..1, negative = reverse
        float steer = 0.0f;     // -1..1 of the full mechanical lock, right positive
        float brake = 0.0f;     // 0..1
        float handbrake = 0.0f; // 0..1
    };

    // Speed-sensitive steering limit, steering smoothing and brake-to-reverse.
    class DriveAssist
    {
    public:
        explicit DriveAssist(const SteeringDef& steering);
        DriverInput Apply(const VehicleCommand& command, float forward_speed_kmh, float dt);
        float SteerLimit(float speed_kmh) const;

    private:
        SteeringDef steering_;
        float steer_ = 0.0f;
    };

    // Jolt's automatic gearbox refuses to upshift while a driven wheel slips > 0.1, which is
    // normal for a FWD car powering out of a corner, so it bounced on the limiter in 1st at
    // 56 km/h. This asks for one upshift after sustained time at the limiter under throttle.
    class LimiterShiftAssist
    {
    public:
        // Returns true when the caller should shift up one gear now.
        bool Update(int gear, int forward_gears, float rpm, float shift_up_rpm, float throttle, float dt);

        static constexpr float kHoldSeconds = 0.3f;

    private:
        float at_limit_s_ = 0.0f;
    };

    struct VehicleTelemetry
    {
        uint64_t tick = 0;
        float position[3] = {};
        float velocity[3] = {};
        float forward_speed_kmh = 0.0f;
        float rpm = 0.0f;
        int gear = 0;
        float wheel_longitudinal_slip[4] = {};
        float wheel_lateral_slip_rad[4] = {};
        bool wheel_contact[4] = {};
        float up_y = 1.0f; // chassis up vector's world Y; < 0 means upside down
        float heading_rad = 0.0f; // atan2(forward.x, forward.z); increases turning right
        float yaw_rate = 0.0f;    // rad/s, positive turning right
        bool AnyWheelContact() const { return wheel_contact[0] || wheel_contact[1] || wheel_contact[2] || wheel_contact[3]; }
    };

    // False if the state contains NaN/inf or physically impossible speed.
    bool IsTelemetrySane(const VehicleTelemetry& telemetry);
}
