// M0 audio fixture: proves the miniaudio features AudioRuntime depends on (plan 2.15)
// by rendering the engine graph offline, with no playback device.
#include "TestHarness.h"
#include "miniaudio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace
{
    constexpr ma_uint32 kRate = 48000;

    struct OfflineEngine
    {
        ma_engine engine{};
        bool ok = false;
        explicit OfflineEngine(ma_uint32 channels)
        {
            ma_engine_config config = ma_engine_config_init();
            config.noDevice = MA_TRUE;
            config.channels = channels;
            config.sampleRate = kRate;
            ok = ma_engine_init(&config, &engine) == MA_SUCCESS;
        }
        ~OfflineEngine() { if (ok) ma_engine_uninit(&engine); }
        // Pulls in 10 ms periods like a real device. A single oversized read (measured: 9600
        // frames with two sounds attached) silently drops a scheduled sound in miniaudio 0.11.25.
        std::vector<float> Render(ma_uint64 frames)
        {
            constexpr ma_uint64 kPeriod = 480;
            const ma_uint32 channels = ma_engine_get_channels(&engine);
            std::vector<float> out(frames * channels);
            for (ma_uint64 done = 0; done < frames;)
            {
                const ma_uint64 want = std::min(kPeriod, frames - done);
                ma_uint64 read = 0;
                ma_engine_read_pcm_frames(&engine, out.data() + done * channels, want, &read);
                done += want;
            }
            return out;
        }
    };

    struct BufferSound
    {
        std::vector<float> pcm;
        ma_audio_buffer buffer{};
        ma_sound sound{};
        BufferSound(ma_engine& engine, std::vector<float> samples, ma_sound_group* group = nullptr, ma_uint32 flags = 0, ma_uint32 channels = 1) : pcm(std::move(samples))
        {
            auto cfg = ma_audio_buffer_config_init(ma_format_f32, channels, pcm.size() / channels, pcm.data(), nullptr);
            ma_audio_buffer_init(&cfg, &buffer);
            ma_sound_init_from_data_source(&engine, &buffer, MA_SOUND_FLAG_NO_SPATIALIZATION | flags, group, &sound);
        }
        ~BufferSound() { ma_sound_uninit(&sound); ma_audio_buffer_uninit(&buffer); }
    };

    std::vector<float> Sine(double hz, ma_uint32 frames)
    {
        std::vector<float> s(frames);
        for (ma_uint32 i = 0; i < frames; ++i)
            s[i] = 0.5f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * hz * i / kRate));
        return s;
    }

    int ZeroCrossings(const std::vector<float>& mono, size_t from = 0)
    {
        int n = 0;
        for (size_t i = from + 1; i < mono.size(); ++i)
            n += (mono[i - 1] < 0.0f) != (mono[i] < 0.0f);
        return n;
    }

    size_t FirstNonSilent(const std::vector<float>& interleaved, ma_uint32 channels, ma_uint32 channel)
    {
        for (size_t f = 0; f * channels < interleaved.size(); ++f)
            if (std::fabs(interleaved[f * channels + channel]) > 1e-6f)
                return f;
        return SIZE_MAX;
    }
}

TEST_CASE("audio: offline miniaudio engine initialises without a device")
{
    OfflineEngine e(1);
    CHECK(e.ok);
}

TEST_CASE("audio: pitch 2.0 doubles a looping engine tone's frequency")
{
    OfflineEngine e(1);
    BufferSound tone(e.engine, Sine(400.0, kRate)); // exactly 400 cycles: seamless loop
    ma_sound_set_looping(&tone.sound, MA_TRUE);
    ma_sound_set_pitch(&tone.sound, 2.0f);
    ma_sound_start(&tone.sound);
    const int crossings = ZeroCrossings(e.Render(kRate), 64);
    CHECK(crossings > 1600 * 0.98 && crossings < 1600 * 1.02);
}

// Music stems never change pitch, so they are created with MA_SOUND_FLAG_NO_PITCH and
// bypass the resampler; that makes the scheduled start exact.
TEST_CASE("audio: unpitched start time is sample accurate in PCM frames")
{
    OfflineEngine e(1);
    BufferSound dc(e.engine, std::vector<float>(kRate, 0.25f), nullptr, MA_SOUND_FLAG_NO_PITCH);
    ma_sound_set_start_time_in_pcm_frames(&dc.sound, 1000);
    ma_sound_start(&dc.sound);
    CHECK(FirstNonSilent(e.Render(4000), 1, 0) == 1000);
}

// Pitchable sounds (engine loops) pass through the linear resampler, which adds a fixed
// one-frame latency. Recorded here so a miniaudio upgrade that changes it is noticed.
TEST_CASE("audio: pitchable sound start has a fixed one-frame resampler latency")
{
    OfflineEngine e(1);
    BufferSound dc(e.engine, std::vector<float>(kRate, 0.25f));
    ma_sound_set_start_time_in_pcm_frames(&dc.sound, 1000);
    ma_sound_start(&dc.sound);
    CHECK(FirstNonSilent(e.Render(4000), 1, 0) == 1001);
}

TEST_CASE("audio: two stems scheduled on the same frame start together")
{
    // Stereo stems, each carrying signal on one channel only, so their onsets are separable.
    auto one_sided = [](ma_uint32 channel) {
        std::vector<float> s(kRate * 2, 0.0f);
        for (ma_uint32 f = 0; f < kRate; ++f) s[f * 2 + channel] = 0.25f;
        return s;
    };
    OfflineEngine e(2);
    BufferSound left(e.engine, one_sided(0), nullptr, MA_SOUND_FLAG_NO_PITCH, 2);
    BufferSound right(e.engine, one_sided(1), nullptr, MA_SOUND_FLAG_NO_PITCH, 2);
    ma_sound_set_start_time_in_pcm_frames(&left.sound, 4800);
    ma_sound_set_start_time_in_pcm_frames(&right.sound, 4800);
    ma_sound_start(&right.sound); // started in the opposite order on purpose
    ma_sound_start(&left.sound);
    auto out = e.Render(9600);
    CHECK(FirstNonSilent(out, 2, 0) == 4800);
    CHECK(FirstNonSilent(out, 2, 1) == 4800);
}

TEST_CASE("audio: sound group volume controls its members (category mix)")
{
    OfflineEngine e(1);
    ma_sound_group ambience{};
    CHECK(ma_sound_group_init(&e.engine, 0, nullptr, &ambience) == MA_SUCCESS);
    {
        BufferSound bed(e.engine, std::vector<float>(kRate, 0.25f), &ambience);
        ma_sound_group_set_volume(&ambience, 0.0f);
        ma_sound_start(&bed.sound);
        CHECK(FirstNonSilent(e.Render(2000), 1, 0) == SIZE_MAX);
    }
    ma_sound_group_uninit(&ambience);
}

TEST_CASE("audio: 48 kHz WAV can be written and streamed back")
{
    const auto path = (std::filesystem::temp_directory_path() / "racer_audio_fixture.wav").string();
    {
        auto enc_cfg = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 1, kRate);
        ma_encoder encoder{};
        CHECK(ma_encoder_init_file(path.c_str(), &enc_cfg, &encoder) == MA_SUCCESS);
        auto pcm = Sine(500.0, kRate);
        ma_uint64 written = 0;
        ma_encoder_write_pcm_frames(&encoder, pcm.data(), pcm.size(), &written);
        ma_encoder_uninit(&encoder);
        CHECK(written == kRate);
    }
    OfflineEngine e(1);
    ma_sound stream{};
    CHECK(ma_sound_init_from_file(&e.engine, path.c_str(), MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, nullptr, &stream) == MA_SUCCESS);
    ma_sound_start(&stream);
    const int crossings = ZeroCrossings(e.Render(kRate / 2), 64);
    CHECK(crossings > 500 * 0.98 && crossings < 500 * 1.02);
    ma_sound_uninit(&stream);
    std::filesystem::remove(path);
}

TEST_CASE("audio: default playback device probe (informational)")
{
    ma_context context{};
    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS)
    {
        std::printf("  INFO no audio context available\n");
        return;
    }
    ma_device_info* playback = nullptr;
    ma_uint32 count = 0;
    if (ma_context_get_devices(&context, &playback, &count, nullptr, nullptr) == MA_SUCCESS)
        for (ma_uint32 i = 0; i < count; ++i)
            if (playback[i].isDefault)
                std::printf("  INFO default playback device: %s (backend %s)\n", playback[i].name, ma_get_backend_name(context.backend));
    ma_context_uninit(&context);
}
