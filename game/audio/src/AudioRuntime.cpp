#include "racer/AudioRuntime.h"
#include "racer/Audio.h"

#pragma warning(push, 0)
#include "miniaudio.h"
#pragma warning(pop)
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>

namespace racer
{
    namespace
    {
        constexpr ma_uint32 kRate = 48000;
        constexpr ma_uint64 kPeriod = 480;          // 10 ms, like a device callback
        constexpr ma_uint32 kFadeMs = 15;           // gain changes ramp; no zipper noise
        constexpr float kSilent = 1e-3f;
        constexpr float kEngineCullDistance = 70.0f;
        // Playtest 2026-09-28: 11 opponent engines at once was a wall of noise. Like most racers,
        // only the nearest few opponents are heard; the rest fade out.
        constexpr int kAudibleOpponents = 4;
        constexpr float kPlayerEngineGain = 0.35f;
        constexpr float kOpponentEngineGain = 0.3f;
        constexpr float kBusGain = 0.5f;            // mix headroom before the limiter
        constexpr float kCeiling = 0.95f;           // about -0.45 dBFS
        constexpr float kMusicTrim = 0.6f;          // music sits under engines and combat
        constexpr std::array<const char*, 4> kStems = { "drums", "bass", "harmony", "intensity" };

        // Game space is left-handed (Wicked); miniaudio spatialises right-handed. Mirror Z.
        void SetPos(ma_sound* s, const AudioVec3& p) { ma_sound_set_position(s, p.x, p.y, -p.z); }
        void SetVel(ma_sound* s, const AudioVec3& v) { ma_sound_set_velocity(s, v.x, v.y, -v.z); }

        void SetupSpatial(ma_sound* s, float min_distance)
        {
            ma_sound_set_attenuation_model(s, ma_attenuation_model_inverse);
            ma_sound_set_min_distance(s, min_distance);
            ma_sound_set_max_distance(s, 250.0f);
        }

        struct Slot
        {
            ma_sound sound{};
            bool init = false;
            uint64_t serial = 0;
        };

        struct Family
        {
            std::array<float, 5> anchors{};
            std::array<ma_sound, 10> protos{}; // anchor * 2 + (0 on-load, 1 off-load)
            int loaded = 0;
        };

        struct EngineVoice
        {
            EngineVoice(Family& f, bool p) : family(&f), player(p), model(f.anchors) {}
            Family* family;
            bool player;
            EngineSoundModel model;
            ParameterSmoother throttle{ 8.0f, 5.0f };
            std::array<ma_sound, 10> layers{};
            std::array<float, 10> target{};
            AudioVec3 position;
            bool culled = false;
            float gain_scale = 1.0f;     // exhaust loudness (D-072)
            ma_sound turbo{};            // turbo whistle loop, made on first boost
            bool turbo_init = false;
            float turbo_target = 0.0f;
        };
    }

    struct AudioRuntime::Impl
    {
        std::filesystem::path root;
        bool offline = false;
        ma_resource_manager resources{};
        ma_engine engine{};
        ma_sound_group music_group{}, sfx_group{}, engine_group{};
        std::map<std::string, std::unique_ptr<ma_sound>, std::less<>> sfx;
        std::map<std::string, std::unique_ptr<Family>, std::less<>> families;
        std::vector<std::unique_ptr<EngineVoice>> engines;
        std::array<Slot, kWorldVoices> world;
        std::array<Slot, kCriticalVoices> critical;
        std::array<ma_sound, 4> stems{};
        ma_sound turbo_proto{};
        bool turbo_ok = false;
        bool music_on = false;     // stems loaded (kept resident between races: no 20+ MB churn)
        bool music_playing = false;
        std::string music_set;
        float intensity_target = 0.0f;
        ParameterSmoother intensity{ 0.5f, 0.25f };
        uint64_t serial = 0;
        AudioVec3 listener;
        AudioStats stats;
        PeakLimiter limiter{ kCeiling, 0.15f, float(kRate) }; // audio thread only

        static void OnProcess(void* user, float* frames, ma_uint64 count)
        {
            Impl& m = *static_cast<Impl*>(user);
            const ma_uint32 channels = ma_engine_get_channels(&m.engine);
            for (ma_uint64 i = 0; i < count * channels; ++i)
                frames[i] *= kBusGain;
            m.limiter.Process(frames, count, channels);
        }

        std::string Path(const std::filesystem::path& rel) const { return (root / rel).string(); }

        bool LoadDecoded(const std::filesystem::path& rel, ma_uint32 flags, ma_sound_group* group, ma_sound* out)
        {
            return ma_sound_init_from_file(&engine, Path(rel).c_str(), MA_SOUND_FLAG_DECODE | flags, group, nullptr, out) == MA_SUCCESS;
        }

        Family* GetFamily(std::string_view id)
        {
            if (auto it = families.find(id); it != families.end())
                return it->second->loaded == 10 ? it->second.get() : nullptr;
            auto family = std::make_unique<Family>();
            Family* f = family.get();
            families.emplace(std::string(id), std::move(family));

            std::ifstream in(root / "engine_families.json");
            const nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
            const std::string key(id);
            if (doc.is_discarded() || !doc.contains(key) || doc[key]["anchors_rpm"].size() != 5)
                return nullptr;
            for (size_t a = 0; a < 5; ++a)
                f->anchors[a] = doc[key]["anchors_rpm"][a].get<float>();
            for (size_t a = 0; a < 5; ++a)
                for (int load = 0; load < 2; ++load)
                {
                    const std::string file = "rpm" + std::to_string(int(f->anchors[a])) + (load == 0 ? "_on.wav" : "_off.wav");
                    if (!LoadDecoded(std::filesystem::path("engine") / std::string(id) / file, 0, nullptr, &f->protos[a * 2 + load]))
                        return nullptr;
                    ++f->loaded;
                }
            return f;
        }

        void ReleaseSlot(Slot& slot)
        {
            if (slot.init)
                ma_sound_uninit(&slot.sound);
            slot.init = false;
        }

        template <size_t N>
        Slot& PickSlot(std::array<Slot, N>& pool)
        {
            Slot* oldest = &pool[0];
            for (Slot& s : pool)
            {
                if (!s.init || !ma_sound_is_playing(&s.sound))
                    return s;
                if (s.serial < oldest->serial)
                    oldest = &s;
            }
            ++stats.oneshots_stolen;
            return *oldest;
        }

        void ClearEngines()
        {
            for (auto& e : engines)
            {
                for (ma_sound& layer : e->layers)
                    ma_sound_uninit(&layer);
                if (e->turbo_init)
                    ma_sound_uninit(&e->turbo);
            }
            engines.clear();
        }

        void ClearMusic()
        {
            if (music_on)
                for (ma_sound& s : stems)
                    ma_sound_uninit(&s);
            music_on = false;
            music_playing = false;
            music_set.clear();
        }
    };

    AudioRuntime::AudioRuntime() = default;
    AudioRuntime::~AudioRuntime() { Shutdown(); }

    bool AudioRuntime::Active() const { return impl_ != nullptr; }

    bool AudioRuntime::Init(const Config& config)
    {
        Shutdown();
        if (!std::filesystem::exists(std::filesystem::path(config.root) / "engine_families.json"))
            return false;
        auto impl = std::make_unique<Impl>();
        impl->root = config.root;
        impl->offline = config.offline;

        // 16-bit decoded buffers: half the RAM of f32, inaudible difference for playback.
        ma_resource_manager_config rm = ma_resource_manager_config_init();
        rm.decodedFormat = ma_format_s16;
        rm.decodedSampleRate = kRate;
        if (ma_resource_manager_init(&rm, &impl->resources) != MA_SUCCESS)
            return false;
        ma_engine_config ec = ma_engine_config_init();
        ec.pResourceManager = &impl->resources;
        ec.sampleRate = kRate;
        ec.onProcess = &Impl::OnProcess;
        ec.pProcessUserData = impl.get();
        if (config.offline)
        {
            ec.noDevice = MA_TRUE;
            ec.channels = 2;
        }
        if (ma_engine_init(&ec, &impl->engine) != MA_SUCCESS)
        {
            ma_resource_manager_uninit(&impl->resources);
            return false;
        }
        ma_engine_listener_set_world_up(&impl->engine, 0, 0.0f, 1.0f, 0.0f);
        ma_sound_group_init(&impl->engine, 0, nullptr, &impl->music_group);
        ma_sound_group_init(&impl->engine, 0, nullptr, &impl->sfx_group);
        ma_sound_group_init(&impl->engine, 0, &impl->sfx_group, &impl->engine_group);

        std::error_code ec_fs;
        for (const auto& entry : std::filesystem::directory_iterator(impl->root / "sfx", ec_fs))
        {
            if (entry.path().extension() != ".wav")
                continue;
            auto proto = std::make_unique<ma_sound>();
            if (impl->LoadDecoded(std::filesystem::path("sfx") / entry.path().filename(), 0, nullptr, proto.get()))
                impl->sfx.emplace(entry.path().stem().string(), std::move(proto));
        }
        impl->turbo_ok = impl->LoadDecoded("loops/turbo_whine.wav", 0, nullptr, &impl->turbo_proto);
        impl_ = std::move(impl);
        return true;
    }

    void AudioRuntime::Shutdown()
    {
        if (!impl_)
            return;
        Impl& m = *impl_;
        m.ClearEngines();
        m.ClearMusic();
        for (Slot& s : m.world)
            m.ReleaseSlot(s);
        for (Slot& s : m.critical)
            m.ReleaseSlot(s);
        for (auto& [name, proto] : m.sfx)
            ma_sound_uninit(proto.get());
        if (m.turbo_ok)
            ma_sound_uninit(&m.turbo_proto);
        for (auto& [id, family] : m.families)
            for (int i = 0; i < family->loaded; ++i)
                ma_sound_uninit(&family->protos[size_t(i)]);
        ma_sound_group_uninit(&m.engine_group);
        ma_sound_group_uninit(&m.sfx_group);
        ma_sound_group_uninit(&m.music_group);
        ma_engine_uninit(&m.engine);
        ma_resource_manager_uninit(&m.resources);
        impl_.reset();
    }

    void AudioRuntime::SetVolumes(float master, float music, float sfx)
    {
        if (!impl_)
            return;
        ma_engine_set_volume(&impl_->engine, std::clamp(master, 0.0f, 1.0f));
        ma_sound_group_set_volume(&impl_->music_group, std::clamp(music, 0.0f, 1.0f) * kMusicTrim);
        ma_sound_group_set_volume(&impl_->sfx_group, std::clamp(sfx, 0.0f, 1.0f));
    }

    void AudioRuntime::SetListener(const AudioVec3& p, const AudioVec3& f, const AudioVec3& v)
    {
        if (!impl_)
            return;
        impl_->listener = p;
        ma_engine_listener_set_position(&impl_->engine, 0, p.x, p.y, -p.z);
        ma_engine_listener_set_direction(&impl_->engine, 0, f.x, f.y, -f.z);
        ma_engine_listener_set_velocity(&impl_->engine, 0, v.x, v.y, -v.z);
    }

    int AudioRuntime::AddEngine(std::string_view family_id, bool player)
    {
        if (!impl_ || impl_->engines.size() >= size_t(kMaxEngines))
            return -1;
        Impl& m = *impl_;
        Family* family = m.GetFamily(family_id);
        if (!family)
            return -1;
        auto voice = std::make_unique<EngineVoice>(*family, player);
        for (size_t i = 0; i < 10; ++i)
        {
            ma_sound* s = &voice->layers[i];
            if (ma_sound_init_copy(&m.engine, &family->protos[i], player ? MA_SOUND_FLAG_NO_SPATIALIZATION : 0, &m.engine_group, s) != MA_SUCCESS)
            {
                for (size_t j = 0; j < i; ++j)
                    ma_sound_uninit(&voice->layers[j]);
                return -1;
            }
            ma_sound_set_looping(s, MA_TRUE);
            ma_sound_set_volume(s, player ? kPlayerEngineGain : kOpponentEngineGain);
            ma_sound_set_fade_in_milliseconds(s, 0.0f, 0.0f, 0);
            if (!player)
            {
                SetupSpatial(s, 8.0f);
                ma_sound_set_doppler_factor(s, 0.5f);
            }
        }
        m.engines.push_back(std::move(voice));
        return int(m.engines.size()) - 1;
    }

    void AudioRuntime::UpdateEngine(int handle, const EngineAudioInput& in, float dt)
    {
        if (!impl_ || handle < 0 || handle >= int(impl_->engines.size()))
            return;
        EngineVoice& e = *impl_->engines[size_t(handle)];
        e.position = in.position;
        const float load = e.throttle.Update(std::clamp(in.throttle, 0.0f, 1.0f), dt);
        const EngineLoopMix mix = e.model.Evaluate(in.rpm, load, in.airborne);
        for (size_t i = 0; i < 10; ++i)
        {
            e.target[i] = mix.layers[i].gain;
            ma_sound_set_pitch(&e.layers[i], std::max(0.05f, mix.layers[i].pitch));
            if (!e.player)
            {
                SetPos(&e.layers[i], in.position);
                SetVel(&e.layers[i], in.velocity);
            }
        }
    }

    void AudioRuntime::SetEngineExtras(int handle, float exhaust_gain, float turbo)
    {
        if (!impl_ || handle < 0 || handle >= int(impl_->engines.size()))
            return;
        Impl& m = *impl_;
        EngineVoice& e = *m.engines[size_t(handle)];
        const float g = std::clamp(exhaust_gain, 0.5f, 2.0f);
        if (std::fabs(g - e.gain_scale) > 1e-3f)
        {
            e.gain_scale = g;
            for (ma_sound& layer : e.layers)
                ma_sound_set_volume(&layer, (e.player ? kPlayerEngineGain : kOpponentEngineGain) * g);
        }
        const float level = std::clamp(turbo, 0.0f, 1.0f);
        if (level > 0.001f && !e.turbo_init && m.turbo_ok)
        {
            if (ma_sound_init_copy(&m.engine, &m.turbo_proto, e.player ? MA_SOUND_FLAG_NO_SPATIALIZATION : 0, &m.engine_group, &e.turbo) == MA_SUCCESS)
            {
                e.turbo_init = true;
                ma_sound_set_looping(&e.turbo, MA_TRUE);
                ma_sound_set_volume(&e.turbo, 0.0f);
                if (!e.player)
                    SetupSpatial(&e.turbo, 8.0f);
            }
        }
        if (e.turbo_init)
        {
            // the whistle rises in pitch with boost and is only really heard on boost
            e.turbo_target = 0.15f * level * std::sqrt(level); // 0.3 doubled the loudness of the engine: a whistle over it, not over the top
            ma_sound_set_pitch(&e.turbo, 0.55f + 0.9f * level);
            if (!e.player)
                SetPos(&e.turbo, e.position);
        }
    }

    void AudioRuntime::RemoveEngines()
    {
        if (impl_)
            impl_->ClearEngines();
    }

    void AudioRuntime::PlaySfx(std::string_view name, float gain, const AudioVec3* position)
    {
        if (!impl_)
            return;
        Impl& m = *impl_;
        auto it = m.sfx.find(name);
        if (it == m.sfx.end())
            return;
        Slot& slot = position ? m.PickSlot(m.world) : m.PickSlot(m.critical);
        m.ReleaseSlot(slot);
        if (ma_sound_init_copy(&m.engine, it->second.get(), position ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION, &m.sfx_group, &slot.sound) != MA_SUCCESS)
            return;
        slot.init = true;
        slot.serial = ++m.serial;
        ma_sound_set_volume(&slot.sound, std::clamp(gain, 0.0f, 1.0f));
        if (position)
        {
            SetupSpatial(&slot.sound, 6.0f);
            SetPos(&slot.sound, *position);
        }
        ma_sound_start(&slot.sound);
    }

    void AudioRuntime::StartMusic(std::string_view set)
    {
        if (!impl_)
            return;
        Impl& m = *impl_;
        if (m.music_on && m.music_set != set)
            m.ClearMusic();
        for (size_t i = 0; i < kStems.size() && !m.music_on; ++i)
        {
            const auto rel = std::filesystem::path("music") / std::string(set) / (std::string(kStems[i]) + ".wav");
            if (!m.LoadDecoded(rel, MA_SOUND_FLAG_NO_SPATIALIZATION, &m.music_group, &m.stems[i]))
            {
                for (size_t j = 0; j < i; ++j)
                    ma_sound_uninit(&m.stems[j]);
                return;
            }
        }
        // One shared start frame keeps the stems sample-aligned for the whole race.
        const ma_uint64 start = ma_engine_get_time_in_pcm_frames(&m.engine) + kPeriod * 2;
        for (size_t i = 0; i < kStems.size(); ++i)
        {
            ma_sound_stop(&m.stems[i]);
            ma_sound_seek_to_pcm_frame(&m.stems[i], 0);
            ma_sound_set_looping(&m.stems[i], MA_TRUE);
            ma_sound_set_start_time_in_pcm_frames(&m.stems[i], start);
            if (i == 3)
                ma_sound_set_volume(&m.stems[i], m.intensity.Value());
            ma_sound_start(&m.stems[i]);
        }
        m.music_on = true;
        m.music_playing = true;
        m.music_set = std::string(set);
    }

    void AudioRuntime::StopMusic()
    {
        if (!impl_ || !impl_->music_on)
            return;
        for (ma_sound& s : impl_->stems)
            ma_sound_stop(&s);
        impl_->music_playing = false;
    }

    void AudioRuntime::SetMusicIntensity(float intensity)
    {
        if (impl_)
            impl_->intensity_target = std::clamp(intensity, 0.0f, 1.0f);
    }

    void AudioRuntime::Update(float dt)
    {
        if (!impl_)
            return;
        Impl& m = *impl_;
        AudioStats s;
        s.oneshots_stolen = m.stats.oneshots_stolen;
        // Rank opponents by distance: only the nearest kAudibleOpponents within range are heard.
        std::vector<std::pair<float, EngineVoice*>> ranked;
        for (auto& e : m.engines)
        {
            const float dx = e->position.x - m.listener.x, dy = e->position.y - m.listener.y, dz = e->position.z - m.listener.z;
            if (!e->player)
                ranked.emplace_back(dx * dx + dy * dy + dz * dz, e.get());
            e->culled = false;
        }
        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (size_t r = 0; r < ranked.size(); ++r)
            ranked[r].second->culled = r >= size_t(kAudibleOpponents) || ranked[r].first > kEngineCullDistance * kEngineCullDistance;
        for (auto& e : m.engines)
        {
            s.engines_culled += e->culled ? 1 : 0;
            for (size_t i = 0; i < 10; ++i)
            {
                ma_sound* layer = &e->layers[i];
                const float target = e->culled ? 0.0f : e->target[i];
                const bool playing = ma_sound_is_playing(layer) != MA_FALSE;
                if (target > kSilent)
                {
                    if (!playing)
                        ma_sound_start(layer);
                    ma_sound_set_fade_in_milliseconds(layer, -1.0f, target, kFadeMs);
                }
                else if (playing)
                {
                    // Fade out first, stop once silent: stopping mid-waveform clicks.
                    if (ma_sound_get_current_fade_volume(layer) <= kSilent)
                        ma_sound_stop(layer);
                    else
                        ma_sound_set_fade_in_milliseconds(layer, -1.0f, 0.0f, kFadeMs);
                }
                s.engine_layers += ma_sound_is_playing(layer) ? 1 : 0;
            }
            if (e->turbo_init)
            {
                const float target = e->culled ? 0.0f : e->turbo_target;
                const bool playing = ma_sound_is_playing(&e->turbo) != MA_FALSE;
                if (target > kSilent)
                {
                    if (!playing)
                        ma_sound_start(&e->turbo);
                    ma_sound_set_volume(&e->turbo, target);
                }
                else if (playing)
                    ma_sound_stop(&e->turbo);
                s.turbo_voices += ma_sound_is_playing(&e->turbo) ? 1 : 0;
            }
        }
        s.playing_voices = s.engine_layers + s.turbo_voices;
        for (Slot& slot : m.world)
            s.playing_voices += slot.init && ma_sound_is_playing(&slot.sound) ? 1 : 0;
        for (Slot& slot : m.critical)
            s.playing_voices += slot.init && ma_sound_is_playing(&slot.sound) ? 1 : 0;
        if (m.music_playing)
        {
            ma_sound_set_volume(&m.stems[3], m.intensity.Update(m.intensity_target, dt));
            s.playing_voices += int(kStems.size());
        }
        m.stats = s;
    }

    AudioStats AudioRuntime::Stats() const { return impl_ ? impl_->stats : AudioStats{}; }

    void AudioRuntime::Render(float* out, uint64_t frames)
    {
        if (!impl_ || !impl_->offline)
            return;
        const ma_uint32 channels = ma_engine_get_channels(&impl_->engine);
        for (uint64_t done = 0; done < frames;)
        {
            const ma_uint64 want = std::min<ma_uint64>(kPeriod, frames - done);
            ma_uint64 read = 0;
            ma_engine_read_pcm_frames(&impl_->engine, out + done * channels, want, &read);
            if (read < want)
                std::fill(out + (done + read) * channels, out + (done + want) * channels, 0.0f);
            done += want;
        }
    }

    bool AudioRuntime::MusicCursors(uint64_t out[4]) const
    {
        if (!impl_ || !impl_->music_playing)
            return false;
        for (size_t i = 0; i < 4; ++i)
        {
            ma_uint64 c = 0;
            ma_sound_get_cursor_in_pcm_frames(&impl_->stems[i], &c);
            out[i] = c;
        }
        return true;
    }
}
