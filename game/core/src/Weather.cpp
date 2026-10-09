#include "racer/Weather.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace racer
{
    namespace
    {
        // The full-intensity weathers (plan table): rain grip x0.85, storm x0.8 with side wind, fog
        // hides the distance but leaves the road dry.
        WeatherEffects Full(WeatherKind k)
        {
            WeatherEffects e;
            switch (k)
            {
            case WeatherKind::Clear: break;
            case WeatherKind::Overcast: e.visibility_m = 1400.0f; break;
            case WeatherKind::Rain: e.grip = 0.85f; e.wetness = 1.0f; e.visibility_m = 700.0f; break;
            case WeatherKind::Storm: e.grip = 0.8f; e.wetness = 1.0f; e.visibility_m = 450.0f; e.wind_ms = 9.0f; break;
            case WeatherKind::Fog: e.visibility_m = 140.0f; break;
            }
            return e;
        }

        float Lerp(float a, float b, float t) { return a + (b - a) * t; }

        WeatherEffects Mix(const WeatherEffects& a, const WeatherEffects& b, float t)
        {
            if (t <= 0.0f)
                return a;
            if (t >= 1.0f)
                return b;
            return { Lerp(a.grip, b.grip, t), Lerp(a.wetness, b.wetness, t), Lerp(a.visibility_m, b.visibility_m, t), Lerp(a.wind_ms, b.wind_ms, t) };
        }
    }

    WeatherEffects EffectsOf(const WeatherState& w)
    {
        return Mix(WeatherEffects{}, Full(w.kind), std::clamp(w.intensity, 0.0f, 1.0f));
    }

    WeatherEffects BlendEffects(const WeatherEffects& a, const WeatherEffects& b, float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return Mix(a, b, t * t * (3.0f - 2.0f * t));
    }

    VehicleDefinition ApplyWeather(const VehicleDefinition& base, const WeatherEffects& fx)
    {
        VehicleDefinition d = base;
        d.tires.longitudinal_grip *= fx.grip;
        d.tires.lateral_grip *= fx.grip;
        return d;
    }

    std::vector<Puddle> MakePuddles(const TrackRoute& route, float wetness, uint32_t seed)
    {
        std::vector<Puddle> out;
        const float L = route.Length(), hw = route.HalfWidth();
        if (wetness <= 0.0f || L <= 0.0f || hw <= 0.0f)
            return out;
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        const int n = int(std::round(kPuddlesPerKm * L / 1000.0f * std::clamp(wetness, 0.0f, 1.0f)));
        const float clear_from = L - 40.0f, clear_to = 160.0f; // the grid and the start straight stay dry
        for (int i = 0; i < n; ++i)
        {
            // even spacing with jitter, so no stretch is flooded and none is all dry
            float s = (float(i) + 0.15f + 0.7f * u(rng)) * L / float(n);
            if (s > clear_from || s < clear_to)
                continue;
            const float side = u(rng) < 0.5f ? -1.0f : 1.0f;
            const float lateral = side * hw * (0.45f + 0.4f * u(rng)); // the outer half of the road
            const Vec2 c = route.PointAt(s), d = route.DirectionAt(s);
            Puddle p;
            p.centre = { c.x + d.z * lateral, c.z - d.x * lateral }; // right of travel = (dz, -dx)
            // 1.2-3.2 m (smaller ones vanished at race speed - measured), but the rim never reaches the
            // centreline: the racing line stays dry
            p.radius = std::min(1.2f + 2.0f * u(rng), std::fabs(lateral) * 0.85f);
            p.depth = (0.5f + 0.5f * u(rng)) * std::clamp(wetness, 0.0f, 1.0f);
            out.push_back(p);
        }
        return out;
    }

    float PuddleGrip(const std::vector<Puddle>& puddles, Vec2 p)
    {
        float g = 1.0f;
        for (const Puddle& q : puddles)
        {
            const float dx = p.x - q.centre.x, dz = p.z - q.centre.z;
            const float r2 = dx * dx + dz * dz;
            if (r2 >= q.radius * q.radius)
                continue;
            const float k = 1.0f - std::sqrt(r2) / q.radius; // 1 at the centre, 0 at the rim
            g = std::min(g, 1.0f - (1.0f - kPuddleGrip) * q.depth * k);
        }
        return g;
    }

    const char* ToString(WeatherKind k)
    {
        switch (k)
        {
        case WeatherKind::Clear: return "clear";
        case WeatherKind::Overcast: return "overcast";
        case WeatherKind::Rain: return "rain";
        case WeatherKind::Storm: return "storm";
        case WeatherKind::Fog: return "fog";
        }
        return "clear";
    }

    bool ParseWeatherKind(const std::string& text, WeatherKind& out)
    {
        for (WeatherKind k : { WeatherKind::Clear, WeatherKind::Overcast, WeatherKind::Rain, WeatherKind::Storm, WeatherKind::Fog })
            if (text == ToString(k))
            {
                out = k;
                return true;
            }
        return false;
    }
}
