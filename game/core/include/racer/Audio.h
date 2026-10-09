#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace racer
{
    // Plan 2.15 / 3.9: each engine family has 5 steady-RPM loops x 2 load states (on/off throttle).
    // The model crossfades neighbouring RPM anchors and the two load layers with equal-power
    // curves and pitches each loop by rpm / anchor_rpm, driven by RPM, load, gear and air state.
    struct EngineLoopMix
    {
        struct Layer
        {
            float gain = 0.0f;
            float pitch = 1.0f;
        };
        std::array<Layer, 10> layers; // index = anchor * 2 + (0 = on-load, 1 = off-load)
    };

    class EngineSoundModel
    {
    public:
        explicit EngineSoundModel(std::array<float, 5> anchor_rpm);

        // load: 0..1 throttle already smoothed; airborne wheels lighten the load (plan 2.15).
        EngineLoopMix Evaluate(float rpm, float load, bool airborne) const;
        const std::array<float, 5>& Anchors() const { return anchors_; }

    private:
        std::array<float, 5> anchors_;
    };

    // Smooths a raw control (throttle) the way plan 2.15 asks ("qaz reaksiyasının yumşaldılması").
    class ParameterSmoother
    {
    public:
        ParameterSmoother(float rise_per_s, float fall_per_s) : rise_(rise_per_s), fall_(fall_per_s) {}
        float Update(float target, float dt);
        float Value() const { return value_; }

    private:
        float rise_, fall_, value_ = 0.0f;
    };

    // Master-bus peak limiter: a busy race (12 engines, impacts, music) sums past full scale.
    // Instant attack guarantees the output never exceeds the ceiling; release is smooth so the
    // gain recovers without pumping. Runs on the audio thread, one instance per output.
    class PeakLimiter
    {
    public:
        PeakLimiter(float ceiling, float release_s, float sample_rate);
        void Process(float* interleaved, uint64_t frames, uint32_t channels);
        float Gain() const { return gain_; }

    private:
        float ceiling_, release_coeff_, gain_ = 1.0f;
    };

    // Turbo sound (D-072): boost builds with revs and throttle (the bigger turbo spools slower and
    // needs more revs) and bleeds off fast on a lift. Update returns the whistle level 0..1;
    // TakeBlowoff reports one blow-off valve hiss per lift from real boost.
    class TurboAudioModel
    {
    public:
        explicit TurboAudioModel(float size); // 0 = no turbo, 0.6 street, 1.0 race
        float Update(float rpm_frac, float throttle, float dt);
        bool TakeBlowoff();
        float Boost() const { return boost_; }

    private:
        float size_, boost_ = 0.0f, prev_throttle_ = 0.0f;
        bool armed_ = false, blowoff_ = false;
    };

    // Exhaust pops and crackle on a lift high in the revs (D-072). Intensity 0 = stock (never),
    // 0.5 sport, 1 straight pipe. Deterministic for a seed. Update returns the pops this step.
    class BackfireModel
    {
    public:
        BackfireModel(float intensity, uint32_t seed);
        int Update(float rpm_frac, float throttle, float dt);

    private:
        float Random(); // 0..1
        float intensity_;
        uint32_t state_;
        float prev_throttle_ = 0.0f, next_pop_ = -1.0f, window_ = 0.0f;
        int pops_left_ = 0;
    };

    enum class SoundCategory : uint8_t { Music, Critical, Player, Opponents, Ambience, Count };

    struct VoiceRequest
    {
        uint32_t id = 0;
        SoundCategory category = SoundCategory::Ambience;
        float priority = 0.0f;   // designer priority, higher wins
        float audibility = 1.0f; // gain after distance attenuation, 0..1
    };

    // Chooses which requested sounds get one of the 96 physical voices (plan 2.15). Every category
    // keeps its reserve; unused reserve can be borrowed by others, but Critical (UI + threat
    // warnings) always gets its reserve first and is never displaced by Ambience. Sounds that do
    // not get a voice are virtual: tracked, silent, resumed when they win again.
    class VoiceBudget
    {
    public:
        static constexpr std::array<int, size_t(SoundCategory::Count)> kReserve = { 8, 8, 24, 40, 16 };
        static constexpr int kTotal = 96;

        struct Allocation
        {
            std::vector<uint32_t> real;
            std::vector<uint32_t> virtualised;
        };
        Allocation Allocate(const std::vector<VoiceRequest>& requests) const;
    };
}
