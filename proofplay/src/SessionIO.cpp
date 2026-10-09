// Session file format and end-state checksum. Engine-free apart from reading the race, so the game
// (recording) and the proofplay tool (replay) share exactly the same code.
#include "Session.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace racer;
using namespace racer::runtime;
using json = nlohmann::json;

namespace proofplay
{
    namespace
    {
        void Mix(uint64_t& h, const void* data, size_t n)
        {
            const auto* b = static_cast<const unsigned char*>(data);
            for (size_t i = 0; i < n; ++i)
            {
                h ^= b[i];
                h *= 1099511628211ull;
            }
        }
    }

    // Same source, same compiler flags: the game and the tool agree on this.
    std::string BuildId() { return "rival-line-proofplay-1"; }

    bool RaceRunning(const RaceSession& race)
    {
        return race.Rules().Phase() != RacePhase::Finished && !race.TimeUp();
    }

    EndState ComputeEnd(RaceSession& race, int player)
    {
        EndState e;
        e.ticks = race.Tick();
        e.player_time_s = race.RaceTime(size_t(player));
        e.player_position = race.Rules().Participant(player).position;
        e.player_finished = race.Rules().Participant(player).finished;
        uint64_t h = 1469598103934665603ull;
        for (size_t c = 0; c < race.CarCount(); ++c)
        {
            const VehicleTelemetry& v = race.Car(c).Telemetry();
            Mix(h, v.position, sizeof(v.position));
            Mix(h, v.velocity, sizeof(v.velocity));
            const float health = race.Combat().Get(int(c)).health;
            Mix(h, &health, sizeof(health));
        }
        e.hash = h;
        return e;
    }

    void StatsTracker::Step(RaceSession& race, int player, bool reset)
    {
        resets_ += reset;
        const VehicleTelemetry& t = race.Car(size_t(player)).Telemetry();
        const float kmh = std::fabs(t.forward_speed_kmh);
        if (race.Rules().Phase() == RacePhase::Racing && kmh > 5.0f)
        {
            top_ = std::max(top_, kmh);
            speed_sum_ += kmh;
            ++moving_steps_;
        }
        const int laps = race.Rules().Participant(player).laps_done;
        if (laps > laps_seen_)
        {
            const float now = race.RaceTime(size_t(player));
            laps_.push_back(now - lap_start_);
            lap_start_ = now;
            laps_seen_ = laps;
        }
    }

    RaceStats StatsTracker::Finish(RaceSession& race, int player) const
    {
        const RaceSummary sum = race.Summary(size_t(player));
        RaceStats s;
        s.valid = true;
        s.position = sum.position;
        s.participants = sum.participants;
        s.time_s = sum.time_s;
        s.finished = sum.finished;
        s.uses = sum.uses;
        s.hits_dealt = sum.hits_dealt;
        s.wrecks = sum.wrecks;
        s.wrecks_caused = race.WrecksCaused(size_t(player));
        s.damage_taken = sum.damage_taken;
        s.resets = resets_;
        s.top_speed_kmh = top_;
        s.avg_speed_kmh = moving_steps_ ? float(speed_sum_ / double(moving_steps_)) : 0.0f;
        s.lap_times = laps_;
        return s;
    }

    std::string Mismatch(const EndState& c, const EndState& r)
    {
        std::string m;
        if (c.ticks != r.ticks)
            m += "ticks ";
        if (c.player_time_s != r.player_time_s)
            m += "time ";
        if (c.player_position != r.player_position)
            m += "position ";
        if (c.player_finished != r.player_finished)
            m += "finished ";
        if (c.hash != r.hash)
            m += "state-hash ";
        return m;
    }

    std::string ToJson(const Session& s)
    {
        json j;
        j["format"] = "proofplay-session/2";
        const SessionSetup& u = s.setup;
        j["setup"] = { { "player", u.player }, { "track", u.track }, { "laps", u.laps }, { "cars", u.cars },
                       { "player_index", u.player_index }, { "grid", u.grid }, { "player_parts", u.player_parts },
                       { "difficulty", u.difficulty }, { "mode", u.mode }, { "weather_kind", u.weather_kind },
                       { "weather_intensity", u.weather_intensity }, { "time_limit_s", u.time_limit_s },
                       { "restrict_abilities", u.restrict_abilities }, { "allowed_abilities", u.allowed_abilities },
                       { "build_id", u.build_id } };
        json in = json::array();
        for (const StepInput& i : s.inputs)
            in.push_back({ i.drive.throttle, i.drive.brake, i.drive.steer, i.drive.handbrake, i.drive.events,
                           i.combat.use, int(i.combat.direction), i.combat.drop, i.combat.cycle, i.combat.select,
                           i.combat.fire_mounted, i.reset });
        j["inputs"] = std::move(in);
        if (s.stats.valid)
        {
            const RaceStats& r = s.stats;
            j["stats"] = { { "position", r.position }, { "participants", r.participants }, { "time_s", r.time_s }, { "finished", r.finished },
                           { "uses", r.uses }, { "hits_dealt", r.hits_dealt }, { "wrecks", r.wrecks }, { "wrecks_caused", r.wrecks_caused },
                           { "resets", r.resets }, { "damage_taken", r.damage_taken }, { "top_speed_kmh", r.top_speed_kmh },
                           { "avg_speed_kmh", r.avg_speed_kmh }, { "lap_times", r.lap_times } };
        }
        j["end"] = { { "ticks", s.end.ticks }, { "player_time_s", s.end.player_time_s }, { "player_position", s.end.player_position },
                     { "player_finished", s.end.player_finished }, { "hash", std::to_string(s.end.hash) } };
        return j.dump();
    }

    bool FromJson(const std::string& text, Session& out, std::string& error)
    {
        try
        {
            const json j = json::parse(text);
            const std::string format = j.value("format", "");
            if (format != "proofplay-session/1" && format != "proofplay-session/2")
                throw std::runtime_error("not a proofplay session");
            const json& s = j.at("setup");
            SessionSetup& u = out.setup;
            u = SessionSetup{};
            u.player = s.value("player", "Bot");
            u.track = s.at("track");
            u.laps = s.at("laps");
            u.cars = s.at("cars");
            u.player_index = s.value("player_index", 0);
            u.grid = s.value("grid", std::vector<std::string>{});
            u.player_parts = s.value("player_parts", std::map<std::string, std::string>{});
            u.difficulty = s.at("difficulty");
            u.mode = s.value("mode", 0);
            u.weather_kind = s.value("weather_kind", 0);
            u.weather_intensity = s.value("weather_intensity", 1.0f);
            u.time_limit_s = s.value("time_limit_s", 0.0f);
            u.restrict_abilities = s.value("restrict_abilities", false);
            u.allowed_abilities = s.value("allowed_abilities", std::vector<int>{});
            u.build_id = s.value("build_id", "");
            out.inputs.clear();
            for (const json& a : j.at("inputs"))
            {
                StepInput i;
                i.drive.throttle = a[0];
                i.drive.brake = a[1];
                i.drive.steer = a[2];
                i.drive.handbrake = a[3];
                i.drive.events = a[4];
                i.combat.use = a[5];
                i.combat.direction = UseDirection(int(a[6]));
                i.combat.drop = a[7];
                i.combat.cycle = a[8];
                i.combat.select = a[9];
                i.combat.fire_mounted = a[10];
                i.reset = a.size() > 11 && a[11].get<bool>();
                out.inputs.push_back(i);
            }
            out.stats = RaceStats{};
            if (j.contains("stats"))
            {
                const json& r = j["stats"];
                out.stats.valid = true;
                out.stats.position = r.value("position", 0);
                out.stats.participants = r.value("participants", 0);
                out.stats.time_s = r.value("time_s", 0.0f);
                out.stats.finished = r.value("finished", false);
                out.stats.uses = r.value("uses", 0);
                out.stats.hits_dealt = r.value("hits_dealt", 0);
                out.stats.wrecks = r.value("wrecks", 0);
                out.stats.wrecks_caused = r.value("wrecks_caused", 0);
                out.stats.resets = r.value("resets", 0);
                out.stats.damage_taken = r.value("damage_taken", 0.0f);
                out.stats.top_speed_kmh = r.value("top_speed_kmh", 0.0f);
                out.stats.avg_speed_kmh = r.value("avg_speed_kmh", 0.0f);
                out.stats.lap_times = r.value("lap_times", std::vector<float>{});
            }
            const json& e = j.at("end");
            out.end.ticks = e.at("ticks");
            out.end.player_time_s = e.at("player_time_s");
            out.end.player_position = e.at("player_position");
            out.end.player_finished = e.at("player_finished");
            out.end.hash = std::stoull(e.at("hash").get<std::string>());
            return true;
        }
        catch (const std::exception& ex)
        {
            error = ex.what();
            return false;
        }
    }
}
