#include "racer/runtime/VehicleRuntime.h"

#include "wiPhysics.h"

#include "Jolt/Jolt.h"
#include "Jolt/Physics/Body/Body.h"
#include "Jolt/Physics/Vehicle/VehicleConstraint.h"
#include "Jolt/Physics/Vehicle/WheeledVehicleController.h"
#include "Jolt/RegisterTypes.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace wi::ecs;
using namespace wi::scene;

namespace racer::runtime
{
    namespace
    {
        JPH::VehicleConstraint* Constraint(RigidBodyPhysicsComponent& rb)
        {
            return static_cast<JPH::VehicleConstraint*>(wi::physics::GetVehicleConstraint(rb));
        }

        XMFLOAT4 YawQuaternion(float yaw)
        {
            XMFLOAT4 q;
            XMStoreFloat4(&q, XMQuaternionRotationRollPitchYaw(0, yaw, 0));
            return q;
        }

        struct ChassisBox
        {
            float half_x, half_y, half_z, origin_height;
        };

        // Collision box between the wheel tops and the beltline. It must be at least as wide as the
        // tyres' outer faces: wheels only cast downward and have no side collision, so a tyre poking
        // past the box sinks into a wall, reads the wall as ground and climbs it (measured: 60 deg roll).
        ChassisBox ComputeChassis(const VehicleDefinition& d)
        {
            const float bottom = d.wheel_radius_m - 0.02f;
            const float top = d.height_m * 0.6f;
            const float tyre_outer = d.track_m * 0.5f + d.wheel_width_m * 0.5f + 0.02f;
            const float half_x = std::max(d.width_m * 0.41f, tyre_outer);
            // 86% of the length. The last ~30 cm of nose and tail have no collision, so two cars nose
            // to tail can sink into each other (owner's report, D-085). Lengthening this box was
            // measured and rejected: at 90-94% the AI clips barriers and E06/D05 gates fail every run.
            // The fix is a car-to-car-only bumper collider (pending, D-085).
            return { half_x, (top - bottom) * 0.5f, d.length_m * 0.43f, (top + bottom) * 0.5f };
        }
    }

    VehicleRuntime::VehicleRuntime(Scene& scene, const VehicleDefinition& d, XMFLOAT3 spawn, float yaw)
        : scene_(scene), definition_(d), assist_(d.steering)
    {
        // Our translation units see Jolt through public headers; a feature-flag mismatch
        // (e.g. JPH_DEBUG_RENDERER) would silently change class layouts.
        if (!JPH::VerifyJoltVersionID())
        {
            std::fprintf(stderr, "FATAL: Jolt ABI mismatch between game code and engine library\n");
            std::abort();
        }

        StabilityTuning stability;
        stability.drift_recovery_s = d.drift_recovery_s;
        stability_ = StabilityAssist(stability);

        const ChassisBox box = ComputeChassis(d);
        origin_height_ = box.origin_height;

        body_ = CreateEntity();
        scene.names.Create(body_) = "vehicle_" + d.id;
        TransformComponent& t = scene.transforms.Create(body_);
        t.Translate(XMFLOAT3(spawn.x, spawn.y + origin_height_, spawn.z));
        t.Rotate(YawQuaternion(yaw));
        t.UpdateTransform();

        RigidBodyPhysicsComponent& rb = scene.rigidbodies.Create(body_);
        rb.shape = RigidBodyPhysicsComponent::BOX;
        rb.box.halfextents = XMFLOAT3(box.half_x, box.half_y, box.half_z);
        rb.mass = d.mass_kg;
        rb.friction = 0.4f; // chassis-to-world contact (scrapes), not tyre grip
        rb.restitution = 0.05f;
        rb.damping_linear = 0.0f;
        rb.damping_angular = 0.05f;
        rb.SetDisableDeactivation(true);

        auto& v = rb.vehicle;
        v.type = RigidBodyPhysicsComponent::Vehicle::Type::Car;
        v.collision_mode = RigidBodyPhysicsComponent::Vehicle::CollisionMode::Cylinder;
        v.chassis_half_width = d.track_m * 0.5f;
        v.chassis_half_length = d.wheelbase_m * 0.5f;
        // Wicked uses this both as wheel attachment height and centre-of-mass offset; solve for com_height_m.
        v.chassis_half_height = d.com_height_m - box.origin_height + box.half_y;
        v.front_wheel_offset = 0.0f;
        v.rear_wheel_offset = 0.0f;
        v.wheel_radius = d.wheel_radius_m;
        v.wheel_width = d.wheel_width_m;
        v.max_engine_torque = d.max_engine_torque_nm;
        v.max_steering_angle = XMConvertToRadians(d.steering.max_angle_deg);
        v.front_suspension = { d.front_suspension.min_length_m, d.front_suspension.max_length_m, d.front_suspension.frequency_hz, d.front_suspension.damping };
        v.rear_suspension = { d.rear_suspension.min_length_m, d.rear_suspension.max_length_m, d.rear_suspension.frequency_hz, d.rear_suspension.damping };
        v.car.four_wheel_drive = d.drivetrain == Drivetrain::AWD;
    }

    RigidBodyPhysicsComponent& VehicleRuntime::RigidBody()
    {
        return *scene_.rigidbodies.GetComponent(body_);
    }

    void VehicleRuntime::SetWheelEntities(Entity fl, Entity fr, Entity rl, Entity rr)
    {
        auto& v = RigidBody().vehicle;
        v.wheel_entity_front_left = fl;
        v.wheel_entity_front_right = fr;
        v.wheel_entity_rear_left = rl;
        v.wheel_entity_rear_right = rr;
        // Authored wheel centre heights: the visual wheel never rises more than 1 cm above them,
        // so a hard landing cannot push a tyre up through the arch (owner report 2026-09-29).
        const Entity wheels[4] = { fl, fr, rl, rr };
        for (int i = 0; i < 4; ++i)
            if (const TransformComponent* t = wheels[i] != INVALID_ENTITY ? scene_.transforms.GetComponent(wheels[i]) : nullptr)
                wheel_rest_y_[i] = t->translation_local.y;
    }

    bool VehicleRuntime::ApplyTuning()
    {
        JPH::VehicleConstraint* vc = Constraint(RigidBody());
        if (vc == nullptr)
        {
            std::fprintf(stderr, "ERROR: vehicle %s has no physics constraint; run PhysicsStepper::Prepare() first\n", definition_.id.c_str());
            return false;
        }
        auto* controller = static_cast<JPH::WheeledVehicleController*>(vc->GetController());
        const VehicleDefinition& d = definition_;

        JPH::VehicleEngine& engine = controller->GetEngine();
        engine.mMaxTorque = d.max_engine_torque_nm;
        engine.mMinRPM = d.min_rpm;
        engine.mMaxRPM = d.max_rpm;
        engine.mInertia = d.engine_inertia_kgm2;
        engine.mNormalizedTorque.Clear();
        for (const TorquePoint& p : d.torque_curve)
            engine.mNormalizedTorque.AddPoint((p.rpm - d.min_rpm) / (d.max_rpm - d.min_rpm), p.normalized);
        engine.SetCurrentRPM(d.min_rpm);

        JPH::VehicleTransmission& gearbox = controller->GetTransmission();
        gearbox.mGearRatios.assign(d.gear_ratios.begin(), d.gear_ratios.end());
        gearbox.mReverseGearRatios = { d.reverse_ratio };
        gearbox.mShiftUpRPM = d.shift_up_rpm;
        gearbox.mShiftDownRPM = d.shift_down_rpm;
        gearbox.mSwitchTime = d.shift_time_s;
        gearbox.mClutchReleaseTime = d.shift_time_s * 0.6f;
        gearbox.mSwitchLatency = d.shift_time_s;

        // Wicked wires the single 2WD differential to wheels 0/1 (front); RWD moves it to the rear.
        auto& diffs = controller->GetDifferentials();
        if (d.drivetrain == Drivetrain::RWD)
        {
            diffs[0].mLeftWheel = 2;
            diffs[0].mRightWheel = 3;
        }
        for (auto& diff : diffs)
            diff.mDifferentialRatio = d.final_drive;

        // Wheel settings are shared-const in Jolt but were heap-allocated non-const by Wicked,
        // and are only read during the step, so adjusting them between steps is safe.
        for (JPH::uint i = 0; i < 4; ++i)
        {
            auto* settings = const_cast<JPH::WheelSettingsWV*>(static_cast<JPH::WheelWV*>(vc->GetWheel(i))->GetSettings());
            settings->mMaxBrakeTorque = d.brake_torque_nm;
            settings->mMaxHandBrakeTorque = i >= 2 ? d.handbrake_torque_nm : 0.0f;
        }

        // Wicked installs a 10x longitudinal impulse workaround for an old Jolt bug. With it the
        // tyres never saturate under braking, so surface grip had no effect (measured: identical
        // stopping distance on asphalt, wet and gravel). Restore Jolt's physical model.
        controller->SetTireMaxImpulseCallback([](JPH::uint, float& out_long, float& out_lat, float suspension_impulse, float long_friction, float lat_friction, float, float, float) {
            out_long = long_friction * suspension_impulse;
            out_lat = lat_friction * suspension_impulse;
        });

        // Surface grip is the static body's friction (see HandlingTrack.h); tyre grip scales on top.
        const float long_grip = d.tires.longitudinal_grip;
        const float lat_grip = d.tires.lateral_grip;
        // Wheels 2/3 are the rear pair (Wicked's wheel order FL, FR, RL, RR).
        vc->SetCombineFriction([this, long_grip, lat_grip](JPH::uint wheel, float& lon, float& lat, const JPH::Body& ground, const JPH::SubShapeID&) {
            const float puddle = wheel_grip_[wheel & 3];
            lon *= ground.GetFriction() * long_grip * puddle;
            lat *= ground.GetFriction() * lat_grip * puddle * (wheel >= 2 ? rear_lateral_grip_ : 1.0f);
        });
        tuned_ = true;
        return true;
    }

    void VehicleRuntime::PrePhysics(const VehicleCommand& command, float dt)
    {
        RigidBodyPhysicsComponent& rb = RigidBody();
        const float speed_kmh = wi::physics::GetVehicleForwardVelocity(rb) * 3.6f;
        last_input_ = assist_.Apply(command, speed_kmh, dt);
        wi::physics::DriveVehicle(rb, last_input_.forward, last_input_.steer, last_input_.brake, last_input_.handbrake);

        // Controlled drift: rear lateral grip drops quickly on the handbrake and returns over
        // drift_recovery_s, so letting go blends back into grip instead of snapping.
        const float target = 1.0f - (1.0f - definition_.handbrake_rear_grip) * last_input_.handbrake;
        const float rate = target < rear_lateral_grip_ ? dt / 0.1f : dt / definition_.drift_recovery_s;
        rear_lateral_grip_ += std::clamp(target - rear_lateral_grip_, -rate, rate);

        JPH::VehicleConstraint* vc = Constraint(rb);
        if (vc == nullptr)
            return;
        auto* controller = static_cast<JPH::WheeledVehicleController*>(vc->GetController());
        JPH::VehicleTransmission& gearbox = controller->GetTransmission();
        if (limiter_shift_enabled_ && !gearbox.IsSwitchingGear() &&
            limiter_shift_.Update(gearbox.GetCurrentGear(), int(definition_.gear_ratios.size()), controller->GetEngine().GetCurrentRPM(),
                                  definition_.shift_up_rpm, last_input_.forward, dt))
        {
            gearbox.Set(gearbox.GetCurrentGear() + 1, gearbox.GetClutchFriction());
        }

        if (!stability_enabled_)
            return;
        const JPH::Body* body = vc->GetVehicleBody();
        const JPH::Quat q = body->GetRotation();
        const JPH::Vec3 w_body = q.Conjugated() * body->GetAngularVelocity();
        const JPH::Vec3 fwd = q * JPH::Vec3::sAxisZ();
        const JPH::Vec3 right = q * JPH::Vec3::sAxisX();

        // StabilityAssist convention: +X accel = nose up, +Z accel = right side down. In Jolt's
        // rotation math a positive rotation about +X drops the nose and about +Z lifts the right
        // side, so both axes are negated here; yaw (+Y = turn right) matches.
        StabilityInput in;
        in.forward_speed_ms = speed_kmh / 3.6f;
        in.angular_velocity[0] = -w_body.GetX();
        in.angular_velocity[1] = w_body.GetY();
        in.angular_velocity[2] = -w_body.GetZ();
        in.pitch_rad = std::asin(std::clamp(fwd.GetY(), -1.0f, 1.0f));
        in.roll_rad = -std::asin(std::clamp(right.GetY(), -1.0f, 1.0f));
        in.steer = last_input_.steer;
        in.handbrake = last_input_.handbrake;
        in.airborne = !telemetry_.AnyWheelContact() && telemetry_.tick > 0;
        in.max_steer_rad = XMConvertToRadians(definition_.steering.max_angle_deg);
        in.wheelbase_m = definition_.wheelbase_m;

        float accel[3];
        stability_.Compute(in, dt, accel);
        if (accel[0] == 0.0f && accel[1] == 0.0f && accel[2] == 0.0f)
            return;
        const JPH::Vec3 dw_body(-accel[0] * dt, accel[1] * dt, -accel[2] * dt);
        const JPH::Vec3 w_world = body->GetAngularVelocity() + q * dw_body;
        wi::physics::SetAngularVelocity(rb, XMFLOAT3(w_world.GetX(), w_world.GetY(), w_world.GetZ()));
    }

    const VehicleTelemetry& VehicleRuntime::PostPhysics(uint64_t tick)
    {
        RigidBodyPhysicsComponent& rb = RigidBody();
        VehicleTelemetry& t = telemetry_;
        t.tick = tick;
        const XMFLOAT3 p = wi::physics::GetPosition(rb);
        const XMFLOAT3 v = wi::physics::GetVelocity(rb);
        const XMFLOAT4 q = wi::physics::GetRotation(rb);
        t.position[0] = p.x; t.position[1] = p.y; t.position[2] = p.z;
        t.velocity[0] = v.x; t.velocity[1] = v.y; t.velocity[2] = v.z;
        t.forward_speed_kmh = wi::physics::GetVehicleForwardVelocity(rb) * 3.6f;
        t.up_y = 1.0f - 2.0f * (q.x * q.x + q.z * q.z);

        {
            // Forward axis of the rotation: q * (0,0,1).
            const float fx = 2.0f * (q.x * q.z + q.w * q.y);
            const float fz = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
            t.heading_rad = std::atan2(fx, fz);
        }

        if (JPH::VehicleConstraint* vc = Constraint(rb))
        {
            t.yaw_rate = vc->GetVehicleBody()->GetAngularVelocity().GetY();
            auto* controller = static_cast<const JPH::WheeledVehicleController*>(vc->GetController());
            t.rpm = controller->GetEngine().GetCurrentRPM();
            t.gear = controller->GetTransmission().GetCurrentGear();
            for (JPH::uint i = 0; i < 4; ++i)
            {
                const auto* w = static_cast<const JPH::WheelWV*>(vc->GetWheel(i));
                t.wheel_longitudinal_slip[i] = w->mLongitudinalSlip;
                t.wheel_lateral_slip_rad[i] = w->mLateralSlip;
                t.wheel_contact[i] = w->HasContact();
            }
        }
        return t;
    }

    void VehicleRuntime::UpdateWheelVisuals(float visual_root_y)
    {
        RigidBodyPhysicsComponent& rb = RigidBody();
        JPH::VehicleConstraint* vc = Constraint(rb);
        if (vc == nullptr)
            return;
        const Entity wheels[4] = { rb.vehicle.wheel_entity_front_left, rb.vehicle.wheel_entity_front_right,
                                   rb.vehicle.wheel_entity_rear_left, rb.vehicle.wheel_entity_rear_right };
        for (JPH::uint i = 0; i < 4; ++i)
        {
            TransformComponent* t = wheels[i] != INVALID_ENTITY ? scene_.transforms.GetComponent(wheels[i]) : nullptr;
            if (t == nullptr)
                continue;
            // Pivots are authored with identity rotation (+Y up, +Z forward). Jolt builds the wheel
            // basis right-handed, where "right" for +Z forward / +Y up is -X; passing +X yielded a
            // 180 degree yaw (hubs facing inward), measured by the visual wheel test.
            const JPH::Mat44 m = vc->GetWheelLocalTransform(i, -JPH::Vec3::sAxisX(), JPH::Vec3::sAxisY());
            const JPH::Vec3 p = m.GetTranslation();
            const JPH::Quat q = m.GetQuaternion().Normalized();
            t->translation_local = XMFLOAT3(p.GetX(), std::min(p.GetY() - visual_root_y, wheel_rest_y_[i] + 0.01f), p.GetZ());
            t->rotation_local = XMFLOAT4(q.GetX(), q.GetY(), q.GetZ(), q.GetW());
            t->SetDirty();
        }
    }

    float VehicleRuntime::WheelCentreHeight()
    {
        JPH::VehicleConstraint* vc = Constraint(RigidBody());
        if (vc == nullptr)
            return -origin_height_ + definition_.wheel_radius_m;
        float sum = 0.0f;
        for (JPH::uint i = 0; i < 4; ++i)
            sum += vc->GetWheelLocalTransform(i, -JPH::Vec3::sAxisX(), JPH::Vec3::sAxisY()).GetTranslation().GetY();
        return sum / 4.0f;
    }

    void VehicleRuntime::Teleport(XMFLOAT3 ground, float yaw)
    {
        RigidBodyPhysicsComponent& rb = RigidBody();
        wi::physics::SetPositionAndRotation(rb, XMFLOAT3(ground.x, ground.y + origin_height_, ground.z), YawQuaternion(yaw));
        wi::physics::SetLinearVelocity(rb, XMFLOAT3(0, 0, 0));
        wi::physics::SetAngularVelocity(rb, XMFLOAT3(0, 0, 0));
    }
}
