#include "Recorder.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace proofplay
{
    void Recorder::Begin(SessionSetup setup, const std::string& dir)
    {
        active_ = false;
        if (const char* off = std::getenv("PROOFPLAY_OFF"); off && *off == '1')
            return;
        const char* name = std::getenv("PROOFPLAY_PLAYER");
        setup.player = name && *name ? name : "Oyuncu";
        setup.build_id = BuildId();
        session_ = Session{};
        stats_ = StatsTracker{};
        session_.setup = std::move(setup);
        session_.inputs.reserve(120 * 60 * 5);
        // PROOFPLAY_SESSIONS moves recordings elsewhere (a packaged game, an uploader folder).
        const char* sessions = std::getenv("PROOFPLAY_SESSIONS");
        dir_ = sessions && *sessions ? sessions : dir;
        active_ = true;
    }

    void Recorder::BeforeStep(racer::runtime::RaceSession& race, const StepInput& in)
    {
        if (active_ && RaceRunning(race))
        {
            session_.inputs.push_back(in);
            reset_ = in.reset;
        }
    }

    void Recorder::AfterStep(racer::runtime::RaceSession& race)
    {
        if (!active_)
            return;
        stats_.Step(race, session_.setup.player_index, reset_);
        reset_ = false;
        // Saved the moment the PLAYER finishes, not when the whole field does: a player who
        // finished early and went back to the menu before the last rival crossed the line lost
        // the race entirely (D-099). The replay stops after the same input, so it still matches.
        if (RaceRunning(race) && !race.Rules().Participant(session_.setup.player_index).finished)
            return;
        active_ = false;
        session_.end = ComputeEnd(race, session_.setup.player_index);
        session_.stats = stats_.Finish(race, session_.setup.player_index);
        const std::time_t now = std::time(nullptr);
        std::tm tm{};
        localtime_s(&tm, &now);
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tm);
        const std::filesystem::path folder = std::filesystem::u8path(dir_) / std::filesystem::u8path(session_.setup.player);
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        // Write to a temp name first: the dashboard picks up only complete files.
        const std::filesystem::path tmp = folder / (std::string(stamp) + ".tmp");
        const std::filesystem::path final_path = folder / (std::string(stamp) + ".json");
        {
            std::ofstream f(tmp, std::ios::binary);
            f << ToJson(session_);
        }
        std::filesystem::rename(tmp, final_path, ec);
        last_file_ = final_path.string();
        session_.inputs.clear();
    }
}
