#include "TestHarness.h"
#include "racer/Damage.h"

#include <cmath>
#include <cstdio>

using namespace racer;

// ---- visible damage (D-085) ---------------------------------------------------------------------
TEST_CASE("damage: the hit's direction picks the zone")
{
    CHECK(ZoneOfHit(0.0f, -20.0f) == DentZone::Front); // thrown back: hit ahead
    CHECK(ZoneOfHit(0.0f, 20.0f) == DentZone::Rear);   // shoved forward: hit from behind
    CHECK(ZoneOfHit(-15.0f, 3.0f) == DentZone::Right); // pushed left: hit on the right
    CHECK(ZoneOfHit(15.0f, -3.0f) == DentZone::Left);
}

TEST_CASE("damage: taps leave no dent, harder hits deeper, never past the limit")
{
    DentState s;
    AddDent(s, DentZone::Front, kDentFreeKmh - 1.0f);
    CHECK(s.Depth(DentZone::Front) == 0.0f);
    AddDent(s, DentZone::Front, 40.0f);
    const float one = s.Depth(DentZone::Front);
    CHECK(one > 0.0f && one < 1.0f);
    AddDent(s, DentZone::Front, 40.0f);
    const float two = s.Depth(DentZone::Front);
    CHECK(two > one);
    CHECK(two - one < one); // diminishing returns: the second identical hit adds less
    for (int i = 0; i < 50; ++i)
        AddDent(s, DentZone::Front, 200.0f);
    CHECK(s.Depth(DentZone::Front) <= 1.0f);
    CHECK(s.Depth(DentZone::Rear) == 0.0f && s.Depth(DentZone::Left) == 0.0f);
    DentState hard;
    AddDent(hard, DentZone::Rear, kDentFullKmh);
    CHECK(std::fabs(hard.Depth(DentZone::Rear) - 1.0f) < 1e-5f);
    std::printf("  INFO front dent after 40 km/h: %.2f (%.0f cm), after two: %.2f\n", one, one * kDentMaxM * 100, two);
}

TEST_CASE("damage: deformation is identity undamaged, local to the hit zone, bounded")
{
    const BodyBounds b = { -0.9f, 0.9f, -2.2f, 2.2f };
    DentState none;
    const Point3 nose = { 0.1f, 0.6f, 2.15f }, tail = { 0.1f, 0.6f, -2.15f }, door = { 0.88f, 0.6f, 0.0f };
    const Point3 n0 = DeformPoint(nose, b, none);
    CHECK(n0.x == nose.x && n0.y == nose.y && n0.z == nose.z);
    DentState front;
    front.depth[int(DentZone::Front)] = 1.0f;
    const Point3 n1 = DeformPoint(nose, b, front), t1 = DeformPoint(tail, b, front), d1 = DeformPoint(door, b, front);
    std::printf("  INFO full front dent moves the nose %.2f m back; the tail %.3f m\n", nose.z - n1.z, std::fabs(tail.z - t1.z));
    CHECK(nose.z - n1.z > kDentMaxM * 0.6f);              // the nose goes in
    CHECK(nose.z - n1.z <= kDentMaxM * 1.3f + 1e-5f);      // but no further than the limit allows
    CHECK(t1.z == tail.z && t1.x == tail.x);              // the tail is untouched
    CHECK(std::fabs(d1.x - door.x) < 1e-5f);              // the door (mid-car) too
    DentState side;
    side.depth[int(DentZone::Right)] = 1.0f;
    const Point3 d2 = DeformPoint(door, b, side);
    CHECK(door.x - d2.x > kDentMaxM * 0.5f);              // the right door goes in (towards -x)
    const Point3 left = { -0.88f, 0.6f, 0.0f };
    CHECK(DeformPoint(left, b, side).x == left.x);        // the left one stays
}

TEST_CASE("damage: smoke only below 15% health, light, thickest near a wreck")
{
    CHECK(SmokeAmount(1.0f) == 0.0f);
    CHECK(SmokeAmount(0.5f) == 0.0f);
    CHECK(SmokeAmount(kSmokeStartHp + 0.01f) == 0.0f);
    const float mid = SmokeAmount(0.1f), low = SmokeAmount(0.02f);
    CHECK(mid > 0.0f && mid < 1.0f);
    CHECK(low == 1.0f);
    CHECK(SmokeAmount(0.0f) == 1.0f);
}
