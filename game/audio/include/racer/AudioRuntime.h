#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace racer
{
    // Positions and directions in game world space (Wicked: left-handed, +Y up, metres).
    struct AudioVec3
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };

    struct EngineAudioInput
    {
        float rpm = 0.0f;
        float throttle = 0.0f; // raw 0..1; smoothed inside (plan 2.15)
        bool airborne = false;
        AudioVec3 position;
        AudioVec3 velocity;
    };

    struct AudioStats
    {
        int playing_voices = 0;  // physical voices mixed this frame
        int engine_layers = 0;   // of which engine loop layers
        int engines_culled = 0;  // engines too far away to hear (all layers stopped)
        int oneshots_stolen = 0; // total one-shots that replaced a still-playing one
        int turbo_voices = 0;    // turbo whistle loops playing (D-072)
    };

    // Game audio on miniaudio (plan 2.15): engine RPM/load loops per car, one-shot SFX pools,
    // layered music stems, three volume groups from settings. Voice caps are static, so the total
    // stays under VoiceBudget::kTotal (96): engines 12 x 4 layers + 24 world + 8 UI/critical + 4 stems.
    // If there is no audio device or the assets are missing, Init returns false and every call is
    // a cheap no-op: the game runs silent rather than failing.
    class AudioRuntime
    {
    public:
        struct Config
        {
            std::string root;     // assets/audio/game
            bool offline = false; // no device; the caller pulls samples with Render (tests)
        };
        static constexpr int kMaxEngines = 12;
        static constexpr int kWorldVoices = 24;
        static constexpr int kCriticalVoices = 8;

        AudioRuntime();
        ~AudioRuntime();
        AudioRuntime(const AudioRuntime&) = delete;
        AudioRuntime& operator=(const AudioRuntime&) = delete;

        bool Init(const Config& config);
        void Shutdown();
        bool Active() const;

        void SetVolumes(float master, float music, float sfx);
        void SetListener(const AudioVec3& position, const AudioVec3& forward, const AudioVec3& velocity);

        // Returns an engine handle, or -1 (unknown family, cap reached, inactive).
        int AddEngine(std::string_view family, bool player);
        void UpdateEngine(int handle, const EngineAudioInput& input, float dt);
        void RemoveEngines(); // end of race
        // Performance parts on this engine (D-072): exhaust_gain scales the engine loops (sport /
        // straight pipe are louder), turbo 0..1 is the boost level driving the whistle loop.
        void SetEngineExtras(int handle, float exhaust_gain, float turbo);

        // name = file stem under sfx/. Without a position the sound is 2D on the critical pool
        // (UI, countdown, threat warnings: never stolen by world sounds).
        void PlaySfx(std::string_view name, float gain = 1.0f, const AudioVec3* position = nullptr);

        void StartMusic(std::string_view set); // music/<set>/{drums,bass,harmony,intensity}.wav
        void StopMusic();
        void SetMusicIntensity(float intensity); // 0..1, fades the intensity stem

        void Update(float dt); // per rendered frame: culling, music fades, stats
        AudioStats Stats() const;

        // Offline only: pulls interleaved stereo frames in device-sized periods.
        void Render(float* out, uint64_t frames);
        // Offline only: sample-accurate cursors of the music stems (alignment check).
        bool MusicCursors(uint64_t out[4]) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
