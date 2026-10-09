#pragma once
#include "racer/Race.h"
#include "racer/Vehicle.h"

#include <string>

namespace racer
{
    // Weather (D-083, design/weather_plan.md). Engine-free rules: what a weather does to driving and
    // seeing. Every car in a race gets the same effects (the AI too), and nothing here is random, so
    // a race with the same seed and weather replays identically.
    enum class WeatherKind { Clear, Overcast, Rain, Storm, Fog };

    struct WeatherState
    {
        WeatherKind kind = WeatherKind::Clear;
        float intensity = 1.0f; // 0 = as clear, 1 = the full weather
    };

    struct WeatherEffects
    {
        float grip = 1.0f;            // x tyre grip, both directions (braking distance follows)
        float wetness = 0.0f;         // 0..1: wet road look, spray, rain sound
        float visibility_m = 2000.0f; // fog distance for the renderer
        float wind_ms = 0.0f;         // gusting side wind for the storm
    };

    WeatherEffects EffectsOf(const WeatherState& w);
    // A transition between two weathers, t 0..1 with a smoothstep ease.
    WeatherEffects BlendEffects(const WeatherEffects& a, const WeatherEffects& b, float t);
    VehicleDefinition ApplyWeather(const VehicleDefinition& base, const WeatherEffects& fx);

    // Puddles (D-084): standing water a wet road collects. Placed like a real crowned road drains -
    // in the outer part of each lane, the gutters - so the racing line stays mostly clear and going
    // wide to pass is the risk. Deterministic from the seed; more with the wetness; none on the grid
    // straight. A wheel in one loses grip towards the centre (a light aquaplane), the same for all.
    struct Puddle
    {
        Vec2 centre;
        float radius = 1.0f; // m
        float depth = 1.0f;  // 0..1: the grip loss at the centre scales with it
    };
    constexpr float kPuddleGrip = 0.8f;      // grip factor at the centre of the deepest puddle
    constexpr float kPuddlesPerKm = 48.0f;   // at full wetness
    std::vector<Puddle> MakePuddles(const TrackRoute& route, float wetness, uint32_t seed);
    float PuddleGrip(const std::vector<Puddle>& puddles, Vec2 p); // 1 on a dry spot

    const char* ToString(WeatherKind k);
    bool ParseWeatherKind(const std::string& text, WeatherKind& out);
}
