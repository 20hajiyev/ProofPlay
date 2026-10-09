#include "racer/runtime/PadRace.h"

#include <cmath>

namespace racer::runtime
{
    TrackRoute MakePadRoute()
    {
        const float x0 = -30, z0 = 900, w = 100, h = 600, r = 30;
        std::vector<Vec2> pts;
        auto arc = [&](float cx, float cz, float a0) {
            for (int i = 0; i <= 10; ++i)
            {
                const float a = a0 - i * (XM_PIDIV2 / 10);
                pts.push_back({ x0 + cx + r * std::cos(a), z0 + cz + r * std::sin(a) });
            }
        };
        pts.push_back({ x0, z0 + 150 });
        arc(r, h - r, XM_PI);
        arc(w - r, h - r, XM_PIDIV2);
        arc(w - r, r, 0.0f);
        arc(r, r, -XM_PIDIV2);
        return TrackRoute(pts, 7.0f);
    }

    RaceSetup MakePadRaceSetup(const std::vector<VehicleDefinition>& cars, int laps)
    {
        RaceSetup s;
        s.route = MakePadRoute();
        s.race.laps = laps;
        s.race.checkpoints_s = { 0, s.route.Length() * 0.25f, s.route.Length() * 0.5f, s.route.Length() * 0.75f };
        for (size_t i = 0; i < cars.size(); ++i)
        {
            s.cars.push_back(cars[i]);
            s.grid.push_back({ XMFLOAT3(-30 + (i % 2 ? 2.5f : -2.5f), 0, 900 + 140 - (i / 2) * 9.0f - (i % 2) * 4.5f), 0.0f });
        }
        int k = 0;
        for (float along = 60; along < s.route.Length() - 30; along += 150)
        {
            const Vec2 p = s.route.PointAt(along);
            const Vec2 d = s.route.DirectionAt(along);
            for (float lat : { -4.0f, 0.0f, 4.0f })
                s.pickups.push_back({ { p.x + d.z * lat, p.z - d.x * lat }, Ability(1 + (k++ % 8)) });
        }
        return s;
    }
}
