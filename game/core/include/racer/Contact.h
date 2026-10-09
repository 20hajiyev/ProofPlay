#pragma once
#include "racer/Race.h"

namespace racer
{
    // Car-to-car bumpers (D-088). The physics chassis box is 86% of a car's length (longer boxes make
    // the AI clip barriers - measured in D-085), so two cars nose to tail could sink up to 60 cm into
    // each other. Between cars only, their full visual footprints are kept apart: walls never see this.
    struct Footprint
    {
        Vec2 centre;
        float heading = 0.0f;   // atan2(forward.x, forward.z)
        float half_len = 2.0f;  // m, along forward
        float half_wid = 0.9f;  // m, across
    };

    struct Overlap
    {
        bool hit = false;
        Vec2 normal;            // unit, from a towards b
        float depth = 0.0f;     // m along normal to separate them
    };

    // Separating-axis test of two oriented rectangles: the axis of least penetration.
    Overlap FootprintOverlap(const Footprint& a, const Footprint& b);
}
