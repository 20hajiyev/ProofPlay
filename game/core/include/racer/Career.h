#pragma once
#include "racer/Customization.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace racer
{
    // Plan 2.12: 6 regions x 8 events, ids "R{region}_E{event}" (R01_E01 .. R06_E08).
    std::string EventId(int region, int event);
    bool ParseEventId(const std::string& id, int& region, int& event);

    enum class EventMode { CombatRace, TimeTrial, CheckpointRun, Destruction, RivalDuel };
    EventMode ModeOfEvent(int event); // E01..E08 layout from plan 2.12

    // Main-result medals (max 3). Races: 1st = 3, 2nd = 2, 3rd = 1. Timed events use the
    // validated reference time T: gold <= 1.05 T, silver <= 1.15 T, bronze <= 1.30 T.
    int PositionMedals(int position);
    int TimeMedals(float time_s, float reference_s);

    struct EventRecord
    {
        int result_medals = 0;           // best main result medals (0..3)
        bool objective[2] = {};          // two extra objectives, 1 medal each
        float best_time_s = 0.0f;        // 0 = none
        int best_position = 0;           // 0 = none
        int Medals() const { return result_medals + objective[0] + objective[1]; }
    };

    struct SaveProfile
    {
        static constexpr int kSchemaVersion = 1;
        int schema_version = kSchemaVersion;
        std::string profile_name;
        std::map<std::string, EventRecord> events;
        std::set<std::string> mastery_done;     // "R01_M1".."R01_M3"
        std::map<std::string, float> mastery_progress; // running count per task id (optional in the save)
        std::set<std::string> applied_results;  // idempotency journal: result ids already applied
        int unlocked_region = 1;                // highest open region
        int reputation = 0;                     // cosmetic progression (plan 2.12)
        std::map<std::string, CarSetup> garage; // car id -> parts and paint (D-055; optional in the save)
        DriverLook driver;                      // the player's driver character (D-076; optional in the save)
    };

    struct RaceResult
    {
        std::string result_id; // unique per finished race (e.g. event + session seed + tick)
        std::string event_id;
        int position = 0;       // 0 for timed events
        float time_s = 0.0f;
        float reference_s = 0.0f; // timed events
        bool objective[2] = {};
        bool completed = false; // quitting mid-race is not a completion (plan 2.16)
        int hits = 0;           // hits dealt, for mastery tasks
        int wrecks = 0;         // times wrecked
    };

    // Regional mastery tasks (plan 2.12: three per region; two of them, plus 14 medals, open the
    // duel). They span events, so progress lives in the profile. Authored in content/events.
    enum class MasteryKind { TotalHits, CleanCombatFinishes, GoldInEvents };
    struct MasteryTask
    {
        std::string id;                   // R01_M1
        MasteryKind kind = MasteryKind::TotalHits;
        float target = 1.0f;
        std::vector<std::string> events;  // GoldInEvents: which events count (empty = any)
        std::string text_key;
    };

    struct ApplyOutcome
    {
        bool applied = false;   // false: duplicate or not completed
        int medals_gained = 0;  // only improvements count
        bool duel_unlocked = false;
        bool region_unlocked = false;
        int reputation_gained = 0;
        std::vector<std::string> mastery_completed; // task ids finished by this result
    };

    // Applies a result exactly once and only rewards improvement over the personal best.
    ApplyOutcome ApplyRaceResult(SaveProfile& profile, const RaceResult& result, const std::vector<MasteryTask>& mastery = {});

    int RegionMedals(const SaveProfile& p, int region, int first_events = 7);
    bool DuelUnlocked(const SaveProfile& p, int region);

    // JSON (de)serialisation with migration from older schema versions. A newer, unknown
    // schema is refused rather than silently downgraded.
    std::string SerializeProfile(const SaveProfile& p);
    bool ParseProfile(const std::string& json_text, SaveProfile& out, std::string& error);

    // Plan 2.16 save discipline: temp file -> read back and verify -> keep previous good copy
    // as .bak -> atomic replace. Load falls back to .bak when the main file is damaged.
    struct SaveIO
    {
        static bool Save(const std::string& path, const SaveProfile& p, std::string& error);
        enum class LoadSource { Main, Backup, Fresh };
        static bool Load(const std::string& path, SaveProfile& out, LoadSource& source, std::string& error);
    };

    // Settings live in their own file so a career reset never touches them (plan 2.16).
    // Remappable keyboard actions and the key names the game understands.
    const std::vector<std::string>& ControlActions();
    std::map<std::string, std::string> DefaultBindings();
    bool IsBindableKey(const std::string& key);
    // Assigns key to action; if another action had that key, the two swap (never two actions on
    // one key). False for an unknown action or key.
    bool Rebind(struct Settings& s, const std::string& action, const std::string& key);

    struct Settings
    {
        float master_volume = 1.0f, music_volume = 0.8f, sfx_volume = 1.0f;
        float ui_scale = 1.0f;
        bool reduce_shake = false, motion_blur = true, high_contrast = false;
        float stick_deadzone = 0.15f;
        std::string language = "az";
        // Keyboard remap (plan 2.14): action -> key name. Gamepad bindings are fixed for now.
        std::map<std::string, std::string> bindings = DefaultBindings();
    };
    std::string SerializeSettings(const Settings& s);
    bool ParseSettings(const std::string& json_text, Settings& out, std::string& error);
}
