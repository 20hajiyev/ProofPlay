#pragma once
#include "racer/AI.h"
#include "racer/Career.h"
#include "racer/Combat.h"
#include "racer/Vehicle.h"
#include "racer/Weather.h"

#include <string>
#include <vector>

namespace racer
{
    enum class ObjectiveType { NoWreck, HitsAtLeast, FinishTopN, UsesAtLeast, NoDamageOver, TimeUnder };

    struct Objective
    {
        ObjectiveType type = ObjectiveType::NoWreck;
        float value = 0;
        std::string text_key; // localisation key for the HUD/result screen
    };

    // Plan 2.12 event: mode, layout, field size, laps, allowed abilities, two extra objectives.
    struct EventDefinition
    {
        std::string id;          // R01_E05
        std::string name_key;
        EventMode mode = EventMode::CombatRace;
        std::string track;       // track id, e.g. S01
        int participants = 12;   // including the player
        int laps = 3;
        AIDifficulty difficulty = AIDifficulty::Normal;
        std::vector<Ability> allowed_abilities;
        Objective objectives[2];
        float reference_time_s = 0; // timed modes: validated reference time T
        WeatherState weather;       // optional "weather"/"weather_intensity" (D-083); old files stay clear
        bool available = true;      // false while its track or mode is not built yet
        std::string unavailable_reason_key;
    };

    struct RegionEvents
    {
        std::string region; // R01
        std::string vehicle_class; // D
        std::vector<EventDefinition> events;
        std::vector<MasteryTask> mastery; // plan 2.12: three per region
        const EventDefinition* Find(const std::string& id) const;
    };

    bool ParseRegionEvents(const std::string& json_text, RegionEvents& out, std::vector<ValidationIssue>& issues);

    // What the player did in one finished race; objectives are judged from this only.
    struct RaceSummary
    {
        int position = 0;
        int participants = 0;
        float time_s = 0;
        int wrecks = 0;
        int hits_dealt = 0;
        int uses = 0;
        float damage_taken = 0;
        bool finished = false;
    };

    bool ObjectiveMet(const Objective& o, const RaceSummary& s);

    // Builds the save-side result for a finished (or abandoned) race.
    RaceResult MakeRaceResult(const EventDefinition& e, const RaceSummary& s, const std::string& result_id);

    // Replaces disallowed pickup abilities by cycling through the allowed ones, keeping pad count
    // and positions so the layout's line choices stay intact.
    std::vector<Ability> FilterPickupAbilities(const std::vector<Ability>& authored, const std::vector<Ability>& allowed);
}
