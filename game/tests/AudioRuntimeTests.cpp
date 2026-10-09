// AudioRuntime rendered offline (no device) against the real generated assets (plan 2.15, 5.5).
#include "TestHarness.h"
#include "racer/AudioRuntime.h"

#define NOMINMAX
#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    constexpr float kDt = 1.0f / 60.0f;
    constexpr uint64_t kFramesPerTick = 800; // 48 kHz / 60

    racer::AudioRuntime::Config Offline()
    {
        return { RACER_AUDIO_DIR, true };
    }

    double PrivateMiB()
    {
        PROCESS_MEMORY_COUNTERS_EX c{};
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&c), sizeof(c));
        return double(c.PrivateUsage) / (1024.0 * 1024.0);
    }

    struct Meter
    {
        std::vector<float> buffer = std::vector<float>(kFramesPerTick * 2);
        float peak = 0.0f;
        double sum_sq = 0.0;
        uint64_t samples = 0;
        bool finite = true;
        void Tick(racer::AudioRuntime& audio)
        {
            audio.Render(buffer.data(), kFramesPerTick);
            for (float v : buffer)
            {
                finite = finite && std::isfinite(v);
                peak = std::max(peak, std::abs(v));
                sum_sq += double(v) * v;
            }
            samples += buffer.size();
        }
        float Rms() const { return samples ? float(std::sqrt(sum_sq / double(samples))) : 0.0f; }
    };

    const char* kFamilies[] = { "I4_NA", "I4_TURBO", "V6", "V8" };

    // 12 cars on a 60 m circle around the listener, revving through the range.
    void DriveEngines(racer::AudioRuntime& audio, const std::vector<int>& handles, float t, float radius = 60.0f)
    {
        for (size_t i = 0; i < handles.size(); ++i)
        {
            const float phase = t * 0.4f + float(i) * 0.52f;
            racer::EngineAudioInput in;
            in.rpm = 1500.0f + 2800.0f * (1.0f + std::sin(t * 0.9f + float(i)));
            in.throttle = std::fmod(t + float(i) * 0.3f, 3.0f) < 2.0f ? 1.0f : 0.0f;
            in.airborne = std::fmod(t, 7.0f) < 0.4f;
            const float r = i == 0 ? 0.0f : radius * (0.2f + 0.08f * float(i));
            in.position = { r * std::cos(phase), 0.5f, r * std::sin(phase) };
            in.velocity = { -r * 0.4f * std::sin(phase), 0.0f, r * 0.4f * std::cos(phase) };
            audio.UpdateEngine(handles[i], in, kDt);
        }
    }
}

TEST_CASE("audio: missing asset root leaves the runtime inactive and every call is a no-op")
{
    racer::AudioRuntime audio;
    CHECK(!audio.Init({ "does/not/exist", true }));
    CHECK(!audio.Active());
    CHECK(audio.AddEngine("V8", true) == -1);
    audio.UpdateEngine(0, {}, kDt);
    audio.PlaySfx("pickup");
    audio.StartMusic("harbor");
    audio.Update(kDt);
    CHECK(audio.Stats().playing_voices == 0);
}

TEST_CASE("audio: unknown engine family and unknown sfx are rejected without side effects")
{
    racer::AudioRuntime audio;
    CHECK(audio.Init(Offline()));
    CHECK(audio.AddEngine("W16", false) == -1);
    audio.PlaySfx("no_such_sound");
    audio.Update(kDt);
    CHECK(audio.Stats().playing_voices == 0);
}

TEST_CASE("audio: full race mix stays inside voice budget, clean and cheap")
{
    racer::AudioRuntime audio;
    CHECK(audio.Init(Offline()));
    std::vector<int> handles;
    for (int i = 0; i < racer::AudioRuntime::kMaxEngines; ++i)
        handles.push_back(audio.AddEngine(kFamilies[i % 4], i == 0));
    CHECK(std::count(handles.begin(), handles.end(), -1) == 0);
    CHECK(audio.AddEngine("V8", false) == -1); // cap
    audio.SetVolumes(1.0f, 0.8f, 1.0f);
    audio.StartMusic("harbor");
    audio.SetListener({ 0, 1, 0 }, { 0, 0, 1 }, {});

    const char* world[] = { "impact_heavy", "impact_light", "use_lance", "use_pulse", "use_trap", "wreck", "pickup" };
    Meter meter;
    int max_voices = 0, max_layers = 0;
    double render_s = 0.0;
    const int ticks = 60 * 20;
    for (int k = 0; k < ticks; ++k)
    {
        const float t = float(k) * kDt;
        DriveEngines(audio, handles, t);
        if (k % 6 == 0)
        {
            const racer::AudioVec3 p{ 20.0f * std::cos(t), 0.0f, 20.0f * std::sin(t) };
            audio.PlaySfx(world[k % 7], 0.8f, &p);
        }
        if (k % 60 == 0)
            audio.PlaySfx(k % 120 == 0 ? "warning" : "countdown");
        audio.SetMusicIntensity(std::fmod(t, 8.0f) < 4.0f ? 1.0f : 0.0f);
        audio.Update(kDt);
        max_voices = std::max(max_voices, audio.Stats().playing_voices);
        max_layers = std::max(max_layers, audio.Stats().engine_layers);
        const auto t0 = std::chrono::steady_clock::now();
        meter.Tick(audio);
        render_s += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }
    const double cpu_share = render_s / (ticks * kDt);
    std::printf("  race mix: peak %.3f, rms %.3f, max voices %d, max engine layers %d, render %.1f%% of real time\n",
        meter.peak, meter.Rms(), max_voices, max_layers, cpu_share * 100.0);
    CHECK(meter.finite);
    CHECK(meter.Rms() > 0.01f);
    CHECK(meter.peak <= 1.0f);                                // no clipping at full settings
    CHECK(max_voices <= 96);                                  // VoiceBudget::kTotal
    CHECK(max_layers <= racer::AudioRuntime::kMaxEngines * 4); // two anchors x two loads per engine
#ifdef NDEBUG
    CHECK(cpu_share < 0.10); // one mixing thread, well under 10% of a core
#endif
}

TEST_CASE("audio: engines beyond the cull distance stop all layers; the player's never does")
{
    racer::AudioRuntime audio;
    CHECK(audio.Init(Offline()));
    const int player = audio.AddEngine("I4_NA", true);
    const int far_car = audio.AddEngine("V8", false);
    audio.SetListener({}, { 0, 0, 1 }, {});
    Meter meter;
    for (int k = 0; k < 30; ++k)
    {
        audio.UpdateEngine(player, { 3000.0f, 1.0f, false, { 500, 0, 500 }, {} }, kDt);
        audio.UpdateEngine(far_car, { 3000.0f, 1.0f, false, { 0, 0, 200 }, {} }, kDt);
        audio.Update(kDt);
        meter.Tick(audio);
    }
    CHECK(audio.Stats().engines_culled == 1);
    CHECK(audio.Stats().engine_layers >= 1 && audio.Stats().engine_layers <= 4);
}

TEST_CASE("audio: world one-shot pool steals the oldest, critical pool is separate")
{
    racer::AudioRuntime audio;
    CHECK(audio.Init(Offline()));
    const racer::AudioVec3 p{ 5, 0, 5 };
    for (int i = 0; i < racer::AudioRuntime::kWorldVoices + 16; ++i)
        audio.PlaySfx("wreck", 1.0f, &p);
    audio.PlaySfx("warning");
    audio.Update(kDt);
    CHECK(audio.Stats().oneshots_stolen == 16);
    CHECK(audio.Stats().playing_voices == racer::AudioRuntime::kWorldVoices + 1);
}

TEST_CASE("audio: music stems stay sample-aligned across the loop point")
{
    racer::AudioRuntime audio;
    CHECK(audio.Init(Offline()));
    audio.StartMusic("harbor");
    Meter meter;
    for (int k = 0; k < 60 * 35; ++k) // loop is 30 s
    {
        audio.SetMusicIntensity(k % 240 < 120 ? 1.0f : 0.0f);
        audio.Update(kDt);
        meter.Tick(audio);
    }
    uint64_t c[4] = {};
    CHECK(audio.MusicCursors(c));
    CHECK(c[0] == c[1] && c[1] == c[2] && c[2] == c[3]);
    CHECK(c[0] > 0);
    CHECK(meter.Rms() > 0.01f);
}

TEST_CASE("audio: volume groups — sfx 0 keeps music, master 0 is silence")
{
    racer::AudioRuntime audio;
    CHECK(audio.Init(Offline()));
    const int h = audio.AddEngine("V6", true);
    audio.StartMusic("harbor");
    auto run = [&](float master, float music, float sfx) {
        audio.SetVolumes(master, music, sfx);
        Meter m;
        for (int k = 0; k < 60; ++k)
        {
            audio.UpdateEngine(h, { 4000.0f, 1.0f, false, {}, {} }, kDt);
            audio.Update(kDt);
            m.Tick(audio);
        }
        return m.Rms();
    };
    const float all = run(1, 1, 1);
    const float music_only = run(1, 1, 0);
    const float engine_only = run(1, 0, 1);
    const float none = run(0, 1, 1);
    CHECK(all > music_only && all > engine_only);
    CHECK(music_only > 0.005f && engine_only > 0.005f);
    CHECK(none < 1e-4f);
}

TEST_CASE("audio: race-sized Init/Shutdown cycles do not grow RAM")
{
    constexpr int kWarmup = 2, kCycles = 8;
    std::vector<double> ram; // after each measured cycle
    for (int cycle = 0; cycle < kWarmup + kCycles; ++cycle)
    {
        racer::AudioRuntime audio;
        CHECK(audio.Init(Offline()));
        std::vector<int> handles;
        for (int i = 0; i < racer::AudioRuntime::kMaxEngines; ++i)
            handles.push_back(audio.AddEngine(kFamilies[i % 4], i == 0));
        audio.StartMusic("harbor");
        Meter meter;
        for (int k = 0; k < 120; ++k)
        {
            DriveEngines(audio, handles, float(k) * kDt);
            if (k % 10 == 0)
                audio.PlaySfx("impact_light");
            audio.Update(kDt);
            meter.Tick(audio);
        }
        audio.RemoveEngines();
        audio.StopMusic();
        audio.Shutdown();
        if (cycle >= kWarmup)
            ram.push_back(PrivateMiB());
    }
    // Median of the last half against the first half (D-075): end-minus-start swung 0.04-1.02 MiB
    // run to run on one-off allocator steps while cycle-by-cycle RAM stayed flat; a real leak
    // raises every cycle and still fails this.
    auto median = [](std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
    const double growth = median({ ram.begin() + kCycles / 2, ram.end() }) - median({ ram.begin(), ram.begin() + kCycles / 2 });
    std::printf("  audio init/shutdown x%d: RAM %.1f..%.1f MiB, growth (median last half - first half) %.2f MiB\n", kCycles,
                *std::min_element(ram.begin(), ram.end()), *std::max_element(ram.begin(), ram.end()), growth);
    CHECK(growth < 1.0);
}

TEST_CASE("audio: only the nearest opponents' engines are heard")
{
    racer::AudioRuntime audio;
    CHECK(audio.Init(Offline()));
    std::vector<int> handles;
    for (int i = 0; i < racer::AudioRuntime::kMaxEngines; ++i)
        handles.push_back(audio.AddEngine(kFamilies[i % 4], i == 0));
    audio.SetListener({}, { 0, 0, 1 }, {});
    Meter meter;
    for (int k = 0; k < 30; ++k)
    {
        for (size_t i = 0; i < handles.size(); ++i) // whole pack within 25 m
            audio.UpdateEngine(handles[i], { 3000.0f, 1.0f, false, { float(i) * 2.0f, 0, 5.0f }, {} }, kDt);
        audio.Update(kDt);
        meter.Tick(audio);
    }
    CHECK(audio.Stats().engines_culled == racer::AudioRuntime::kMaxEngines - 1 - 4);
    CHECK(audio.Stats().engine_layers <= 5 * 4);
}
TEST_CASE("audio (D-072): a turbo adds its whistle voice; a louder exhaust raises the engine level")
{
    auto measure = [](float exhaust_gain, float turbo, int& turbo_voices) {
        racer::AudioRuntime audio;
        CHECK(audio.Init(Offline()));
        const int player = audio.AddEngine("I4_TURBO", true);
        audio.SetListener({}, { 0, 0, 1 }, {});
        Meter meter;
        for (int k = 0; k < 60; ++k)
        {
            audio.UpdateEngine(player, { 5500.0f, 1.0f, false, {}, {} }, kDt);
            audio.SetEngineExtras(player, exhaust_gain, turbo);
            audio.Update(kDt);
            meter.Tick(audio);
        }
        turbo_voices = audio.Stats().turbo_voices;
        CHECK(meter.finite);
        return meter.Rms();
    };
    int tv_stock = 0, tv_turbo = 0, tv_loud = 0;
    const float stock = measure(1.0f, 0.0f, tv_stock);
    const float turbo = measure(1.0f, 1.0f, tv_turbo);
    const float loud = measure(1.4f, 0.0f, tv_loud);
    std::printf("  INFO rms stock %.4f, +turbo %.4f, straight pipe %.4f\n", stock, turbo, loud);
    CHECK(tv_stock == 0 && tv_loud == 0);
    CHECK(tv_turbo == 1);
    CHECK(turbo > stock * 1.02f);
    CHECK(loud > stock * 1.2f);
}
