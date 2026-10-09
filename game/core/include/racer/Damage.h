#pragma once
#include <cstdint>

namespace racer
{
    // Visible crash damage (D-085, owner: "when a car hits a wall its front should crumple; control the
    // degree of the dents"). Engine-free rules: where a hit lands, how deep the dent gets, and how a
    // body point moves under it; the renderer only applies DeformPoint to the car's mesh. Damage is
    // cosmetic: health and handling come from the combat rules as before.
    enum class DentZone : uint8_t { Front, Rear, Left, Right };

    constexpr float kDentFreeKmh = 15.0f;  // a nudge below this leaves no mark
    constexpr float kDentFullKmh = 90.0f;  // a hit this hard takes a fresh panel to the limit at once
    constexpr float kDentMaxM = 0.28f;     // the deepest a zone ever goes in (the degree control; 20 cm barely read)
    constexpr float kSmokeStartHp = 0.15f; // engine smoke only below 15% health...
    constexpr float kSmokeFullHp = 0.04f;  // ...and at its thickest (still light) near a wreck

    struct DentState
    {
        float depth[4] = {}; // 0..1 per zone; x kDentMaxM metres
        float Depth(DentZone z) const { return depth[int(z)]; }
    };

    // The zone a hit lands on, from the car's velocity change in its own frame (x right, z forward):
    // a car stopped by a wall ahead is thrown back (dv.z < 0) - a front hit.
    DentZone ZoneOfHit(float dv_x, float dv_z);
    // Adds a hit of |dv| km/h. Each hit pushes the panel further in, with diminishing returns as it
    // nears the limit (crumpled metal resists); never past 1.
    void AddDent(DentState& s, DentZone z, float dv_kmh);

    struct BodyBounds
    {
        float min_x, max_x, min_z, max_z;
    };
    struct Point3
    {
        float x, y, z;
    };
    // Where a point of the undamaged body ends up: pushed in from the dented side, falling off over
    // the crumple zone (0.8 m at the ends, 0.45 m at the sides), with a lumpy crumple pattern and the
    // bonnet/bootlid buckling up a little. Identity with no dents. Never moves a point more than
    // 1.3 x kDentMaxM.
    Point3 DeformPoint(Point3 p, const BodyBounds& b, const DentState& s);
    // Engine smoke, 0 (none) .. 1 (the most there is), from the health fraction.
    float SmokeAmount(float health_fraction);
}
