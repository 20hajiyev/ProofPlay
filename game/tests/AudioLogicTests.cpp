#include "TestHarness.h"
#include "racer/Audio.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace racer;

namespace
{
    EngineSoundModel I4() { return EngineSoundModel({ 1000, 2500, 4000, 5500, 7000 }); }
    float Power(const EngineLoopMix& m)
    {
        float p = 0;
        for (auto& l : m.layers)
            p += l.gain * l.gain;
        return p;
    }
}

TEST_CASE("audio model: total loudness stays constant across the rev range and load (equal power)")
{
    const EngineSoundModel m = I4();
    for (float rpm = 800; rpm <= 7400; rpm += 37)
        for (float load : { 0.0f, 0.3f, 1.0f })
            CHECK(std::fabs(Power(m.Evaluate(rpm, load, false)) - 1.0f) < 1e-3f);
}

TEST_CASE("audio model: at an anchor only that loop plays at pitch 1; between anchors pitches bracket 1")
{
    const EngineSoundModel m = I4();
    const EngineLoopMix at = m.Evaluate(4000, 1.0f, false);
    CHECK(std::fabs(at.layers[4].gain - 1.0f) < 1e-4f && std::fabs(at.layers[4].pitch - 1.0f) < 1e-4f);
    const EngineLoopMix mid = m.Evaluate(4750, 1.0f, false);
    CHECK(mid.layers[4].pitch > 1.0f && mid.layers[6].pitch < 1.0f); // 4000 pitched up, 5500 pitched down
    for (auto& l : mid.layers)
        if (l.gain > 0)
            CHECK(l.pitch > 0.75f && l.pitch < 1.35f); // never stretched far from the recording
}

TEST_CASE("audio model: no jump in any layer gain as rpm sweeps (no clicks)")
{
    const EngineSoundModel m = I4();
    EngineLoopMix prev = m.Evaluate(900, 0.7f, false);
    float worst = 0;
    for (float rpm = 900; rpm <= 7200; rpm += 5)
    {
        const EngineLoopMix cur = m.Evaluate(rpm, 0.7f, false);
        for (size_t i = 0; i < 10; ++i)
            worst = std::max(worst, std::fabs(cur.layers[i].gain - prev.layers[i].gain));
        prev = cur;
    }
    CHECK(worst < 0.02f);
}

TEST_CASE("audio model: lifting off and going airborne shift the mix to off-load")
{
    const EngineSoundModel m = I4();
    const EngineLoopMix on = m.Evaluate(4000, 1.0f, false);
    const EngineLoopMix air = m.Evaluate(4000, 1.0f, true);
    const EngineLoopMix off = m.Evaluate(4000, 0.0f, false);
    CHECK(on.layers[4].gain > 0.99f && on.layers[5].gain < 0.01f);
    CHECK(air.layers[5].gain > on.layers[5].gain);
    CHECK(off.layers[5].gain > 0.99f);
}

TEST_CASE("audio smoother: throttle response is rate limited up and down")
{
    ParameterSmoother s(8.0f, 4.0f);
    s.Update(1.0f, 0.05f);
    CHECK(std::fabs(s.Value() - 0.4f) < 1e-5f);
    for (int i = 0; i < 10; ++i)
        s.Update(1.0f, 0.05f);
    s.Update(0.0f, 0.05f);
    CHECK(std::fabs(s.Value() - 0.8f) < 1e-5f);
}

TEST_CASE("voice budget: 96 voices, reserves honoured, warnings never lose to ambience")
{
    std::vector<VoiceRequest> req;
    uint32_t id = 1;
    for (int i = 0; i < 60; ++i) // ambience storm
        req.push_back({ id++, SoundCategory::Ambience, 1.0f, 1.0f });
    for (int i = 0; i < 70; ++i) // big combat scene
        req.push_back({ id++, SoundCategory::Opponents, 2.0f, 0.05f + i * 0.01f });
    for (int i = 0; i < 12; ++i) // many simultaneous threat warnings
        req.push_back({ id++, SoundCategory::Critical, 5.0f, 1.0f });
    for (int i = 0; i < 10; ++i)
        req.push_back({ id++, SoundCategory::Player, 3.0f, 1.0f });
    const VoiceBudget::Allocation a = VoiceBudget().Allocate(req);
    CHECK(a.real.size() == 96);
    CHECK(a.real.size() + a.virtualised.size() == req.size());
    int critical_real = 0, player_real = 0;
    for (uint32_t r : a.real)
    {
        const auto& q = *std::find_if(req.begin(), req.end(), [&](auto& x) { return x.id == r; });
        critical_real += q.category == SoundCategory::Critical;
        player_real += q.category == SoundCategory::Player;
    }
    CHECK(critical_real == 12); // all warnings audible, even beyond their 8-voice reserve
    CHECK(player_real == 10);
}

TEST_CASE("voice budget: an idle category lends its reserve")
{
    std::vector<VoiceRequest> req;
    for (uint32_t i = 1; i <= 80; ++i)
        req.push_back({ i, SoundCategory::Opponents, 1.0f, 1.0f });
    const VoiceBudget::Allocation a = VoiceBudget().Allocate(req);
    CHECK(a.real.size() == 80); // 40 reserve + 40 borrowed from idle music/UI/player/ambience
}

TEST_CASE("audio: peak limiter never exceeds the ceiling and recovers to unity")
{
    PeakLimiter limiter(0.95f, 0.15f, 48000.0f);
    std::vector<float> x(48000 * 2);
    for (size_t f = 0; f < 24000; ++f) // 0.5 s at 2x full scale, then 0.5 s quiet
        x[f * 2] = x[f * 2 + 1] = 2.0f * std::sin(float(f) * 0.05f);
    for (size_t f = 24000; f < 48000; ++f)
        x[f * 2] = x[f * 2 + 1] = 0.2f * std::sin(float(f) * 0.05f);
    limiter.Process(x.data(), 48000, 2);
    float peak = 0.0f;
    for (float v : x)
        peak = std::max(peak, std::abs(v));
    CHECK(peak <= 0.95f + 1e-6f);
    CHECK(limiter.Gain() > 0.97f); // 0.5 s = 3.3 release time constants: 1 - 0.5 * e^-3.3

    // Below the ceiling it is transparent.
    PeakLimiter clean(0.95f, 0.15f, 48000.0f);
    std::vector<float> y(960, 0.5f), ref = y;
    clean.Process(y.data(), 480, 2);
    CHECK(y == ref);
}
// ---- turbo and exhaust sound models (D-072) ----------------------------------------------------
namespace
{
    float Spool(TurboAudioModel& t, float rpm_frac, float throttle, float seconds)
    {
        float level = 0.0f;
        for (int i = 0; i < int(seconds * 120); ++i)
            level = t.Update(rpm_frac, throttle, 1.0f / 120);
        return level;
    }
}

TEST_CASE("turbo sound: no turbo fitted, no whistle and no blow-off")
{
    TurboAudioModel t(0.0f);
    CHECK(Spool(t, 0.9f, 1.0f, 3.0f) == 0.0f);
    Spool(t, 0.9f, 0.0f, 0.5f);
    CHECK(!t.TakeBlowoff());
}

TEST_CASE("turbo sound: spools up on boost, the big turbo slower; little boost low in the revs")
{
    TurboAudioModel street(0.6f), race(1.0f);
    float t_street = -1, t_race = -1;
    for (int i = 0; i < 600; ++i)
    {
        if (street.Update(0.85f, 1.0f, 1.0f / 120) > 0.9f && t_street < 0)
            t_street = i / 120.0f;
        if (race.Update(0.85f, 1.0f, 1.0f / 120) > 0.9f && t_race < 0)
            t_race = i / 120.0f;
    }
    CHECK(t_street > 0 && t_race > 0);
    CHECK(t_race > t_street);      // lag: the big one takes longer
    CHECK(t_race < 2.5f);
    TurboAudioModel low(1.0f);
    CHECK(Spool(low, 0.12f, 1.0f, 3.0f) < 0.2f); // off boost low in the revs
}

TEST_CASE("turbo sound: one blow-off per lift from boost, re-armed by the next boost")
{
    TurboAudioModel t(1.0f);
    Spool(t, 0.85f, 1.0f, 2.5f);
    t.Update(0.85f, 0.0f, 1.0f / 120);
    CHECK(t.TakeBlowoff());
    Spool(t, 0.85f, 0.0f, 1.0f);
    CHECK(!t.TakeBlowoff());       // staying off the throttle: no second hiss
    Spool(t, 0.2f, 1.0f, 0.1f);
    t.Update(0.2f, 0.0f, 1.0f / 120);
    CHECK(!t.TakeBlowoff());       // a blip without boost built: nothing to vent
    Spool(t, 0.85f, 1.0f, 2.5f);
    t.Update(0.85f, 0.0f, 1.0f / 120);
    CHECK(t.TakeBlowoff());
}

TEST_CASE("exhaust pops: a stock exhaust never pops; a straight pipe crackles on a high-rev lift")
{
    BackfireModel stock(0.0f, 7), pipe(1.0f, 7), pipe2(1.0f, 7);
    int pops_stock = 0, pops_high = 0, pops_low = 0;
    for (int i = 0; i < 240; ++i) // 2 s on the throttle high in the revs, then 1 s off
        pops_stock += stock.Update(0.8f, i < 240 ? 1.0f : 0.0f, 1.0f / 120);
    for (int i = 0; i < 120; ++i)
        pops_stock += stock.Update(0.8f, 0.0f, 1.0f / 120);
    for (int i = 0; i < 240; ++i)
        pipe.Update(0.8f, 1.0f, 1.0f / 120);
    for (int i = 0; i < 120; ++i)
        pops_high += pipe.Update(0.8f, 0.0f, 1.0f / 120);
    for (int i = 0; i < 240; ++i)
        pipe2.Update(0.25f, 1.0f, 1.0f / 120);
    for (int i = 0; i < 120; ++i)
        pops_low += pipe2.Update(0.25f, 0.0f, 1.0f / 120);
    CHECK(pops_stock == 0);
    CHECK(pops_high >= 2 && pops_high <= 8);
    CHECK(pops_low == 0);          // lifting at low revs: no unburnt fuel to light
}

TEST_CASE("exhaust pops: the same seed gives the same crackle (deterministic replays)")
{
    auto run = [] {
        BackfireModel b(0.6f, 99);
        std::vector<int> when;
        for (int i = 0; i < 600; ++i)
        {
            const float thr = (i / 120) % 2 == 0 ? 1.0f : 0.0f;
            if (b.Update(0.8f, thr, 1.0f / 120))
                when.push_back(i);
        }
        return when;
    };
    const std::vector<int> a = run(), b = run();
    CHECK(!a.empty());
    CHECK(a == b);
}

TEST_CASE("exhaust pops: a gradual lift (gamepad trigger, autopilot) pops too")
{
    BackfireModel b(1.0f, 7);
    for (int i = 0; i < 240; ++i)
        b.Update(0.8f, 1.0f, 1.0f / 120);
    int pops = 0;
    for (int i = 0; i < 24; ++i) // the trigger eased off over 0.2 s
        pops += b.Update(0.8f, 1.0f - (i + 1) / 24.0f, 1.0f / 120);
    for (int i = 0; i < 120; ++i)
        pops += b.Update(0.8f, 0.0f, 1.0f / 120);
    CHECK(pops >= 2);
}
