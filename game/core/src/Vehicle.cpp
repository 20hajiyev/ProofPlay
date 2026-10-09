#include "racer/Vehicle.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <set>

namespace racer
{
    namespace
    {
        using nlohmann::json;

        class Reader
        {
        public:
            Reader(const json& object, std::string path, std::vector<ValidationIssue>& issues)
                : object_(object), path_(std::move(path)), issues_(issues)
            {
                if (!object_.is_object())
                    Fail(path_, "expected an object");
            }

            ~Reader()
            {
                if (!object_.is_object())
                    return;
                for (auto it = object_.begin(); it != object_.end(); ++it)
                    if (!seen_.count(it.key()))
                        Fail(path_ + "." + it.key(), "unknown field");
            }

            float Number(const char* key, float min, float max)
            {
                const json* v = Field(key);
                if (!v)
                    return 0.0f;
                if (!v->is_number())
                {
                    Fail(Path(key), "expected a number");
                    return 0.0f;
                }
                const float value = v->get<float>();
                if (!(value >= min && value <= max))
                    Fail(Path(key), "out of range [" + std::to_string(min) + ", " + std::to_string(max) + "]");
                return value;
            }

            std::string String(const char* key)
            {
                const json* v = Field(key);
                if (!v)
                    return {};
                if (!v->is_string() || v->get<std::string>().empty())
                {
                    Fail(Path(key), "expected a non-empty string");
                    return {};
                }
                return v->get<std::string>();
            }

            template <typename Enum>
            Enum Choice(const char* key, std::initializer_list<std::pair<const char*, Enum>> options)
            {
                const std::string text = String(key);
                for (auto& [name, value] : options)
                    if (text == name)
                        return value;
                if (!text.empty())
                    Fail(Path(key), "unknown enum value '" + text + "'");
                return options.begin()->second;
            }

            const json* Array(const char* key)
            {
                const json* v = Field(key);
                if (v && (!v->is_array() || v->empty()))
                {
                    Fail(Path(key), "expected a non-empty array");
                    return nullptr;
                }
                return v;
            }

            const json* Object(const char* key)
            {
                const json* v = Field(key);
                return v;
            }

            std::string Path(const char* key) const { return path_ + "." + key; }
            void Fail(const std::string& path, const std::string& message) { issues_.push_back({ path, message }); }

        private:
            const json* Field(const char* key)
            {
                seen_.insert(key);
                if (!object_.is_object() || !object_.contains(key))
                {
                    Fail(Path(key), "missing field");
                    return nullptr;
                }
                return &object_.at(key);
            }

            const json& object_;
            std::string path_;
            std::vector<ValidationIssue>& issues_;
            std::set<std::string> seen_;
        };

        SuspensionDef ReadSuspension(const json* node, const std::string& path, std::vector<ValidationIssue>& issues)
        {
            SuspensionDef s;
            if (!node)
                return s;
            Reader r(*node, path, issues);
            s.min_length_m = r.Number("min_length_m", 0.05f, 1.0f);
            s.max_length_m = r.Number("max_length_m", 0.05f, 1.0f);
            s.frequency_hz = r.Number("frequency_hz", 0.5f, 5.0f);
            s.damping = r.Number("damping", 0.05f, 2.0f);
            if (s.max_length_m <= s.min_length_m)
                r.Fail(path + ".max_length_m", "must exceed min_length_m");
            return s;
        }
    }

    bool ParseVehicleDefinition(const std::string& json_text, VehicleDefinition& d, std::vector<ValidationIssue>& issues)
    {
        const size_t issues_before = issues.size();
        const json root = json::parse(json_text, nullptr, false);
        if (root.is_discarded())
        {
            issues.push_back({ "$", "invalid JSON" });
            return false;
        }
        {
            Reader r(root, "$", issues);
            const float schema = r.Number("schema_version", 1, 1);
            (void)schema;
            d.id = r.String("id");
            d.vehicle_class = r.Choice<VehicleClass>("class", { { "D", VehicleClass::D }, { "C", VehicleClass::C }, { "B", VehicleClass::B }, { "A", VehicleClass::A } });
            d.drivetrain = r.Choice<Drivetrain>("drivetrain", { { "FWD", Drivetrain::FWD }, { "RWD", Drivetrain::RWD }, { "AWD", Drivetrain::AWD } });
            d.visual_asset = r.String("visual_asset");
            d.audio_family = r.String("audio_family");

            d.mass_kg = r.Number("mass_kg", 600, 3000);
            d.length_m = r.Number("length_m", 3, 6);
            d.width_m = r.Number("width_m", 1.4f, 2.4f);
            d.height_m = r.Number("height_m", 0.9f, 2.2f);
            d.wheelbase_m = r.Number("wheelbase_m", 2, 3.6f);
            d.track_m = r.Number("track_m", 1.2f, 2.2f);
            d.wheel_radius_m = r.Number("wheel_radius_m", 0.25f, 0.5f);
            d.wheel_width_m = r.Number("wheel_width_m", 0.15f, 0.4f);
            d.com_height_m = r.Number("com_height_m", 0.2f, 1.0f);

            d.max_engine_torque_nm = r.Number("max_engine_torque_nm", 50, 1500);
            d.min_rpm = r.Number("min_rpm", 500, 2000);
            d.max_rpm = r.Number("max_rpm", 4000, 11000);
            d.engine_inertia_kgm2 = r.Number("engine_inertia_kgm2", 0.03f, 1.0f);
            d.shift_time_s = r.Number("shift_time_s", 0.05f, 1.0f);
            if (const json* curve = r.Array("torque_curve"))
            {
                float last_rpm = -1.0f;
                for (size_t i = 0; i < curve->size(); ++i)
                {
                    const std::string p = "$.torque_curve[" + std::to_string(i) + "]";
                    const json& point = (*curve)[i];
                    if (!point.is_array() || point.size() != 2 || !point[0].is_number() || !point[1].is_number())
                    {
                        issues.push_back({ p, "expected [rpm, normalized_torque]" });
                        continue;
                    }
                    TorquePoint tp{ point[0].get<float>(), point[1].get<float>() };
                    if (tp.rpm <= last_rpm)
                        issues.push_back({ p, "rpm must increase strictly" });
                    if (!(tp.normalized >= 0.0f && tp.normalized <= 1.0f))
                        issues.push_back({ p, "normalized torque must be in [0, 1]" });
                    last_rpm = tp.rpm;
                    d.torque_curve.push_back(tp);
                }
            }
            if (const json* gears = r.Array("gear_ratios"))
            {
                float last = 1e9f;
                for (size_t i = 0; i < gears->size(); ++i)
                {
                    const std::string p = "$.gear_ratios[" + std::to_string(i) + "]";
                    if (!(*gears)[i].is_number())
                    {
                        issues.push_back({ p, "expected a number" });
                        continue;
                    }
                    const float g = (*gears)[i].get<float>();
                    if (!(g > 0.3f && g < 6.0f) || g >= last)
                        issues.push_back({ p, "ratios must be in (0.3, 6) and strictly decreasing" });
                    last = g;
                    d.gear_ratios.push_back(g);
                }
            }
            d.reverse_ratio = r.Number("reverse_ratio", -6, -1);
            d.final_drive = r.Number("final_drive", 2, 6);
            d.shift_up_rpm = r.Number("shift_up_rpm", 2000, 11000);
            d.shift_down_rpm = r.Number("shift_down_rpm", 1000, 8000);
            if (d.shift_up_rpm > d.max_rpm || d.shift_down_rpm >= d.shift_up_rpm)
                r.Fail("$.shift_up_rpm", "require shift_down_rpm < shift_up_rpm <= max_rpm");

            d.front_suspension = ReadSuspension(r.Object("front_suspension"), "$.front_suspension", issues);
            d.rear_suspension = ReadSuspension(r.Object("rear_suspension"), "$.rear_suspension", issues);
            if (const json* steering = r.Object("steering"))
            {
                Reader s(*steering, "$.steering", issues);
                d.steering.max_angle_deg = s.Number("max_angle_deg", 10, 50);
                d.steering.high_speed_angle_deg = s.Number("high_speed_angle_deg", 2, 50);
                d.steering.high_speed_kmh = s.Number("high_speed_kmh", 40, 300);
                d.steering.rate_per_s = s.Number("rate_per_s", 0.5f, 20);
                d.steering.return_rate_per_s = s.Number("return_rate_per_s", 0.5f, 20);
                if (d.steering.high_speed_angle_deg > d.steering.max_angle_deg)
                    s.Fail("$.steering.high_speed_angle_deg", "must not exceed max_angle_deg");
            }
            if (const json* tires = r.Object("tires"))
            {
                Reader t(*tires, "$.tires", issues);
                d.tires.longitudinal_grip = t.Number("longitudinal_grip", 0.3f, 3);
                d.tires.lateral_grip = t.Number("lateral_grip", 0.3f, 3);
            }
            d.brake_torque_nm = r.Number("brake_torque_nm", 200, 10000);
            d.handbrake_torque_nm = r.Number("handbrake_torque_nm", 200, 10000);
            d.handbrake_rear_grip = r.Number("handbrake_rear_grip", 0.15f, 1.0f);
            d.drift_recovery_s = r.Number("drift_recovery_s", 0.1f, 3.0f);
            d.top_speed_target_kmh = r.Number("top_speed_target_kmh", 100, 400);
            d.health = r.Number("health", 50, 200);
            if (d.wheelbase_m >= d.length_m)
                r.Fail("$.wheelbase_m", "must be shorter than length_m");
            if (d.track_m >= d.width_m)
                r.Fail("$.track_m", "must be narrower than width_m");
        }
        return issues.size() == issues_before;
    }

    void CommandBuffer::Submit(const VehicleCommand& command)
    {
        latest_ = command;
        pending_events_ |= command.events;
    }

    VehicleCommand CommandBuffer::ForStep()
    {
        VehicleCommand step = latest_;
        step.events = pending_events_;
        pending_events_ = EVENT_NONE;
        return step;
    }

    DriveAssist::DriveAssist(const SteeringDef& steering) : steering_(steering) {}

    float DriveAssist::SteerLimit(float speed_kmh) const
    {
        const float t = std::clamp(std::fabs(speed_kmh) / steering_.high_speed_kmh, 0.0f, 1.0f);
        const float high_fraction = steering_.high_speed_angle_deg / steering_.max_angle_deg;
        return 1.0f + (high_fraction - 1.0f) * t;
    }

    DriverInput DriveAssist::Apply(const VehicleCommand& c, float forward_speed_kmh, float dt)
    {
        DriverInput out;
        const float limit = SteerLimit(forward_speed_kmh);
        const float target = std::clamp(c.steer, -1.0f, 1.0f) * limit;
        const bool recentring = std::fabs(target) < std::fabs(steer_) || target * steer_ < 0.0f;
        const float rate = (recentring ? steering_.return_rate_per_s : steering_.rate_per_s) * dt;
        steer_ += std::clamp(target - steer_, -rate, rate);
        out.steer = steer_;

        const float throttle = std::clamp(c.throttle, 0.0f, 1.0f);
        const float brake = std::clamp(c.brake, 0.0f, 1.0f);
        constexpr float kReverseEngageKmh = 2.0f;
        if (brake > 0.0f && throttle == 0.0f && forward_speed_kmh < kReverseEngageKmh)
        {
            out.forward = -brake; // brake-to-reverse once (nearly) stopped
        }
        else
        {
            out.forward = throttle;
            out.brake = brake;
        }
        out.handbrake = std::clamp(c.handbrake, 0.0f, 1.0f);
        return out;
    }

    bool LimiterShiftAssist::Update(int gear, int forward_gears, float rpm, float shift_up_rpm, float throttle, float dt)
    {
        const bool pinned = gear >= 1 && gear < forward_gears && rpm >= shift_up_rpm && throttle > 0.5f;
        at_limit_s_ = pinned ? at_limit_s_ + dt : 0.0f;
        if (at_limit_s_ < kHoldSeconds)
            return false;
        at_limit_s_ = 0.0f;
        return true;
    }

    bool IsTelemetrySane(const VehicleTelemetry& t)
    {
        constexpr float kMaxSpeed = 150.0f; // m/s, ~540 km/h
        float speed_sq = 0.0f;
        for (int i = 0; i < 3; ++i)
        {
            if (!std::isfinite(t.position[i]) || !std::isfinite(t.velocity[i]))
                return false;
            speed_sq += t.velocity[i] * t.velocity[i];
        }
        return std::isfinite(t.rpm) && std::isfinite(t.up_y) && speed_sq < kMaxSpeed * kMaxSpeed;
    }
}
