#pragma once
#include "wiScene.h"

namespace racer::runtime
{
    // Relative tyre grip per surface (plan 2.6); stored as the static body's friction.
    namespace surface
    {
        constexpr float kDryAsphalt = 1.00f;
        constexpr float kWetAsphalt = 0.82f;
        constexpr float kConcrete = 0.95f;
        constexpr float kCobble = 0.88f;
        constexpr float kDirt = 0.72f;
        constexpr float kGravel = 0.65f;
    }

    // Greybox tuning ground (plan 2.6 "Tuning meydançası"). World space: +Y up, +Z forward.
    struct HandlingTrackLayout
    {
        // Long asphalt straight along +Z centred on x = 0, used for acceleration/top speed/braking.
        static constexpr float kStraightStartZ = -100.0f;
        static constexpr float kStraightEndZ = 3400.0f;
        // Surface lanes parallel to the straight.
        static constexpr float kGravelLaneX = 120.0f;
        static constexpr float kWetLaneX = 180.0f;
        static constexpr float kLaneHalfWidth = 25.0f;
        // Features on the open asphalt pad (x < -40).
        static constexpr float kRampX = -80.0f, kRampZ = 300.0f;
        static constexpr float kCurbX = -140.0f, kCurbZ0 = 100.0f;
        static constexpr float kWallX = -200.0f, kWallZ0 = 0.0f, kWallZ1 = 800.0f;
    };

    // with_visuals adds cube meshes (needs a graphics device); physics-only otherwise.
    void BuildHandlingTrack(wi::scene::Scene& scene, bool with_visuals);
}
