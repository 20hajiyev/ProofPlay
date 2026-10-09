#include "TestHarness.h"
#include "racer/Career.h"

#include <filesystem>
#include <fstream>
#include <set>

using namespace racer;
namespace fs = std::filesystem;

namespace
{
    RaceResult Race(const std::string& id, const std::string& event, int position, bool o1 = false, bool o2 = false)
    {
        RaceResult r;
        r.result_id = id;
        r.event_id = event;
        r.position = position;
        r.objective[0] = o1;
        r.objective[1] = o2;
        r.completed = true;
        return r;
    }

    // Per-test scratch folder with a non-ASCII name (players' Windows user folders often have one).
    struct Scratch
    {
        fs::path dir;
        explicit Scratch(const char* tag)
        {
            dir = fs::temp_directory_path() / fs::path(std::u8string(u8"racer_test_ə_") + std::u8string(reinterpret_cast<const char8_t*>(tag)));
            fs::remove_all(dir);
            fs::create_directories(dir);
        }
        ~Scratch() { std::error_code ec; fs::remove_all(dir, ec); }
        std::string File(const char* name) const
        {
            const std::u8string u = (dir / name).u8string();
            return std::string(u.begin(), u.end());
        }
    };
}

TEST_CASE("career: event ids and the plan 2.12 event layout")
{
    CHECK(EventId(1, 5) == "R01_E05");
    int r = 0, e = 0;
    CHECK(ParseEventId("R06_E08", r, e) && r == 6 && e == 8);
    CHECK(!ParseEventId("R07_E01", r, e));
    int combat = 0, tt = 0, cp = 0, destr = 0, duel = 0;
    for (int region = 1; region <= 6; ++region)
        for (int ev = 1; ev <= 8; ++ev)
            switch (ModeOfEvent(ev))
            {
            case EventMode::CombatRace: ++combat; break;
            case EventMode::TimeTrial: ++tt; break;
            case EventMode::CheckpointRun: ++cp; break;
            case EventMode::Destruction: ++destr; break;
            case EventMode::RivalDuel: ++duel; break;
            }
    CHECK(combat == 24 && tt == 6 && cp == 6 && destr == 6 && duel == 6);
}

TEST_CASE("career: medal thresholds (positions and 1.05/1.15/1.30 T)")
{
    CHECK(PositionMedals(1) == 3 && PositionMedals(2) == 2 && PositionMedals(3) == 1 && PositionMedals(4) == 0);
    CHECK(TimeMedals(104.9f, 100) == 3);
    CHECK(TimeMedals(114.9f, 100) == 2);
    CHECK(TimeMedals(129.9f, 100) == 1);
    CHECK(TimeMedals(131.0f, 100) == 0);
}

TEST_CASE("career: a result is applied once, only improvements pay, max 5 per event")
{
    SaveProfile p;
    ApplyOutcome a = ApplyRaceResult(p, Race("race-1", "R01_E01", 2));
    CHECK(a.applied && a.medals_gained == 2);
    CHECK(!ApplyRaceResult(p, Race("race-1", "R01_E01", 1)).applied); // same result id again
    a = ApplyRaceResult(p, Race("race-2", "R01_E01", 3, true));
    CHECK(a.applied && a.medals_gained == 1);                       // worse position, but a new objective
    a = ApplyRaceResult(p, Race("race-3", "R01_E01", 1, true, true));
    CHECK(a.medals_gained == 2);
    CHECK(p.events["R01_E01"].Medals() == 5);
    a = ApplyRaceResult(p, Race("race-4", "R01_E01", 1, true, true));
    CHECK(a.applied && a.medals_gained == 0);
    RaceResult quit = Race("race-5", "R01_E02", 1);
    quit.completed = false;                                          // quit mid-race
    CHECK(!ApplyRaceResult(p, quit).applied);
    CHECK(p.events.count("R01_E02") == 0);
}

TEST_CASE("career: duel needs 14 medals from E01-E07 and 2 of 3 mastery tasks; winning it opens R02")
{
    SaveProfile p;
    for (int e = 1; e <= 5; ++e)
    {
        RaceResult r = Race("r" + std::to_string(e), EventId(1, e), 1);
        if (ModeOfEvent(e) == EventMode::TimeTrial || ModeOfEvent(e) == EventMode::CheckpointRun)
        {
            r.position = 0; // timed events are judged on time vs the reference, not position
            r.time_s = 100.0f;
            r.reference_s = 100.0f;
        }
        ApplyRaceResult(p, r); // 3 medals each: 15 in total
    }
    CHECK(RegionMedals(p, 1) == 15);
    CHECK(!DuelUnlocked(p, 1));
    p.mastery_done.insert("R01_M1");
    CHECK(!DuelUnlocked(p, 1));
    p.mastery_done.insert("R01_M3");
    CHECK(DuelUnlocked(p, 1));
    CHECK(!ApplyRaceResult(p, Race("x", "R02_E01", 1)).applied); // region 2 still locked
    const ApplyOutcome duel = ApplyRaceResult(p, Race("duel", "R01_E08", 1));
    CHECK(duel.region_unlocked && p.unlocked_region == 2);
    CHECK(ApplyRaceResult(p, Race("y", "R02_E01", 1)).applied);
}

TEST_CASE("career: profile round-trips; tampering and newer schemas are refused")
{
    SaveProfile p;
    p.profile_name = "Sanan";
    ApplyRaceResult(p, Race("a", "R01_E03", 0));
    ApplyRaceResult(p, Race("b", "R01_E01", 1, true));
    p.mastery_done.insert("R01_M2");
    const std::string text = SerializeProfile(p);
    SaveProfile q;
    std::string err;
    CHECK(ParseProfile(text, q, err));
    CHECK(SerializeProfile(q) == text);
    std::string tampered = text;
    tampered.replace(tampered.find("\"reputation\": ") + 14, 1, "9");
    CHECK(!ParseProfile(tampered, q, err));
    CHECK(err.find("checksum") != std::string::npos);
    std::string newer = text;
    newer.replace(newer.find("\"schema_version\": 1"), 19, "\"schema_version\": 9");
    CHECK(!ParseProfile(newer, q, err));
}

TEST_CASE("save: atomic save, backup fallback, damaged file never overwritten by a fresh profile")
{
    Scratch s("save");
    const std::string path = s.File("career.json");
    SaveProfile p;
    p.profile_name = "first";
    std::string err;
    CHECK(SaveIO::Save(path, p, err));
    p.profile_name = "second";
    CHECK(SaveIO::Save(path, p, err));
    CHECK(!fs::exists(fs::path(std::u8string(path.begin(), path.end()) + u8".tmp")));

    SaveProfile loaded;
    SaveIO::LoadSource src;
    CHECK(SaveIO::Load(path, loaded, src, err) && src == SaveIO::LoadSource::Main && loaded.profile_name == "second");

    { // power loss while writing: main truncated
        std::ofstream f(fs::path(std::u8string(path.begin(), path.end())), std::ios::binary | std::ios::trunc);
        f << "{\"schema_ver";
    }
    CHECK(SaveIO::Load(path, loaded, src, err) && src == SaveIO::LoadSource::Backup && loaded.profile_name == "first");

    fs::remove(fs::path(std::u8string(path.begin(), path.end()) + u8".bak"));
    CHECK(!SaveIO::Load(path, loaded, src, err)); // damaged, no backup: refuse, keep the file
    CHECK(fs::exists(fs::path(std::u8string(path.begin(), path.end()))));

    Scratch fresh("fresh");
    CHECK(SaveIO::Load(fresh.File("none.json"), loaded, src, err) && src == SaveIO::LoadSource::Fresh);
}

TEST_CASE("settings: defaults for missing keys, clamped values, separate from career")
{
    Settings s;
    std::string err;
    CHECK(ParseSettings("{\"music_volume\": 3.0, \"language\": \"xx\", \"ui_scale\": 0.1}", s, err));
    CHECK(s.music_volume == 1.0f && s.language == "az" && s.ui_scale == 0.75f && s.sfx_volume == 1.0f);
    Settings t;
    CHECK(ParseSettings(SerializeSettings(s), t, err));
    CHECK(SerializeSettings(t) == SerializeSettings(s));
}

namespace
{
    // R01's three mastery tasks as authored in content/events/R01.json (D-042).
    std::vector<MasteryTask> R01Mastery()
    {
        return { { "R01_M1", MasteryKind::TotalHits, 25, {}, "mastery.R01_M1" },
                 { "R01_M2", MasteryKind::CleanCombatFinishes, 3, {}, "mastery.R01_M2" },
                 { "R01_M3", MasteryKind::GoldInEvents, 1, { "R01_E03", "R01_E04" }, "mastery.R01_M3" } };
    }
    RaceResult Fight(const std::string& id, const std::string& event, int position, int hits, int wrecks)
    {
        RaceResult r = Race(id, event, position);
        r.hits = hits;
        r.wrecks = wrecks;
        return r;
    }
}

TEST_CASE("mastery: hits accumulate across races and complete M1 exactly once")
{
    SaveProfile p;
    const auto tasks = R01Mastery();
    ApplyOutcome a = ApplyRaceResult(p, Fight("a", "R01_E01", 5, 12, 1), tasks);
    CHECK(a.mastery_completed.empty());
    CHECK(p.mastery_progress["R01_M1"] == 12.0f);
    ApplyRaceResult(p, Fight("a", "R01_E01", 5, 12, 1), tasks); // duplicate result id: ignored
    CHECK(p.mastery_progress["R01_M1"] == 12.0f);
    ApplyOutcome b = ApplyRaceResult(p, Fight("b", "R01_E05", 6, 14, 1), tasks);
    CHECK(b.mastery_completed.size() == 1 && b.mastery_completed[0] == "R01_M1");
    CHECK(p.mastery_done.count("R01_M1") == 1);
    ApplyOutcome c = ApplyRaceResult(p, Fight("c", "R01_E05", 6, 30, 1), tasks);
    CHECK(c.mastery_completed.empty()); // already done: reported once, tracking stops
    CHECK(p.mastery_progress["R01_M1"] == 26.0f);
}

TEST_CASE("mastery: clean finishes count only wreck-free combat races, not other modes")
{
    SaveProfile p;
    const auto tasks = R01Mastery();
    ApplyRaceResult(p, Fight("1", "R01_E01", 4, 0, 0), tasks);    // clean combat race
    ApplyRaceResult(p, Fight("2", "R01_E05", 4, 0, 2), tasks);    // wrecked: no
    ApplyRaceResult(p, Fight("3", "R01_E06", 1, 9, 0), tasks);    // Destruction: not a race
    ApplyRaceResult(p, Fight("4", "R01_E07", 9, 0, 0), tasks);    // clean
    CHECK(p.mastery_progress["R01_M2"] == 2.0f);
    const ApplyOutcome o = ApplyRaceResult(p, Fight("5", "R01_E02", 2, 0, 0), tasks);
    CHECK(o.mastery_completed.size() == 1 && o.mastery_completed[0] == "R01_M2");
    // Abandoned races are not completions and never count.
    SaveProfile q;
    RaceResult quit = Fight("q", "R01_E01", 1, 5, 0);
    quit.completed = false;
    ApplyRaceResult(q, quit, tasks);
    CHECK(q.mastery_progress["R01_M1"] == 0.0f && q.mastery_progress["R01_M2"] == 0.0f);
}

TEST_CASE("mastery: gold in a listed timed event completes M3; silver or another event does not")
{
    SaveProfile p;
    const auto tasks = R01Mastery();
    RaceResult silver = Race("s", "R01_E03", 0);
    silver.reference_s = 100.0f;
    silver.time_s = 110.0f; // 1.10 T: silver
    ApplyRaceResult(p, silver, tasks);
    CHECK(p.mastery_done.count("R01_M3") == 0);
    RaceResult gold = silver;
    gold.result_id = "g";
    gold.time_s = 104.0f; // 1.04 T: gold
    const ApplyOutcome o = ApplyRaceResult(p, gold, tasks);
    CHECK(p.mastery_done.count("R01_M3") == 1);
    CHECK(o.mastery_completed.size() == 1);
}

TEST_CASE("mastery: completing the second task unlocks the duel in the same result")
{
    SaveProfile p;
    const auto tasks = R01Mastery();
    for (int e = 1; e <= 7; ++e)
    {
        p.events[EventId(1, e)].result_medals = 2; // 14 medals, duel still needs mastery
    }
    p.mastery_done.insert("R01_M3");
    CHECK(!DuelUnlocked(p, 1));
    const ApplyOutcome o = ApplyRaceResult(p, Fight("x", "R01_E01", 3, 25, 1), tasks); // M1 done
    CHECK(o.duel_unlocked);
    CHECK(DuelUnlocked(p, 1));
}

TEST_CASE("mastery: progress survives save/load; old saves without it still load")
{
    SaveProfile p;
    p.mastery_progress["R01_M1"] = 17.0f;
    SaveProfile back;
    std::string err;
    CHECK(ParseProfile(SerializeProfile(p), back, err));
    CHECK(back.mastery_progress["R01_M1"] == 17.0f);
}
TEST_CASE("controls: default bindings cover every action with distinct keys")
{
    Settings s;
    CHECK(s.bindings.size() == ControlActions().size());
    std::set<std::string> keys;
    for (const std::string& a : ControlActions())
    {
        CHECK(s.bindings.count(a) == 1);
        CHECK(IsBindableKey(s.bindings[a]));
        keys.insert(s.bindings[a]);
    }
    CHECK(keys.size() == s.bindings.size());
}

TEST_CASE("controls: rebinding to a used key swaps the two actions; bad input is refused")
{
    Settings s;
    const std::string old_throttle = s.bindings["throttle"], old_brake = s.bindings["brake"];
    CHECK(Rebind(s, "throttle", "I"));
    CHECK(s.bindings["throttle"] == "I");
    CHECK(Rebind(s, "throttle", old_brake)); // brake's key: brake takes throttle's (I)
    CHECK(s.bindings["throttle"] == old_brake && s.bindings["brake"] == "I");
    CHECK(!Rebind(s, "throttle", "F13"));   // not a bindable key
    CHECK(!Rebind(s, "fly", "J"));          // not an action
    (void)old_throttle;
}

TEST_CASE("controls: bindings round-trip through settings.json; a broken map falls back to defaults")
{
    Settings s;
    Rebind(s, "handbrake", "B");
    Settings back;
    std::string err;
    CHECK(ParseSettings(SerializeSettings(s), back, err));
    CHECK(back.bindings["handbrake"] == "B");
    Settings dup;
    CHECK(ParseSettings(R"({"bindings":{"throttle":"W","brake":"W"}})", dup, err)); // settings still load
    CHECK(dup.bindings == Settings{}.bindings);                                   // but not a clash
}