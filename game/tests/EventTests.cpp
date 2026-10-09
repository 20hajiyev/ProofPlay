#include "TestHarness.h"
#include "racer/Event.h"
#include "racer/Localization.h"

#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

using namespace racer;

namespace
{
    std::string ReadContent(const char* rel)
    {
        std::ifstream f(std::string(RACER_CONTENT_DIR) + "/" + rel, std::ios::binary);
        std::stringstream s;
        s << f.rdbuf();
        return s.str();
    }
}

TEST_CASE("events: R01 definition is valid and follows the plan 2.12 slots")
{
    RegionEvents r;
    std::vector<ValidationIssue> issues;
    CHECK(ParseRegionEvents(ReadContent("events/R01.json"), r, issues));
    for (auto& i : issues)
        std::printf("  ISSUE %s: %s\n", i.path.c_str(), i.message.c_str());
    CHECK(r.events.size() == 8);
    const EventDefinition* e1 = r.Find("R01_E01");
    CHECK(e1 && e1->participants == 12 && e1->laps == 3);
    // Plan: R01's first event only has Surge, Ward and Mend.
    CHECK(e1 && e1->allowed_abilities.size() == 3);
    int playable = 0;
    for (auto& e : r.events)
        playable += e.available;
    std::printf("  INFO R01: %d/8 events playable now\n", playable);
    CHECK(playable >= 3);
}

TEST_CASE("events: a mode in the wrong slot and a timed event without reference are rejected")
{
    auto j = nlohmann::json::parse(ReadContent("events/R01.json"));
    j["events"][0]["mode"] = "TimeTrial";
    RegionEvents r;
    std::vector<ValidationIssue> issues;
    CHECK(!ParseRegionEvents(j.dump(), r, issues));
    j = nlohmann::json::parse(ReadContent("events/R01.json"));
    j["events"][2].erase("reference_time_s");
    issues.clear();
    CHECK(!ParseRegionEvents(j.dump(), r, issues));
}

TEST_CASE("events: objectives and result construction")
{
    RaceSummary s;
    s.finished = true;
    s.position = 2;
    s.hits_dealt = 4;
    s.uses = 3;
    s.time_s = 110;
    CHECK(ObjectiveMet({ ObjectiveType::FinishTopN, 3 }, s));
    CHECK(!ObjectiveMet({ ObjectiveType::FinishTopN, 1 }, s));
    CHECK(ObjectiveMet({ ObjectiveType::HitsAtLeast, 4 }, s));
    CHECK(ObjectiveMet({ ObjectiveType::NoWreck }, s));
    CHECK(ObjectiveMet({ ObjectiveType::TimeUnder, 113 }, s));
    RaceSummary quit = s;
    quit.finished = false;
    CHECK(!ObjectiveMet({ ObjectiveType::NoWreck }, quit)); // nothing counts from an abandoned race

    RegionEvents r;
    std::vector<ValidationIssue> issues;
    ParseRegionEvents(ReadContent("events/R01.json"), r, issues);
    const RaceResult tt = MakeRaceResult(*r.Find("R01_E03"), s, "id-1");
    CHECK(tt.position == 0 && tt.reference_s == r.Find("R01_E03")->reference_time_s && tt.objective[0]);
}

TEST_CASE("events: disallowed pickups are remapped, pad count unchanged")
{
    const std::vector<Ability> authored = { Ability::Lance, Ability::Surge, Ability::Storm, Ability::Ward, Ability::Needle };
    const std::vector<Ability> allowed = { Ability::Surge, Ability::Ward, Ability::Mend };
    const auto out = FilterPickupAbilities(authored, allowed);
    CHECK(out.size() == authored.size());
    for (Ability a : out)
        CHECK(a == Ability::Surge || a == Ability::Ward || a == Ability::Mend);
    CHECK(out[1] == Ability::Surge && out[3] == Ability::Ward); // allowed ones stay where authored
}

TEST_CASE("localization: AZ and EN have the same keys and cover every key the events use")
{
    Strings az, en;
    std::string err;
    CHECK(az.Load(ReadContent("text/az.json"), err));
    CHECK(en.Load(ReadContent("text/en.json"), err));
    CHECK(az.Keys() == en.Keys());
    RegionEvents r;
    std::vector<ValidationIssue> issues;
    ParseRegionEvents(ReadContent("events/R01.json"), r, issues);
    for (const EventDefinition& e : r.events)
    {
        CHECK(az.Has(e.name_key));
        for (const Objective& o : e.objectives)
            CHECK(az.Has(o.text_key));
        if (!e.available)
            CHECK(az.Has(e.unavailable_reason_key));
    }
    CHECK(az.Get("no.such.key") == "no.such.key");
    CHECK(az.Get("menu.title").find("Liman") != std::string::npos);
}

TEST_CASE("events: R01 authors three mastery tasks the duel rule can use")
{
    RegionEvents r;
    std::vector<ValidationIssue> issues;
    CHECK(ParseRegionEvents(ReadContent("events/R01.json"), r, issues));
    CHECK(r.mastery.size() == 3);
    for (const MasteryTask& t : r.mastery)
        CHECK(t.id.rfind("R01_M", 0) == 0 && t.target > 0 && !t.text_key.empty());
    // A GoldInEvents task may only name timed events of this region.
    auto j = nlohmann::json::parse(ReadContent("events/R01.json"));
    j["mastery"][2]["events"] = { "R01_E01" };
    RegionEvents bad;
    issues.clear();
    CHECK(!ParseRegionEvents(j.dump(), bad, issues));
}
TEST_CASE("events: optional weather - old files stay clear, rain parses, bad values are rejected (D-083)")
{
    auto j = nlohmann::json::parse(ReadContent("events/R01.json"));
    RegionEvents r;
    std::vector<ValidationIssue> issues;
    CHECK(ParseRegionEvents(j.dump(), r, issues));
    for (const auto& e : r.events)
        if (!j["events"][&e - &r.events[0]].contains("weather"))
            CHECK(e.weather.kind == WeatherKind::Clear);
    j["events"][0]["weather"] = "rain";
    j["events"][0]["weather_intensity"] = 0.6;
    RegionEvents wet; // the parser appends, so every parse gets a fresh result
    issues.clear();
    CHECK(ParseRegionEvents(j.dump(), wet, issues));
    CHECK(wet.events[0].weather.kind == WeatherKind::Rain && std::fabs(wet.events[0].weather.intensity - 0.6f) < 1e-5f);
    j["events"][0]["weather"] = "snow";
    RegionEvents snow;
    issues.clear();
    CHECK(!ParseRegionEvents(j.dump(), snow, issues));
    j["events"][0]["weather"] = "fog";
    j["events"][0]["weather_intensity"] = 1.5;
    RegionEvents dense;
    issues.clear();
    CHECK(!ParseRegionEvents(j.dump(), dense, issues));
}
