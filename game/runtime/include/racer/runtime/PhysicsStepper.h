#pragma once
#include "wiScene.h"

namespace racer::runtime
{
    // The only place authoritative physics advances. Wicked's own accumulator is neutralised
    // (accuracy 1, dt clamped to one step) so each Step() is exactly one Jolt step, and
    // Scene::Update() never runs physics because it is disabled outside Step()/Prepare().
    class PhysicsStepper
    {
    public:
        static constexpr float kStepSeconds = 1.0f / 120.0f;

        explicit PhysicsStepper(wi::scene::Scene& scene);
        ~PhysicsStepper();

        // Creates bodies/constraints for newly added components without advancing time.
        void Prepare();
        void Step();

    private:
        void Run(bool simulate);
        wi::scene::Scene& scene_;
    };

    // Call once per process before any PhysicsStepper.
    void InitializePhysicsRuntime();
}
