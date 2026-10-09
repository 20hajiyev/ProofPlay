#include "racer/Damage.h"

#include <algorithm>
#include <cmath>

namespace racer
{
    namespace
    {
        float Smooth(float t)
        {
            t = std::clamp(t, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        // Value noise on a 0.22 m lattice: the lumpy crumple pattern (deterministic, no state).
        float Hash(int x, int y, int z)
        {
            uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + uint32_t(z) * 2147483647u;
            h = (h ^ (h >> 13)) * 1274126177u;
            return float((h ^ (h >> 16)) & 0xffff) / 65535.0f;
        }
        float Noise(float x, float y, float z)
        {
            constexpr float cell = 0.22f;
            x /= cell, y /= cell, z /= cell;
            const int ix = int(std::floor(x)), iy = int(std::floor(y)), iz = int(std::floor(z));
            const float fx = Smooth(x - float(ix)), fy = Smooth(y - float(iy)), fz = Smooth(z - float(iz));
            auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
            float c[2][2];
            for (int j = 0; j < 2; ++j)
                for (int k = 0; k < 2; ++k)
                    c[j][k] = lerp(Hash(ix, iy + j, iz + k), Hash(ix + 1, iy + j, iz + k), fx);
            return lerp(lerp(c[0][0], c[1][0], fy), lerp(c[0][1], c[1][1], fy), fz);
        }
    }

    DentZone ZoneOfHit(float dv_x, float dv_z)
    {
        if (std::fabs(dv_z) >= std::fabs(dv_x))
            return dv_z < 0.0f ? DentZone::Front : DentZone::Rear;
        return dv_x < 0.0f ? DentZone::Right : DentZone::Left;
    }

    void AddDent(DentState& s, DentZone z, float dv_kmh)
    {
        const float hit = std::clamp((dv_kmh - kDentFreeKmh) / (kDentFullKmh - kDentFreeKmh), 0.0f, 1.0f);
        float& d = s.depth[int(z)];
        d = std::min(1.0f, d + hit * (1.0f - d * 0.5f)); // a crumpled panel gives less with each hit
    }

    Point3 DeformPoint(Point3 p, const BodyBounds& b, const DentState& s)
    {
        const float crumple = 0.7f + 0.6f * Noise(p.x, p.y, p.z); // 0.7..1.3: no flat-pressed look
        const float len = b.max_z - b.min_z, mid_z = (b.max_z + b.min_z) * 0.5f;
        constexpr float kEndZone = 0.8f, kSideZone = 0.45f;
        Point3 q = p;
        if (const float d = s.Depth(DentZone::Front) * kDentMaxM; d > 0.0f)
        {
            const float w = Smooth((p.z - (b.max_z - kEndZone)) / kEndZone);
            q.z -= d * w * crumple;
            q.y += d * 0.25f * w * Smooth((p.y - 0.5f) / 0.4f) * crumple; // the bonnet buckles up
        }
        if (const float d = s.Depth(DentZone::Rear) * kDentMaxM; d > 0.0f)
        {
            const float w = Smooth(((b.min_z + kEndZone) - p.z) / kEndZone);
            q.z += d * w * crumple;
            q.y += d * 0.2f * w * Smooth((p.y - 0.5f) / 0.4f) * crumple;
        }
        // side dents are deepest mid-car, fading towards the wheels' arches and the ends
        const float along = std::max(0.0f, 1.0f - std::fabs(p.z - mid_z) / (len * 0.45f));
        if (const float d = s.Depth(DentZone::Right) * kDentMaxM; d > 0.0f)
            q.x -= d * Smooth((p.x - (b.max_x - kSideZone)) / kSideZone) * Smooth(along) * crumple;
        if (const float d = s.Depth(DentZone::Left) * kDentMaxM; d > 0.0f)
            q.x += d * Smooth(((b.min_x + kSideZone) - p.x) / kSideZone) * Smooth(along) * crumple;
        return q;
    }

    float SmokeAmount(float health_fraction)
    {
        return Smooth((kSmokeStartHp - health_fraction) / (kSmokeStartHp - kSmokeFullHp));
    }
}
