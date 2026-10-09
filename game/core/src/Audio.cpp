#include "racer/Audio.h"

#include <algorithm>
#include <cmath>

namespace racer
{
    namespace
    {
        constexpr float kHalfPi = 1.5707963f;
        // Equal-power pair: a^2 + b^2 = 1 for t in [0, 1].
        void EqualPower(float t, float& a, float& b)
        {
            t = std::clamp(t, 0.0f, 1.0f);
            a = std::cos(t * kHalfPi);
            b = std::sin(t * kHalfPi);
        }
    }

    EngineSoundModel::EngineSoundModel(std::array<float, 5> anchor_rpm) : anchors_(anchor_rpm) {}

    EngineLoopMix EngineSoundModel::Evaluate(float rpm, float load, bool airborne) const
    {
        EngineLoopMix mix;
        rpm = std::clamp(rpm, anchors_.front() * 0.5f, anchors_.back() * 1.2f);
        // In the air the wheels unload: the engine sound lightens toward off-load.
        load = std::clamp(airborne ? load * 0.4f : load, 0.0f, 1.0f);

        // Neighbouring anchors around rpm.
        size_t hi = 0;
        while (hi < anchors_.size() && anchors_[hi] < rpm)
            ++hi;
        size_t lo = hi == 0 ? 0 : hi - 1;
        hi = std::min(hi, anchors_.size() - 1);
        float g_on, g_off;
        EqualPower(1.0f - load, g_on, g_off); // load 1 -> all on-load
        auto set = [&](size_t a, float g) {
            const float pitch = rpm / anchors_[a];
            mix.layers[a * 2 + 0] = { g * g_on, pitch };
            mix.layers[a * 2 + 1] = { g * g_off, pitch };
        };
        if (hi == lo)
        {
            // Below the first or above the last anchor: one loop, pitched (idle / limiter).
            set(lo, 1.0f);
            return mix;
        }
        float g_lo, g_hi;
        EqualPower((rpm - anchors_[lo]) / (anchors_[hi] - anchors_[lo]), g_lo, g_hi);
        set(lo, g_lo);
        set(hi, g_hi);
        return mix;
    }

    PeakLimiter::PeakLimiter(float ceiling, float release_s, float sample_rate)
        : ceiling_(ceiling), release_coeff_(1.0f - std::exp(-1.0f / (release_s * sample_rate))) {}

    void PeakLimiter::Process(float* x, uint64_t frames, uint32_t channels)
    {
        for (uint64_t f = 0; f < frames; ++f)
        {
            float* frame = x + f * channels;
            float peak = 0.0f;
            for (uint32_t c = 0; c < channels; ++c)
                peak = std::max(peak, std::abs(frame[c]));
            const float needed = peak > ceiling_ ? ceiling_ / peak : 1.0f;
            gain_ = needed < gain_ ? needed : gain_ + (1.0f - gain_) * release_coeff_;
            gain_ = std::min(gain_, needed);
            for (uint32_t c = 0; c < channels; ++c)
                frame[c] *= gain_;
        }
    }

    float ParameterSmoother::Update(float target, float dt)
    {
        const float rate = (target > value_ ? rise_ : fall_) * dt;
        value_ += std::clamp(target - value_, -rate, rate);
        return value_;
    }

    VoiceBudget::Allocation VoiceBudget::Allocate(const std::vector<VoiceRequest>& requests) const
    {
        Allocation out;
        std::array<std::vector<const VoiceRequest*>, size_t(SoundCategory::Count)> by_cat;
        for (const VoiceRequest& r : requests)
            by_cat[size_t(r.category)].push_back(&r);
        auto score = [](const VoiceRequest* r) { return r->priority * 10.0f + r->audibility; };
        for (auto& list : by_cat)
            std::stable_sort(list.begin(), list.end(), [&](auto a, auto b) { return score(a) > score(b); });

        // Pass 1: every category fills its own reserve, best first.
        std::vector<const VoiceRequest*> overflow;
        int used = 0;
        for (size_t c = 0; c < by_cat.size(); ++c)
            for (size_t i = 0; i < by_cat[c].size(); ++i)
            {
                if (int(i) < kReserve[c])
                {
                    out.real.push_back(by_cat[c][i]->id);
                    ++used;
                }
                else
                    overflow.push_back(by_cat[c][i]);
            }
        // Pass 2: spare voices go to the rest by audibility and priority. Critical overflow is
        // considered first so a burst of warnings wins spare voices before anything else.
        std::stable_sort(overflow.begin(), overflow.end(), [&](auto a, auto b) {
            const bool ca = a->category == SoundCategory::Critical, cb = b->category == SoundCategory::Critical;
            if (ca != cb)
                return ca;
            return score(a) > score(b);
        });
        for (const VoiceRequest* r : overflow)
        {
            if (used < kTotal)
            {
                out.real.push_back(r->id);
                ++used;
            }
            else
                out.virtualised.push_back(r->id);
        }
        return out;
    }
}

namespace racer
{
    // ---- turbo and exhaust sound models (D-072) ------------------------------------------------
    TurboAudioModel::TurboAudioModel(float size) : size_(std::clamp(size, 0.0f, 1.0f)) {}

    float TurboAudioModel::Update(float rpm_frac, float throttle, float dt)
    {
        if (size_ <= 0.0f)
            return 0.0f;
        // a bigger turbo needs more revs before it makes boost, and spools slower
        const float lo = 0.25f + 0.15f * size_;
        const float x = std::clamp((rpm_frac - lo) / 0.3f, 0.0f, 1.0f);
        const float target = std::clamp(throttle, 0.0f, 1.0f) * x * x * (3.0f - 2.0f * x);
        const float k = target > boost_ ? 2.2f / (0.4f + size_) : 6.0f;
        boost_ += (target - boost_) * std::min(1.0f, dt * k);
        if (boost_ > 0.6f && throttle >= 0.2f) // armed only while boost is being built, not while it bleeds off
            armed_ = true;
        if (armed_ && throttle < 0.2f && prev_throttle_ >= 0.2f)
            blowoff_ = true, armed_ = false;
        prev_throttle_ = throttle;
        return boost_;
    }

    bool TurboAudioModel::TakeBlowoff()
    {
        const bool b = blowoff_;
        blowoff_ = false;
        return b;
    }

    BackfireModel::BackfireModel(float intensity, uint32_t seed) : intensity_(std::clamp(intensity, 0.0f, 1.0f)), state_(seed ? seed : 1u) {}

    float BackfireModel::Random()
    {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return float(state_ & 0xFFFFFF) / float(0x1000000);
    }

    int BackfireModel::Update(float rpm_frac, float throttle, float dt)
    {
        if (intensity_ <= 0.0f)
            return 0;
        // a lift = the throttle closing below 15% within 0.35 s of being over 60% open, so an eased-off
        // gamepad trigger counts as well as a snapped-shut key
        window_ = throttle > 0.6f ? 0.35f : std::max(0.0f, window_ - dt);
        const bool lift = window_ > 0.0f && throttle < 0.15f && prev_throttle_ >= 0.15f;
        prev_throttle_ = throttle;
        if (lift && rpm_frac > 0.55f) // unburnt fuel lights in a hot exhaust
        {
            pops_left_ = 2 + int(Random() * intensity_ * 5.0f);
            next_pop_ = 0.03f + Random() * 0.05f;
        }
        if (throttle >= 0.15f)
        {
            pops_left_ = 0;
            return 0;
        }
        if (pops_left_ > 0)
        {
            next_pop_ -= dt;
            if (next_pop_ <= 0.0f)
            {
                --pops_left_;
                next_pop_ = 0.04f + Random() * 0.08f;
                return 1;
            }
        }
        return 0;
    }
}
