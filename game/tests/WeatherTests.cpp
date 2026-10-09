#include "TestHarness.h"
#include "racer/Weather.h"

#include <cmath>
#include <cstdio>

using namespace racer;

// ---- weather (D-083, design/weather_plan.md step 1) ---------------------------------------------
TEST_CASE("weather: clear changes nothing; rain and storm cut grip, storm most; fog only hides")
{
    const WeatherEffects clear = EffectsOf({ WeatherKind::Clear, 1.0f });
    CHECK(clear.grip == 1.0f && clear.wetness == 0.0f && clear.wind_ms == 0.0f);
    const WeatherEffects rain = EffectsOf({ WeatherKind::Rain, 1.0f });
    const WeatherEffects storm = EffectsOf({ WeatherKind::Storm, 1.0f });
    const WeatherEffects fog = EffectsOf({ WeatherKind::Fog, 1.0f });
    CHECK(std::fabs(rain.grip - 0.85f) < 1e-4f); // plan: grip x0.85 in full rain
    CHECK(std::fabs(storm.grip - 0.8f) < 1e-4f);
    CHECK(storm.wind_ms > 0.0f && rain.wind_ms == 0.0f);
    CHECK(fog.grip == 1.0f && fog.visibility_m < clear.visibility_m);
    CHECK(rain.wetness == 1.0f && storm.wetness == 1.0f && fog.wetness == 0.0f);
}

TEST_CASE("weather: intensity scales the effect from clear (0) to the full weather (1)")
{
    const WeatherEffects half = EffectsOf({ WeatherKind::Rain, 0.5f });
    CHECK(std::fabs(half.grip - 0.925f) < 1e-4f);
    CHECK(std::fabs(half.wetness - 0.5f) < 1e-4f);
    const WeatherEffects none = EffectsOf({ WeatherKind::Rain, 0.0f });
    CHECK(none.grip == 1.0f);
    const WeatherEffects over = EffectsOf({ WeatherKind::Rain, 3.0f }); // clamped
    CHECK(std::fabs(over.grip - 0.85f) < 1e-4f);
}

TEST_CASE("weather: blending two weathers is smooth and stays between them")
{
    const WeatherEffects a = EffectsOf({ WeatherKind::Clear, 1.0f });
    const WeatherEffects b = EffectsOf({ WeatherKind::Storm, 1.0f });
    float last = a.grip;
    for (int k = 0; k <= 20; ++k)
    {
        const WeatherEffects m = BlendEffects(a, b, k / 20.0f);
        CHECK(m.grip <= last + 1e-6f);                 // monotonic towards the storm
        CHECK(m.grip >= b.grip - 1e-6f && m.grip <= a.grip + 1e-6f);
        last = m.grip;
    }
    CHECK(BlendEffects(a, b, 0.0f).grip == a.grip);
    CHECK(BlendEffects(a, b, 1.0f).grip == b.grip);
    // smoothstep: the first and last 10% of the transition change less than the middle 10%
    const float edge = BlendEffects(a, b, 0.1f).grip - a.grip;
    const float mid = BlendEffects(a, b, 0.55f).grip - BlendEffects(a, b, 0.45f).grip;
    CHECK(std::fabs(edge) < std::fabs(mid));
}

TEST_CASE("weather: ApplyWeather scales tyre grip only, the same for every car")
{
    VehicleDefinition d;
    d.tires.longitudinal_grip = 1.0f;
    d.tires.lateral_grip = 0.96f;
    d.brake_torque_nm = 1800.0f;
    d.max_engine_torque_nm = 178.0f;
    const VehicleDefinition wet = ApplyWeather(d, EffectsOf({ WeatherKind::Rain, 1.0f }));
    CHECK(std::fabs(wet.tires.longitudinal_grip - 0.85f) < 1e-4f);
    CHECK(std::fabs(wet.tires.lateral_grip - 0.816f) < 1e-4f);
    CHECK(wet.brake_torque_nm == d.brake_torque_nm);      // stopping is longer through grip, not brakes
    CHECK(wet.max_engine_torque_nm == d.max_engine_torque_nm);
    const VehicleDefinition dry = ApplyWeather(d, EffectsOf({ WeatherKind::Clear, 1.0f }));
    CHECK(dry.tires.lateral_grip == d.tires.lateral_grip);
}

TEST_CASE("weather: names parse and print; unknown names are rejected")
{
    for (WeatherKind k : { WeatherKind::Clear, WeatherKind::Overcast, WeatherKind::Rain, WeatherKind::Storm, WeatherKind::Fog })
    {
        WeatherKind back = WeatherKind::Clear;
        CHECK(ParseWeatherKind(ToString(k), back));
        CHECK(back == k);
    }
    WeatherKind w = WeatherKind::Rain;
    CHECK(!ParseWeatherKind("snow", w));
    CHECK(w == WeatherKind::Rain); // untouched on failure
}

// ---- puddles (D-084) -----------------------------------------------------------------------------
namespace
{
    TrackRoute Oval()
    {
        std::vector<Vec2> pts;
        for (int i = 0; i < 64; ++i)
        {
            const float a = 6.2831853f * float(i) / 64.0f;
            pts.push_back({ 300.0f * std::cos(a), 180.0f * std::sin(a) });
        }
        return TrackRoute(pts, 7.0f);
    }
}

TEST_CASE("puddles: more with the wetness, none when dry, same seed same puddles")
{
    const TrackRoute r = Oval();
    CHECK(MakePuddles(r, 0.0f, 1).empty());
    const auto light = MakePuddles(r, 0.4f, 1), full = MakePuddles(r, 1.0f, 1), again = MakePuddles(r, 1.0f, 1);
    std::printf("  INFO %.0f m oval: %zu puddles at 0.4 wetness, %zu at 1.0\n", r.Length(), light.size(), full.size());
    CHECK(full.size() > light.size() && light.size() > 0);
    CHECK(full.size() == again.size());
    for (size_t i = 0; i < full.size() && i < again.size(); ++i)
        CHECK(full[i].centre.x == again[i].centre.x && full[i].radius == again[i].radius);
}

TEST_CASE("puddles: in the outer half of the road, never on the start straight (fair racing line)")
{
    const TrackRoute r = Oval();
    const auto ps = MakePuddles(r, 1.0f, 7);
    for (const Puddle& p : ps)
    {
        const TrackRoute::Projection pr = r.Project(p.centre);
        CHECK(std::fabs(pr.lateral) >= r.HalfWidth() * 0.45f - 0.01f);
        CHECK(std::fabs(pr.lateral) <= r.HalfWidth() * 0.85f + 0.01f);
        CHECK(pr.s >= 160.0f && pr.s <= r.Length() - 40.0f);
    }
    // the centreline itself stays dry almost everywhere: at most a puddle rim reaches it
    int wet = 0, samples = 0;
    for (float s = 0; s < r.Length(); s += 2.0f, ++samples)
        wet += PuddleGrip(ps, r.PointAt(s)) < 0.99f;
    std::printf("  INFO centreline samples touching a puddle: %d / %d\n", wet, samples);
    CHECK(wet <= samples / 50);
}

TEST_CASE("puddles: grip falls towards the centre, never below kPuddleGrip, 1 outside")
{
    std::vector<Puddle> ps = { { { 0, 0 }, 2.0f, 1.0f } };
    CHECK(std::fabs(PuddleGrip(ps, { 0, 0 }) - kPuddleGrip) < 1e-5f);
    const float mid = PuddleGrip(ps, { 1.0f, 0 });
    CHECK(mid > kPuddleGrip && mid < 1.0f);
    CHECK(PuddleGrip(ps, { 2.5f, 0 }) == 1.0f);
    ps[0].depth = 0.5f;
    CHECK(PuddleGrip(ps, { 0, 0 }) > kPuddleGrip);
}
