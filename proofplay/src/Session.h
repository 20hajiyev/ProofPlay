#pragma once
// ProofPlay session: everything needed to re-run a race exactly. Only the setup and the player's
// per-step commands are stored; telemetry, events and results come back from deterministic replay,
// so a session is small and a forged result cannot match its own replay.
#include "racer/Combat.h"
#include "racer/Vehicle.h"
#include "racer/runtime/RaceSession.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace proofplay
{
    struct SessionSetup
    {
        std::string player = "Bot";          // display name on the dashboard and leaderboard
        std::string track = "S01";           // S01 | L01
        int laps = 2;
        int cars = 4;
        int player_index = 0;                // grid slot the player drives
        std::vector<std::string> grid;       // car id per grid slot; empty = D01..D05 repeating
        std::map<std::string, std::string> player_parts; // garage parts (performance + weapon)
        std::string difficulty = "Normal";   // AI field: Easy | Normal | Hard
        int mode = 0;                        // racer::EventMode
        int weather_kind = 0;                // racer::WeatherKind
        float weather_intensity = 1.0f;
        float time_limit_s = 0.0f;
        bool restrict_abilities = false;
        std::vector<int> allowed_abilities;  // racer::Ability values
        std::string build_id;                // determinism holds within one build only
    };

    struct StepInput
    {
        racer::VehicleCommand drive;
        racer::CombatCommand combat;
        bool reset = false; // player-held reset, applied before this step
    };

    // Checksum of the end state: compared on replay to prove the session is genuine.
    struct EndState
    {
        uint64_t ticks = 0;
        float player_time_s = 0;
        int player_position = 0;
        bool player_finished = false;
        uint64_t hash = 0; // FNV-1a over every car's position, velocity and health bits
    };

    // The game's own end-of-race numbers (the results screen plus laps and speeds). Written with
    // the session so the dashboard can show a quick analysis at once, before the replay analysis.
    struct RaceStats
    {
        bool valid = false;
        int position = 0, participants = 0;
        float time_s = 0;
        bool finished = false;
        int uses = 0, hits_dealt = 0, wrecks = 0, wrecks_caused = 0, resets = 0;
        float damage_taken = 0;
        float top_speed_kmh = 0, avg_speed_kmh = 0;
        std::vector<float> lap_times;
    };

    // Collects RaceStats while the race runs; Step after every physics step.
    class StatsTracker
    {
    public:
        void Step(racer::runtime::RaceSession& race, int player, bool reset);
        RaceStats Finish(racer::runtime::RaceSession& race, int player) const;

    private:
        int laps_seen_ = 0;
        float lap_start_ = 0;
        double speed_sum_ = 0;
        uint64_t moving_steps_ = 0;
        float top_ = 0;
        int resets_ = 0;
        std::vector<float> laps_;
    };

    struct Session
    {
        SessionSetup setup;
        std::vector<StepInput> inputs; // one per physics step from Start()
        EndState end;
        RaceStats stats;
    };

    std::string BuildId();
    std::string ToJson(const Session& s);
    bool FromJson(const std::string& text, Session& out, std::string& error);
    EndState ComputeEnd(racer::runtime::RaceSession& race, int player);
    // Empty when the claimed end state is exactly what replay produced; otherwise what differs.
    std::string Mismatch(const EndState& claimed, const EndState& replayed);
    // The race keeps stepping until every car is done or the clock runs out.
    bool RaceRunning(const racer::runtime::RaceSession& race);

    // A live headless race built the same way the game's RacePath builds it.
    struct Race
    {
        explicit Race(const SessionSetup& setup);
        ~Race();
        bool Running() const;
        void Step(const StepInput& in);
        EndState End() const;
        racer::runtime::RaceSession& Session() { return *race; }
        int Player() const { return player; }

        std::unique_ptr<struct RaceScene> scene;
        std::unique_ptr<racer::runtime::RaceSession> race;
        int player = 0;
    };

    void InitializeEngine(); // job system, windowless device, physics (once per process)
    std::string ContentDir();
}
