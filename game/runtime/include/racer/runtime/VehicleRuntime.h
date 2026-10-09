#pragma once
#include "racer/StabilityAssist.h"
#include "racer/Vehicle.h"
#include "wiScene.h"

namespace racer::runtime
{
    // Owns one car's physics entity. Game rules never touch Jolt directly; they go through this.
    class VehicleRuntime
    {
    public:
        VehicleRuntime(wi::scene::Scene& scene, const VehicleDefinition& definition, XMFLOAT3 spawn_ground, float yaw_rad);
        // The Jolt friction callback captures this object; it must never be copied or moved.
        VehicleRuntime(const VehicleRuntime&) = delete;
        VehicleRuntime& operator=(const VehicleRuntime&) = delete;

        // Must run after PhysicsStepper::Prepare() created the Jolt constraint; false if it did not.
        [[nodiscard]] bool ApplyTuning();

        // Per authoritative step, before PhysicsStepper::Step().
        void PrePhysics(const VehicleCommand& command, float dt);
        // Per-wheel surface grip on top of the ground's friction (D-084 puddles), FL FR RL RR.
        void SetWheelGrip(int wheel, float k) { wheel_grip_[wheel & 3] = k; }
        // Per authoritative step, after PhysicsStepper::Step().
        const VehicleTelemetry& PostPhysics(uint64_t tick);

        // Places the car upright on the ground point with zero velocity.
        void Teleport(XMFLOAT3 ground_point, float yaw_rad);

        void SetWheelEntities(wi::ecs::Entity fl, wi::ecs::Entity fr, wi::ecs::Entity rl, wi::ecs::Entity rr);

        // Poses the visual wheel pivots from the Jolt constraint (suspension, steer, spin) in the
        // body's local space. Wicked's own override is gated on physics being enabled, which it
        // never is during Scene::Update in our stepping model. Call once per frame before it.
        // visual_root_y is the pivots' parent offset relative to the body origin.
        void UpdateWheelVisuals(float visual_root_y);
        // Mean wheel-centre height relative to the body origin as the suspension holds it now
        // (call at rest): imported models are aligned to it so their arches sit around the wheels.
        float WheelCentreHeight();

        // Yaw/air assist (plan 2.6). Only removes unrequested rotation; tests compare on/off.
        void SetStabilityEnabled(bool enabled) { stability_enabled_ = enabled; }
        void SetLimiterShiftEnabled(bool enabled) { limiter_shift_enabled_ = enabled; }

        wi::ecs::Entity Body() const { return body_; }
        const VehicleDefinition& Definition() const { return definition_; }
        const VehicleTelemetry& Telemetry() const { return telemetry_; }
        const DriverInput& LastInput() const { return last_input_; }
        bool IsTuned() const { return tuned_; }
        float BodyOriginHeight() const { return origin_height_; }

    private:
        wi::scene::RigidBodyPhysicsComponent& RigidBody();

        wi::scene::Scene& scene_;
        VehicleDefinition definition_;
        DriveAssist assist_;
        StabilityAssist stability_;
        bool stability_enabled_ = true;
        float rear_lateral_grip_ = 1.0f; // read by the friction callback during the step
        float wheel_grip_[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        LimiterShiftAssist limiter_shift_;
        bool limiter_shift_enabled_ = true;
        wi::ecs::Entity body_ = wi::ecs::INVALID_ENTITY;
        float origin_height_ = 0.0f;
        float wheel_rest_y_[4] = { 1e9f, 1e9f, 1e9f, 1e9f }; // authored pivot heights (visual travel clamp)
        bool tuned_ = false;
        DriverInput last_input_;
        VehicleTelemetry telemetry_;
    };
}
