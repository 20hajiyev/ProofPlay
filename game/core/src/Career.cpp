#include "racer/Career.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

namespace racer
{
    using nlohmann::json;
    namespace fs = std::filesystem;

    // UTF-8 std::string -> path (non-ASCII folders such as Azerbaijani user names must work).
    static fs::path P(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

    std::string EventId(int region, int event)
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "R%02d_E%02d", region, event);
        return buf;
    }

    bool ParseEventId(const std::string& id, int& region, int& event)
    {
        // Exactly "Rnn_Enn".
        auto digit = [&](size_t i) { return id[i] >= '0' && id[i] <= '9'; };
        if (id.size() != 7 || id[0] != 'R' || id[3] != '_' || id[4] != 'E' || !digit(1) || !digit(2) || !digit(5) || !digit(6))
            return false;
        region = (id[1] - '0') * 10 + (id[2] - '0');
        event = (id[5] - '0') * 10 + (id[6] - '0');
        return region >= 1 && region <= 6 && event >= 1 && event <= 8;
    }

    EventMode ModeOfEvent(int event)
    {
        switch (event)
        {
        case 3: return EventMode::TimeTrial;
        case 4: return EventMode::CheckpointRun;
        case 6: return EventMode::Destruction;
        case 8: return EventMode::RivalDuel;
        default: return EventMode::CombatRace;
        }
    }

    int PositionMedals(int position)
    {
        return position == 1 ? 3 : position == 2 ? 2 : position == 3 ? 1 : 0;
    }

    int TimeMedals(float t, float T)
    {
        if (t <= 0.0f || T <= 0.0f)
            return 0;
        return t <= 1.05f * T ? 3 : t <= 1.15f * T ? 2 : t <= 1.30f * T ? 1 : 0;
    }

    int RegionMedals(const SaveProfile& p, int region, int first_events)
    {
        int m = 0;
        for (int e = 1; e <= first_events; ++e)
            if (auto it = p.events.find(EventId(region, e)); it != p.events.end())
                m += it->second.Medals();
        return m;
    }

    bool DuelUnlocked(const SaveProfile& p, int region)
    {
        int mastery = 0;
        for (int m = 1; m <= 3; ++m)
        {
            char id[16];
            std::snprintf(id, sizeof(id), "R%02d_M%d", region, m);
            mastery += int(p.mastery_done.count(id));
        }
        return RegionMedals(p, region) >= 14 && mastery >= 2;
    }

    ApplyOutcome ApplyRaceResult(SaveProfile& p, const RaceResult& r, const std::vector<MasteryTask>& mastery)
    {
        ApplyOutcome out;
        int region = 0, event = 0;
        if (!r.completed || r.result_id.empty() || !ParseEventId(r.event_id, region, event) || p.applied_results.count(r.result_id))
            return out;
        if (region > p.unlocked_region)
            return out; // locked content cannot award anything
        p.applied_results.insert(r.result_id);
        out.applied = true;

        const bool duel_before = DuelUnlocked(p, region);
        EventRecord& rec = p.events[r.event_id];
        const int before = rec.Medals();
        const EventMode mode = ModeOfEvent(event);
        const bool timed = mode == EventMode::TimeTrial || mode == EventMode::CheckpointRun;
        const int medals = timed ? TimeMedals(r.time_s, r.reference_s) : PositionMedals(r.position);
        rec.result_medals = std::max(rec.result_medals, medals);
        rec.objective[0] |= r.objective[0];
        rec.objective[1] |= r.objective[1];
        if (r.time_s > 0.0f && (rec.best_time_s == 0.0f || r.time_s < rec.best_time_s))
            rec.best_time_s = r.time_s;
        if (r.position > 0 && (rec.best_position == 0 || r.position < rec.best_position))
            rec.best_position = r.position;

        // Mastery tasks of this result's region, before the duel check so a task finished by this
        // very race can open the duel.
        for (const MasteryTask& t : mastery)
        {
            // Task ids are R<nn>_M<n>; only this result's region counts.
            const bool well_formed = t.id.size() >= 6 && t.id[0] == 'R' && std::isdigit(static_cast<unsigned char>(t.id[1])) &&
                                     std::isdigit(static_cast<unsigned char>(t.id[2])) && t.id.compare(3, 2, "_M") == 0;
            const int task_region = well_formed ? (t.id[1] - '0') * 10 + (t.id[2] - '0') : -1;
            if (task_region != region || p.mastery_done.count(t.id))
                continue;
            float& progress = p.mastery_progress[t.id];
            switch (t.kind)
            {
            case MasteryKind::TotalHits: progress += float(r.hits); break;
            case MasteryKind::CleanCombatFinishes: progress += (mode == EventMode::CombatRace && r.wrecks == 0) ? 1.0f : 0.0f; break;
            case MasteryKind::GoldInEvents:
                if (medals >= 3 && (t.events.empty() || std::find(t.events.begin(), t.events.end(), r.event_id) != t.events.end()))
                    progress += 1.0f;
                break;
            }
            if (progress >= t.target)
            {
                p.mastery_done.insert(t.id);
                out.mastery_completed.push_back(t.id);
            }
        }

        out.medals_gained = rec.Medals() - before;
        out.reputation_gained = 5 + 10 * out.medals_gained;
        p.reputation += out.reputation_gained;
        out.duel_unlocked = !duel_before && DuelUnlocked(p, region);
        // Winning the regional duel opens the next region (plan 2.12).
        if (mode == EventMode::RivalDuel && r.position == 1 && region < 6 && p.unlocked_region < region + 1)
        {
            p.unlocked_region = region + 1;
            out.region_unlocked = true;
        }
        return out;
    }

    namespace
    {
        // FNV-1a over the canonical body: catches bit flips a JSON parse would accept.
        uint64_t Fnv1a(const std::string& s)
        {
            uint64_t h = 1469598103934665603ull;
            for (unsigned char c : s)
            {
                h ^= c;
                h *= 1099511628211ull;
            }
            return h;
        }

        json Body(const SaveProfile& p)
        {
            json events = json::object();
            for (auto& [id, e] : p.events)
                events[id] = { { "result_medals", e.result_medals }, { "objectives", { e.objective[0], e.objective[1] } },
                               { "best_time_s", e.best_time_s }, { "best_position", e.best_position } };
            json body = { { "schema_version", p.schema_version }, { "profile_name", p.profile_name }, { "events", events },
                     { "mastery_done", p.mastery_done }, { "mastery_progress", p.mastery_progress }, { "applied_results", p.applied_results },
                     { "unlocked_region", p.unlocked_region }, { "reputation", p.reputation } };
            if (!p.garage.empty()) // absent when nothing is customised: older saves stay byte-identical
            {
                json garage = json::object();
                for (auto& [car, setup] : p.garage)
                    garage[car] = { { "parts", setup.parts }, { "paint", setup.paint } };
                body["garage"] = garage;
            }
            const DriverLook& d = p.driver;
            if (d.head || d.skin || d.hair || d.jacket) // absent when default (older saves stay identical)
                body["driver"] = { { "head", d.head }, { "skin", d.skin }, { "hair", d.hair }, { "jacket", d.jacket } };
            return body;
        }

        // Upgrades an older document in place, one version at a time. Empty until a second
        // schema exists; the loop and the refusal of newer versions are what is tested now.
        bool Migrate(json& doc, std::string& error)
        {
            int v = doc.value("schema_version", 0);
            if (v < 1)
            {
                error = "missing or invalid schema_version";
                return false;
            }
            if (v > SaveProfile::kSchemaVersion)
            {
                error = "save is from a newer game version (schema " + std::to_string(v) + ")";
                return false;
            }
            while (v < SaveProfile::kSchemaVersion)
            {
                // e.g. if (v == 1) { ...transform to 2... }
                ++v;
                doc["schema_version"] = v;
            }
            return true;
        }
    }

    std::string SerializeProfile(const SaveProfile& p)
    {
        json body = Body(p);
        const std::string canonical = body.dump();
        body["checksum"] = std::to_string(Fnv1a(canonical));
        return body.dump(1);
    }

    bool ParseProfile(const std::string& text, SaveProfile& out, std::string& error)
    {
        json doc = json::parse(text, nullptr, false);
        if (doc.is_discarded() || !doc.is_object())
        {
            error = "not valid JSON";
            return false;
        }
        if (!doc.contains("checksum") || !doc["checksum"].is_string())
        {
            error = "missing checksum";
            return false;
        }
        const std::string stored = doc["checksum"].get<std::string>();
        doc.erase("checksum");
        if (std::to_string(Fnv1a(doc.dump())) != stored)
        {
            error = "checksum mismatch (damaged file)";
            return false;
        }
        if (!Migrate(doc, error))
            return false;
        try
        {
            SaveProfile p;
            p.schema_version = doc.at("schema_version").get<int>();
            p.profile_name = doc.at("profile_name").get<std::string>();
            for (auto& [id, e] : doc.at("events").items())
            {
                EventRecord r;
                r.result_medals = std::clamp(e.at("result_medals").get<int>(), 0, 3);
                r.objective[0] = e.at("objectives").at(0).get<bool>();
                r.objective[1] = e.at("objectives").at(1).get<bool>();
                r.best_time_s = e.at("best_time_s").get<float>();
                r.best_position = e.at("best_position").get<int>();
                p.events[id] = r;
            }
            p.mastery_done = doc.at("mastery_done").get<std::set<std::string>>();
            if (doc.contains("mastery_progress")) // added 2026-09-29; older saves have none
                p.mastery_progress = doc.at("mastery_progress").get<std::map<std::string, float>>();
            p.applied_results = doc.at("applied_results").get<std::set<std::string>>();
            p.unlocked_region = std::clamp(doc.at("unlocked_region").get<int>(), 1, 6);
            p.reputation = doc.at("reputation").get<int>();
            if (doc.contains("garage")) // added 2026-09-29 (D-055); older saves have none
                for (auto& [car, g] : doc.at("garage").items())
                {
                    CarSetup setup;
                    setup.parts = g.at("parts").get<std::map<std::string, std::string>>();
                    setup.paint = std::clamp(g.at("paint").get<int>(), -1, kPaintPresetCount - 1);
                    p.garage[car] = setup;
                }
            if (doc.contains("driver")) // added 2026-10-02 (D-076)
            {
                const auto& d = doc.at("driver");
                p.driver.head = std::clamp(d.value("head", 0), 0, kDriverHeads - 1);
                p.driver.skin = std::clamp(d.value("skin", 0), 0, kSkinTones - 1);
                p.driver.hair = std::clamp(d.value("hair", 0), 0, kHairColours - 1);
                p.driver.jacket = std::clamp(d.value("jacket", 0), 0, kJacketColours - 1);
            }
            out = std::move(p);
            return true;
        }
        catch (const json::exception& e)
        {
            error = std::string("malformed profile: ") + e.what();
            return false;
        }
    }

    namespace
    {
        bool ReadFile(const std::string& path, std::string& out)
        {
            std::ifstream f(P(path), std::ios::binary);
            if (!f)
                return false;
            std::stringstream s;
            s << f.rdbuf();
            out = s.str();
            return true;
        }
    }

    bool SaveIO::Save(const std::string& path, const SaveProfile& p, std::string& error)
    {
        const fs::path target = P(path);
        const fs::path tmp = P(path + ".tmp");
        const fs::path bak = P(path + ".bak");
        const std::string text = SerializeProfile(p);
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f || !(f << text) || !f.flush())
            {
                error = "cannot write temporary file";
                return false;
            }
        }
        // Verify what actually reached the disk before trusting it.
        std::string back;
        SaveProfile check;
        if (!ReadFile(path + ".tmp", back) || back != text || !ParseProfile(back, check, error))
        {
            error = "verification of the temporary file failed: " + error;
            return false;
        }
        std::error_code ec;
        if (fs::exists(target, ec))
        {
            // Keep the previous file only if it is itself healthy; never back up a broken save
            // over a good backup.
            std::string current;
            SaveProfile old;
            std::string ignored;
            if (ReadFile(path, current) && ParseProfile(current, old, ignored))
            {
                fs::copy_file(target, bak, fs::copy_options::overwrite_existing, ec);
                if (ec)
                {
                    error = "cannot keep backup: " + ec.message();
                    return false;
                }
            }
        }
        fs::rename(tmp, target, ec); // MoveFileEx(REPLACE_EXISTING) on Windows: atomic on one volume
        if (ec)
        {
            error = "cannot replace save: " + ec.message();
            return false;
        }
        return true;
    }

    bool SaveIO::Load(const std::string& path, SaveProfile& out, LoadSource& source, std::string& error)
    {
        std::string text, main_error, bak_error;
        if (ReadFile(path, text) && ParseProfile(text, out, main_error))
        {
            source = LoadSource::Main;
            return true;
        }
        if (ReadFile(path + ".bak", text) && ParseProfile(text, out, bak_error))
        {
            source = LoadSource::Backup;
            error = main_error.empty() ? "main save missing" : main_error;
            return true;
        }
        std::error_code ec;
        if (fs::exists(P(path), ec))
        {
            // Damaged and no usable backup: do not hand out a fresh profile that the caller
            // would then save over the player's (possibly recoverable) file.
            error = "save damaged and no valid backup: " + main_error;
            return false;
        }
        out = SaveProfile{};
        source = LoadSource::Fresh;
        return true;
    }

    const std::vector<std::string>& ControlActions()
    {
        static const std::vector<std::string> actions = { "throttle", "brake", "steer_left", "steer_right", "handbrake",
                                                          "fire_forward", "fire_back", "cycle", "drop", "reset",
                                                          // car features (D-060)
                                                          "look_back", "camera", "horn", "lights", "indicator_left",
                                                          "indicator_right", "wipers", "radio", "taunt", "weapon", "hazard", "dome", "visor" };
        return actions;
    }

    std::map<std::string, std::string> DefaultBindings()
    {
        return { { "throttle", "W" }, { "brake", "S" }, { "steer_left", "A" }, { "steer_right", "D" }, { "handbrake", "Space" },
                 { "fire_forward", "LShift" }, { "fire_back", "LCtrl" }, { "cycle", "Q" }, { "drop", "R" }, { "reset", "F" },
                 // plan 2.14: Tab looks back, C changes the camera
                 { "look_back", "Tab" }, { "camera", "C" }, { "horn", "H" }, { "lights", "L" }, { "indicator_left", "Z" },
                 { "indicator_right", "X" }, { "wipers", "V" }, { "radio", "N" }, { "taunt", "G" }, { "weapon", "B" },
                 { "hazard", "J" }, { "dome", "K" }, { "visor", "U" } };
    }

    bool IsBindableKey(const std::string& key)
    {
        if (key.size() == 1)
            return (key[0] >= 'A' && key[0] <= 'Z') || (key[0] >= '0' && key[0] <= '9');
        static const char* named[] = { "Space", "LShift", "RShift", "LCtrl", "RCtrl", "Up", "Down", "Left", "Right", "Tab", "Enter" };
        for (const char* n : named)
            if (key == n)
                return true;
        return false;
    }

    bool Rebind(Settings& s, const std::string& action, const std::string& key)
    {
        if (!IsBindableKey(key) || std::find(ControlActions().begin(), ControlActions().end(), action) == ControlActions().end())
            return false;
        const std::string previous = s.bindings[action];
        for (auto& [other, k] : s.bindings)
            if (other != action && k == key)
                k = previous; // swap
        s.bindings[action] = key;
        return true;
    }

    std::string SerializeSettings(const Settings& s)
    {
        return json{ { "master_volume", s.master_volume }, { "music_volume", s.music_volume }, { "sfx_volume", s.sfx_volume },
                     { "ui_scale", s.ui_scale }, { "reduce_shake", s.reduce_shake }, { "motion_blur", s.motion_blur },
                     { "high_contrast", s.high_contrast }, { "stick_deadzone", s.stick_deadzone }, { "language", s.language },
                     { "bindings", s.bindings } }
            .dump(1);
    }

    bool ParseSettings(const std::string& text, Settings& out, std::string& error)
    {
        const json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object())
        {
            error = "not valid JSON";
            return false;
        }
        Settings s; // missing keys keep defaults: settings must never block the game from starting
        auto f = [&](const char* k, float& v, float lo, float hi) {
            if (j.contains(k) && j[k].is_number())
                v = std::clamp(j[k].get<float>(), lo, hi);
        };
        auto b = [&](const char* k, bool& v) {
            if (j.contains(k) && j[k].is_boolean())
                v = j[k].get<bool>();
        };
        f("master_volume", s.master_volume, 0, 1);
        f("music_volume", s.music_volume, 0, 1);
        f("sfx_volume", s.sfx_volume, 0, 1);
        f("ui_scale", s.ui_scale, 0.75f, 2.0f);
        f("stick_deadzone", s.stick_deadzone, 0.0f, 0.5f);
        b("reduce_shake", s.reduce_shake);
        b("motion_blur", s.motion_blur);
        b("high_contrast", s.high_contrast);
        if (j.contains("language") && j["language"].is_string() && (j["language"] == "az" || j["language"] == "en"))
            s.language = j["language"].get<std::string>();
        if (j.contains("bindings") && j["bindings"].is_object())
        {
            // All-or-nothing: a hand-edited map with a clash or an unknown key falls back to the
            // defaults rather than leaving an action unreachable.
            Settings mapped;
            bool ok = true;
            std::set<std::string> used;
            for (const std::string& a : ControlActions())
            {
                if (j["bindings"].contains(a) && j["bindings"][a].is_string())
                    mapped.bindings[a] = j["bindings"][a].get<std::string>();
                ok = ok && IsBindableKey(mapped.bindings[a]) && used.insert(mapped.bindings[a]).second;
            }
            if (ok)
                s.bindings = mapped.bindings;
        }
        out = s;
        return true;
    }
}
