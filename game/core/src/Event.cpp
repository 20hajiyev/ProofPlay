#include "racer/Event.h"
#include "racer/Track.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace racer
{
    using nlohmann::json;

    const EventDefinition* RegionEvents::Find(const std::string& id) const
    {
        for (const EventDefinition& e : events)
            if (e.id == id)
                return &e;
        return nullptr;
    }

    namespace
    {
        bool ModeFrom(const std::string& s, EventMode& m)
        {
            if (s == "CombatRace") m = EventMode::CombatRace;
            else if (s == "TimeTrial") m = EventMode::TimeTrial;
            else if (s == "CheckpointRun") m = EventMode::CheckpointRun;
            else if (s == "Destruction") m = EventMode::Destruction;
            else if (s == "RivalDuel") m = EventMode::RivalDuel;
            else return false;
            return true;
        }
        bool ObjectiveFrom(const std::string& s, ObjectiveType& t)
        {
            if (s == "no_wreck") t = ObjectiveType::NoWreck;
            else if (s == "hits_at_least") t = ObjectiveType::HitsAtLeast;
            else if (s == "finish_top") t = ObjectiveType::FinishTopN;
            else if (s == "uses_at_least") t = ObjectiveType::UsesAtLeast;
            else if (s == "damage_under") t = ObjectiveType::NoDamageOver;
            else if (s == "time_under") t = ObjectiveType::TimeUnder;
            else return false;
            return true;
        }
    }

    bool ParseRegionEvents(const std::string& text, RegionEvents& out, std::vector<ValidationIssue>& issues)
    {
        const size_t before = issues.size();
        const json root = json::parse(text, nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            issues.push_back({ "$", "invalid JSON object" });
            return false;
        }
        try
        {
            if (root.at("schema_version") != 1)
                issues.push_back({ "$.schema_version", "must be 1" });
            out.region = root.at("region").get<std::string>();
            out.vehicle_class = root.at("vehicle_class").get<std::string>();
            int index = 0;
            for (const json& e : root.at("events"))
            {
                const std::string p = "$.events[" + std::to_string(index++) + "]";
                EventDefinition d;
                d.id = e.at("id").get<std::string>();
                int region = 0, number = 0;
                if (!ParseEventId(d.id, region, number) || d.id.substr(0, 3) != out.region)
                    issues.push_back({ p + ".id", "must be " + out.region + "_Enn" });
                d.name_key = e.at("name_key").get<std::string>();
                if (!ModeFrom(e.at("mode").get<std::string>(), d.mode))
                    issues.push_back({ p + ".mode", "unknown mode" });
                else if (number >= 1 && d.mode != ModeOfEvent(number))
                    issues.push_back({ p + ".mode", "does not match the plan 2.12 slot for E" + std::to_string(number) });
                d.track = e.at("track").get<std::string>();
                d.participants = e.at("participants").get<int>();
                d.laps = e.at("laps").get<int>();
                if (d.participants < 1 || d.participants > 20)
                    issues.push_back({ p + ".participants", "1..20 (plan: max 20 cars)" });
                if (d.laps < 1 || d.laps > 5)
                    issues.push_back({ p + ".laps", "1..5" });
                const std::string diff = e.at("difficulty").get<std::string>();
                d.difficulty = diff == "Easy" ? AIDifficulty::Easy : diff == "Hard" ? AIDifficulty::Hard : AIDifficulty::Normal;
                for (const json& a : e.at("allowed_abilities"))
                {
                    const Ability ab = AbilityFromString(a.get<std::string>());
                    if (ab == Ability::None)
                        issues.push_back({ p + ".allowed_abilities", "unknown ability" });
                    else
                        d.allowed_abilities.push_back(ab);
                }
                const json& objs = e.at("objectives");
                if (!objs.is_array() || objs.size() != 2)
                    issues.push_back({ p + ".objectives", "exactly two extra objectives (plan 2.12)" });
                else
                    for (size_t k = 0; k < 2; ++k)
                    {
                        if (!ObjectiveFrom(objs[k].at("type").get<std::string>(), d.objectives[k].type))
                            issues.push_back({ p + ".objectives", "unknown objective type" });
                        d.objectives[k].value = objs[k].value("value", 0.0f);
                        d.objectives[k].text_key = objs[k].at("text_key").get<std::string>();
                    }
                const bool timed = d.mode == EventMode::TimeTrial || d.mode == EventMode::CheckpointRun;
                d.reference_time_s = e.value("reference_time_s", 0.0f);
                if (e.contains("weather"))
                {
                    if (!e.at("weather").is_string() || !ParseWeatherKind(e.at("weather").get<std::string>(), d.weather.kind))
                        issues.push_back({ p + ".weather", "clear|overcast|rain|storm|fog" });
                    d.weather.intensity = e.value("weather_intensity", 1.0f);
                    if (d.weather.intensity < 0.0f || d.weather.intensity > 1.0f)
                        issues.push_back({ p + ".weather_intensity", "0..1" });
                }
                d.available = e.value("available", true);
                // Only a playable timed event can have a measured reference; never invent one.
                if (timed && d.available && d.reference_time_s <= 0)
                    issues.push_back({ p + ".reference_time_s", "timed events need a validated reference time" });
                if (timed && d.participants != 1)
                    issues.push_back({ p + ".participants", "timed events are solo" });
                d.unavailable_reason_key = e.value("unavailable_reason_key", std::string());
                if (!d.available && d.unavailable_reason_key.empty())
                    issues.push_back({ p + ".unavailable_reason_key", "say why it is not playable" });
                out.events.push_back(d);
            }

            // Mastery tasks (optional block; R01 authors three).
            if (root.contains("mastery"))
            {
                int mi = 0;
                for (const json& m : root.at("mastery"))
                {
                    const std::string p = "$.mastery[" + std::to_string(mi++) + "]";
                    MasteryTask t;
                    t.id = m.at("id").get<std::string>();
                    if (t.id.rfind(out.region + "_M", 0) != 0)
                        issues.push_back({ p + ".id", "must be " + out.region + "_Mn" });
                    const std::string kind = m.at("kind").get<std::string>();
                    if (kind == "total_hits") t.kind = MasteryKind::TotalHits;
                    else if (kind == "clean_combat_finishes") t.kind = MasteryKind::CleanCombatFinishes;
                    else if (kind == "gold_in_events") t.kind = MasteryKind::GoldInEvents;
                    else issues.push_back({ p + ".kind", "unknown kind" });
                    t.target = m.at("target").get<float>();
                    if (t.target <= 0)
                        issues.push_back({ p + ".target", "must be > 0" });
                    t.text_key = m.at("text_key").get<std::string>();
                    if (m.contains("events"))
                        for (const json& e : m.at("events"))
                        {
                            t.events.push_back(e.get<std::string>());
                            const EventDefinition* ev = out.Find(t.events.back());
                            const bool timed = ev && (ev->mode == EventMode::TimeTrial || ev->mode == EventMode::CheckpointRun);
                            if (t.kind == MasteryKind::GoldInEvents && !timed)
                                issues.push_back({ p + ".events", t.events.back() + " is not a timed event of this region" });
                        }
                    out.mastery.push_back(t);
                }
            }
        }
        catch (const json::exception& ex)
        {
            issues.push_back({ "$", std::string("malformed: ") + ex.what() });
        }
        return issues.size() == before;
    }

    bool ObjectiveMet(const Objective& o, const RaceSummary& s)
    {
        if (!s.finished)
            return false;
        switch (o.type)
        {
        case ObjectiveType::NoWreck: return s.wrecks == 0;
        case ObjectiveType::HitsAtLeast: return s.hits_dealt >= int(o.value);
        case ObjectiveType::FinishTopN: return s.position >= 1 && s.position <= int(o.value);
        case ObjectiveType::UsesAtLeast: return s.uses >= int(o.value);
        case ObjectiveType::NoDamageOver: return s.damage_taken <= o.value;
        case ObjectiveType::TimeUnder: return s.time_s > 0 && s.time_s <= o.value;
        }
        return false;
    }

    RaceResult MakeRaceResult(const EventDefinition& e, const RaceSummary& s, const std::string& result_id)
    {
        RaceResult r;
        r.result_id = result_id;
        r.event_id = e.id;
        r.completed = s.finished;
        const bool timed = e.mode == EventMode::TimeTrial || e.mode == EventMode::CheckpointRun;
        r.position = timed ? 0 : s.position;
        r.time_s = s.time_s;
        r.reference_s = e.reference_time_s;
        r.objective[0] = ObjectiveMet(e.objectives[0], s);
        r.objective[1] = ObjectiveMet(e.objectives[1], s);
        r.hits = s.hits_dealt;
        r.wrecks = s.wrecks;
        return r;
    }

    std::vector<Ability> FilterPickupAbilities(const std::vector<Ability>& authored, const std::vector<Ability>& allowed)
    {
        if (allowed.empty())
            return authored;
        std::vector<Ability> out;
        size_t next = 0;
        for (Ability a : authored)
        {
            if (std::find(allowed.begin(), allowed.end(), a) != allowed.end())
                out.push_back(a);
            else
                out.push_back(allowed[next++ % allowed.size()]);
        }
        return out;
    }
}
