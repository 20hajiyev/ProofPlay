#include "racer/runtime/PhysicsStepper.h"

#include "wiJobSystem.h"
#include "wiPhysics.h"

namespace racer::runtime
{
    // Resting state between our steps: physics disabled so Scene::Update skips
    // RunPhysicsUpdateSystem entirely. With simulation disabled but physics enabled, Wicked
    // treats the scene as editor-paused and teleports every body back to its (stale) transform,
    // which pinned the car at its spawn point while its velocity kept growing. Simulation stays
    // flagged on so Scene::Update still drives the visual wheels from the Jolt constraint.
    static void SetResting()
    {
        wi::physics::SetEnabled(false);
        wi::physics::SetSimulationEnabled(true);
    }

    void InitializePhysicsRuntime()
    {
        wi::physics::Initialize();
        wi::physics::SetFrameRate(1.0f / PhysicsStepper::kStepSeconds);
        wi::physics::SetAccuracy(1);
        wi::physics::SetInterpolationEnabled(false);
        SetResting();
    }

    PhysicsStepper::PhysicsStepper(wi::scene::Scene& scene) : scene_(scene) {}

    PhysicsStepper::~PhysicsStepper()
    {
        SetResting();
    }

    void PhysicsStepper::Prepare()
    {
        Run(false);
    }

    void PhysicsStepper::Step()
    {
        Run(true);
    }

    void PhysicsStepper::Run(bool simulate)
    {
        wi::physics::SetEnabled(true);
        wi::physics::SetSimulationEnabled(simulate);
        wi::jobsystem::context ctx;
        // Twice the step: Wicked clamps its accumulator to one step (accuracy 1), so this
        // yields exactly one step and a zero remainder regardless of float rounding.
        // dt must be > 0 even for Prepare(): Wicked returns before creating bodies when dt <= 0;
        // with simulation disabled the accumulator is untouched and nothing advances.
        wi::physics::RunPhysicsUpdateSystem(ctx, scene_, 2.0f * kStepSeconds);
        wi::jobsystem::Wait(ctx);
        SetResting();
    }
}
