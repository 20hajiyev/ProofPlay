#include "racer/Contact.h"

#include <cmath>

namespace racer
{
    namespace
    {
        // Half the projected extent of a footprint on a unit axis.
        float Reach(const Footprint& f, Vec2 axis)
        {
            const Vec2 fwd = { std::sin(f.heading), std::cos(f.heading) }, right = { fwd.z, -fwd.x };
            return f.half_len * std::fabs(axis.x * fwd.x + axis.z * fwd.z) + f.half_wid * std::fabs(axis.x * right.x + axis.z * right.z);
        }
    }

    Overlap FootprintOverlap(const Footprint& a, const Footprint& b)
    {
        Overlap best;
        best.depth = 1e9f;
        const Vec2 d = { b.centre.x - a.centre.x, b.centre.z - a.centre.z };
        const Footprint* fs[2] = { &a, &b };
        for (const Footprint* f : fs)
        {
            const Vec2 fwd = { std::sin(f->heading), std::cos(f->heading) };
            for (Vec2 axis : { fwd, Vec2{ fwd.z, -fwd.x } })
            {
                const float dist = d.x * axis.x + d.z * axis.z;
                const float pen = Reach(a, axis) + Reach(b, axis) - std::fabs(dist);
                if (pen <= 0.0f)
                    return {}; // a separating axis: no overlap
                if (pen < best.depth)
                {
                    best.depth = pen;
                    best.normal = dist >= 0.0f ? axis : Vec2{ -axis.x, -axis.z };
                }
            }
        }
        best.hit = true;
        return best;
    }
}
