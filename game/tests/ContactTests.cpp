#include "TestHarness.h"
#include "racer/Contact.h"

#include <cmath>

using namespace racer;

// ---- car-to-car bumpers (D-088) -----------------------------------------------------------------
TEST_CASE("bumpers: nose to tail overlap pushes along the travel axis, apart cars do not touch")
{
    const Footprint a = { { 0, 0 }, 0.0f, 2.2f, 0.9f };           // heading +z
    Footprint b = { { 0.2f, 4.0f }, 0.0f, 2.2f, 0.9f };          // 4 m ahead: nose 0.4 m into a's tail... a's nose into b's tail
    const Overlap o = FootprintOverlap(a, b);
    CHECK(o.hit);
    CHECK(std::fabs(o.depth - 0.4f) < 1e-4f);
    CHECK(o.normal.z > 0.99f);                                   // from a to b: forward
    b.centre.z = 4.5f;
    CHECK(!FootprintOverlap(a, b).hit);
}

TEST_CASE("bumpers: side by side overlap pushes sideways; normal points from a to b")
{
    const Footprint a = { { 0, 0 }, 0.0f, 2.2f, 0.9f };
    const Footprint b = { { -1.6f, 0.5f }, 0.0f, 2.2f, 0.9f };   // 0.2 m into a's left side
    const Overlap o = FootprintOverlap(a, b);
    CHECK(o.hit);
    CHECK(std::fabs(o.depth - 0.2f) < 1e-4f);
    CHECK(o.normal.x < -0.99f);
}

TEST_CASE("bumpers: rotated footprints use their own axes (a car crossed at 90 degrees)")
{
    const Footprint a = { { 0, 0 }, 0.0f, 2.2f, 0.9f };
    const Footprint b = { { 0, 2.9f }, 1.5707963f, 2.2f, 0.9f }; // T-bone: b's side 0.2 m into a's nose
    const Overlap o = FootprintOverlap(a, b);
    CHECK(o.hit);
    CHECK(std::fabs(o.depth - 0.2f) < 1e-3f);
    CHECK(o.normal.z > 0.99f);
    const Footprint c = { { 0, 3.2f }, 1.5707963f, 2.2f, 0.9f };
    CHECK(!FootprintOverlap(a, c).hit);
}
