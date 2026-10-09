#include "Analyze.h"

#include "racer/AI.h"
#include "racer/runtime/PhysicsStepper.h"

#include <nlohmann/json.hpp>

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <map>

using namespace racer;
using namespace racer::runtime;
using json = nlohmann::json;

namespace proofplay
{
    namespace
    {
        constexpr float kDt = PhysicsStepper::kStepSeconds;

        // Shift 0 is the baseline (the player's own inputs, position-aligned after the action point).
        const float kShifts[] = { 0.0f, -30.0f, -20.0f, -15.0f, -10.0f, -5.0f, 5.0f, 10.0f, 15.0f, 20.0f, 30.0f };
        constexpr size_t kRuns = sizeof(kShifts) / sizeof(kShifts[0]);

        // Line-keeper gains (steer per metre off the player's line, per m/s drifting); see TryPlan.
        const float kLineKp = std::getenv("PP_KP") ? float(std::atof(std::getenv("PP_KP"))) : 0.08f;
        const float kLineKd = std::getenv("PP_KD") ? float(std::atof(std::getenv("PP_KD"))) : 0.12f;

        std::string g_session_path; // set when analysing a file: what-if workers re-read it

        unsigned WorkerCount()
        {
            if (const char* e = std::getenv("PROOFPLAY_WORKERS"))
                return unsigned(std::max(1, std::atoi(e)));
            return std::clamp(std::thread::hardware_concurrency() - 2, 1u, 12u);
        }

        std::string SelfPath()
        {
            char buf[MAX_PATH] = {};
            GetModuleFileNameA(nullptr, buf, MAX_PATH);
            return buf;
        }

        Sample Observe(RaceSession& rs, int pl)
        {
            Sample out;
            const VehicleTelemetry& t = rs.Car(size_t(pl)).Telemetry();
            const ParticipantState& p = rs.Rules().Participant(pl);
            out.tick = rs.Tick();
            out.d = rs.Rules().TotalProgress(p);
            out.s = p.s;
            out.x = t.position[0];
            out.z = t.position[2];
            out.speed = std::sqrt(t.velocity[0] * t.velocity[0] + t.velocity[2] * t.velocity[2]);
            out.lateral = rs.Route().Project({ out.x, out.z }, p.s).lateral;
            return out;
        }

        // The ideal speed along the route for the player's car (what a perfect Hard driver aims for).
        struct Ideal
        {
            SpeedProfile profile;
            float length = 0;
            float max_speed = 0;
        };

        // Building it needs a whole race (physics world and all cars), so it is made once per
        // track/car/parts and reused by corner finding, proofs and evaluation.
        const Ideal& MakeIdeal(const SessionSetup& setup)
        {
            static std::map<std::string, Ideal> cache;
            std::string key = setup.track + "|" + std::to_string(setup.player_index) + "|";
            if (size_t(setup.player_index) < setup.grid.size())
                key += setup.grid[size_t(setup.player_index)];
            for (const auto& [slot, part] : setup.player_parts)
                key += "|" + slot + "=" + part;
            if (auto it = cache.find(key); it != cache.end())
                return it->second;
            Race race(setup);
            RaceSession& rs = race.Session();
            AIDriver hard(rs.Route(), rs.Car(size_t(race.Player())).Definition(), MakeAIParams(AIDifficulty::Hard, 1));
            Ideal i{ hard.Profile(), rs.Route().Length(), 0.0f };
            for (float s = 0; s < i.length; s += 2.0f)
                i.max_speed = std::max(i.max_speed, i.profile.At(s));
            return cache.emplace(key, std::move(i)).first->second;
        }

        float Wrap(float s, float length)
        {
            s = std::fmod(s, length);
            return s < 0 ? s + length : s;
        }

        // Forward distance from a to b on a loop of this length.
        float Ahead(float a, float b, float length) { return Wrap(b - a, length); }

        // One pass of the player through one corner segment.
        struct Passage
        {
            int corner = 0;
            int lap = 0;
            size_t first = 0, last = 0; // sample indices
            float time_s = 0, ideal_s = 0, loss_s = 0;
            float mean_abs_lateral = 0;
        };

        std::vector<Passage> FindPassages(const Trace& t, const std::vector<Corner>& corners, const Ideal& ideal)
        {
            std::vector<Passage> out;
            for (const Corner& c : corners)
            {
                const float len = Ahead(c.entry_s, c.exit_s, ideal.length);
                float ideal_s = 0;
                for (float x = 0; x < len; x += 1.0f)
                    ideal_s += 1.0f / std::max(1.0f, ideal.profile.At(Wrap(c.entry_s + x, ideal.length)));
                bool inside = false;
                Passage cur;
                for (size_t i = 0; i < t.samples.size(); ++i)
                {
                    const Sample& sm = t.samples[i];
                    const bool in = Ahead(c.entry_s, sm.s, ideal.length) < len && sm.speed > 1.0f;
                    // A pass must enter at racing speed: the standing start rolls through the
                    // segment by the grid and is not a corner taken by the player.
                    if (in && !inside && sm.speed < 0.5f * ideal.profile.At(sm.s))
                        continue;
                    if (in && !inside)
                    {
                        cur = Passage{};
                        cur.corner = c.id;
                        cur.first = i;
                        cur.lap = int(sm.d / ideal.length);
                    }
                    if (!in && inside)
                    {
                        cur.last = i - 1;
                        cur.time_s = float(cur.last - cur.first + 1) * kDt;
                        cur.ideal_s = ideal_s;
                        cur.loss_s = cur.time_s - ideal_s;
                        float lat = 0;
                        for (size_t k = cur.first; k <= cur.last; ++k)
                            lat += std::fabs(t.samples[k].lateral);
                        cur.mean_abs_lateral = lat / float(cur.last - cur.first + 1);
                        if (cur.time_s < ideal_s * 3.0f && cur.time_s > ideal_s * 0.6f) // crash/reset or a clipped pass
                            out.push_back(cur);
                    }
                    inside = in;
                }
            }
            return out;
        }

        float Clamp99(float v) { return std::clamp(v, 1.0f, 99.0f); }
        // Linear map raw -> rating, anchored at two calibration points (lo -> 40, hi -> 90).
        float Rate(float raw, float lo, float hi) { return Clamp99(40.0f + (raw - lo) / (hi - lo) * 50.0f); }
    }

    Trace Simulate(const SessionSetup& setup, const Policy& policy, size_t max_steps, const Stop& stop, bool keep_samples)
    {
        Trace t;
        Race race(setup);
        RaceSession& rs = race.Session();
        const int pl = race.Player();
        int laps_seen = 0;
        float lap_start = 0;
        for (size_t step = 0; step < max_steps && race.Running(); ++step)
        {
            Sample now = Observe(rs, pl);
            if (stop && stop(now))
                break;
            now.input = policy(step, now);
            race.Step(now.input);
            if (keep_samples)
                t.samples.push_back(now);
            for (const CombatEvent& e : rs.Combat().Events())
                if (e.source == pl || e.target == pl)
                    t.events.push_back(e);
            const int laps = rs.Rules().Participant(pl).laps_done;
            if (laps > laps_seen)
            {
                const float now_s = rs.RaceTime(size_t(pl));
                t.lap_times.push_back(now_s - lap_start);
                lap_start = now_s;
                laps_seen = laps;
            }
        }
        t.recoveries = rs.GetStats().recoveries;
        t.end = race.End();
        return t;
    }

    Trace Replay(const Session& s)
    {
        return Simulate(s.setup, [&](size_t step, const Sample&) { return s.inputs[step]; }, s.inputs.size());
    }

    std::vector<Corner> FindCorners(const SessionSetup& setup)
    {
        const Ideal& ideal = MakeIdeal(setup);
        std::vector<Corner> out;
        const float step = 2.0f;
        for (float s = 0; s < ideal.length; s += step)
        {
            const float v = ideal.profile.At(s);
            if (v > 0.85f * ideal.max_speed)
                continue;
            bool minimum = true;
            for (float o = -30.0f; o <= 30.0f && minimum; o += step)
                minimum = ideal.profile.At(Wrap(s + o, ideal.length)) >= v;
            if (!minimum)
                continue;
            if (!out.empty() && Ahead(out.back().apex_s, s, ideal.length) < 40.0f)
                continue;
            Corner c;
            c.id = int(out.size()) + 1;
            c.apex_s = s;
            c.entry_s = Wrap(s - 70.0f, ideal.length);
            c.exit_s = Wrap(s + 40.0f, ideal.length);
            c.ideal_speed = v;
            out.push_back(c);
        }
        return out;
    }

    namespace
    {
        // Re-simulates one corner pass with the braking point moved. Up to the change the race is
        // the exact replay; after it the player's own inputs are played back by track position (the
        // car is no longer where it was at a given tick). Combat input is dropped after the change.
        class WhatIf
        {
        public:
            WhatIf(const Session& s, const Trace& t)
                : s_(s), t_(t)
            {
                float run_max = -1e9f;
                for (size_t i = 0; i < t.samples.size(); ++i)
                    if (t.samples[i].d > run_max)
                    {
                        run_max = t.samples[i].d;
                        by_d_.push_back({ run_max, i });
                    }
            }

            struct Outcome
            {
                bool valid = false;  // reached the measuring point
                float time = 0, speed = 0;
                uint32_t recoveries = 0;
            };

            // How the player slowed for this corner. Bots brake; keyboard players mostly just lift
            // off (a measured human session had no braking in 8 of 9 passes), and some take a
            // corner flat out. Each gets its own human-sized change.
            enum class Action { Brake, Lift, Flat };
            struct Point { Action action = Action::Brake; size_t index = 0; };

            Point ActionPoint(const Passage& p) const
            {
                for (size_t i = p.first; i <= p.last; ++i)
                    if (t_.samples[i].input.drive.brake >= 0.2f)
                        return { Action::Brake, i };
                for (size_t i = p.first; i <= p.last; ++i)
                    if (t_.samples[i].input.drive.throttle < 0.5f)
                        return { Action::Lift, i };
                // Flat out: the change is "brake before the apex", anchored a third into the pass.
                return { Action::Flat, p.first + (p.last - p.first) / 3 };
            }

            // metres > 0: act that much earlier (brake, or lift / brake where there was none);
            // metres < 0: stay on the throttle that much longer. Flat passes only take metres > 0.
            bool Try(const Passage& p, float metres, Outcome& out, std::vector<float>* path) const
            {
                const Point at = ActionPoint(p);
                if (at.action == Action::Flat && metres < 0)
                    return false;
                const float d_on = t_.samples[at.index].d;
                // Measure 60 m past the corner exit, but never past where the original race ended.
                const float d_measure = std::min(t_.samples[p.last].d + 60.0f, t_.samples.back().d - 1.0f);
                const float brake = at.action == Action::Lift ? 0.0f : std::max(0.5f, t_.samples[at.index].input.drive.brake);
                bool diverged = false;
                out = Outcome{};
                const Trace r = Simulate(s_.setup,
                    [&](size_t step, const Sample& now) {
                        const float start = metres > 0 ? d_on - metres : d_on;
                        if (!diverged && now.d < start && step < s_.inputs.size())
                            return s_.inputs[step];
                        diverged = true;
                        StepInput in = AtD(now.d);
                        in.combat = CombatCommand{};
                        in.reset = false;
                        if (metres > 0 && now.d < d_on)
                        {
                            in.drive.throttle = 0.0f; // brake (or lift) earlier
                            in.drive.brake = brake;
                        }
                        if (metres < 0 && now.d < d_on - metres)
                        {
                            in.drive.throttle = 1.0f; // brake later: stay on the throttle
                            in.drive.brake = 0.0f;
                        }
                        return in;
                    },
                    s_.inputs.size() + 120 * 20,
                    [&](const Sample& now) {
                        if (path && now.d > d_on - std::fabs(metres) - 30.0f && now.tick % 6 == 0)
                        {
                            path->push_back(now.x);
                            path->push_back(now.z);
                        }
                        if (now.d >= d_measure)
                        {
                            out.valid = true;
                            out.time = float(now.tick) * kDt;
                            out.speed = now.speed;
                            return true;
                        }
                        return false;
                    },
                    false);
                out.recoveries = r.recoveries;
                return true;
            }

            // Advice names per action, the same words the dashboard translates.
            static std::string Kind(Action a, float metres)
            {
                if (a == Action::Flat)
                    return "add_brake";
                if (a == Action::Lift)
                    return metres > 0 ? "lift_earlier" : "lift_later";
                return metres > 0 ? "brake_earlier" : "brake_later";
            }

            // The signed shift an advice kind asks for at this pass; false if it does not fit it.
            bool Shift(const Passage& p, const std::string& kind, float amount, float& metres) const
            {
                const Action a = ActionPoint(p).action;
                for (float sign : { 1.0f, -1.0f })
                    if (Kind(a, sign) == kind && !(a == Action::Flat && sign < 0))
                    {
                        metres = sign * amount;
                        return true;
                    }
                return false;
            }

            // Every proven fix at once, in one re-simulation: fixes interact
            // (a faster exit changes the next corner's entry), so the sum of single gains is only a
            // guess. Only the first `active` changes are applied; divergence and the measuring point
            // come from ALL of them, so active = 0 is a baseline shared by every subset (D-097).
            struct Change { const Passage* p; float metres; };
            bool TryPlan(const std::vector<Change>& changes, size_t active, Outcome& out) const
            {
                struct Window { float start, end, brake; bool slow; };
                std::vector<Window> windows;
                float first = 1e30f;
                for (size_t ci = 0; ci < changes.size(); ++ci)
                {
                    const Change& c = changes[ci];
                    const Point at = ActionPoint(*c.p);
                    const float d_on = t_.samples[at.index].d, m = ci < active ? c.metres : 0.0f;
                    const float brake = at.action == Action::Lift ? 0.0f : std::max(0.5f, t_.samples[at.index].input.drive.brake);
                    if (m > 0)
                        windows.push_back({ d_on - m, d_on, brake, true });
                    else if (m < 0)
                        windows.push_back({ d_on, d_on - m, 0.0f, false });
                    first = std::min(first, c.metres > 0 ? d_on - c.metres : d_on);
                }
                if (changes.empty())
                    return false;
                // Measured 60 m past the last fixed corner, like a single proof: playing the player's
                // inputs back open-loop all the way to the flag drifts into walls (measured: the
                // all-zero baseline needed 2 recoveries on Bot_Qusurlu), so a finish-line gain would
                // measure the drift, not the fixes.
                float d_measure = 0;
                for (const Change& c : changes)
                    d_measure = std::max(d_measure, t_.samples[c.p->last].d + 60.0f);
                d_measure = std::min(d_measure, t_.samples.back().d - 1.0f);
                bool diverged = false;
                float prev_err = 0, max_d = -1e9f;
                uint64_t last_tick = 0;
                out = Outcome{};
                const Trace r = Simulate(s_.setup,
                    [&](size_t step, const Sample& now) {
                        if (!diverged && now.d < first && step < s_.inputs.size())
                            return s_.inputs[step];
                        const float err = now.lateral - LateralAtD(now.d); // + = right of the player's own line
                        if (!diverged)
                            prev_err = err;
                        diverged = true;
                        StepInput in = AtD(now.d);
                        in.combat = CombatCommand{};
                        in.reset = false;
                        // Line keeper: a driver steers back onto their own line; pure position-aligned
                        // playback cannot, and drifts into the wall over a few hundred metres (D-097).
                        in.drive.steer = std::clamp(in.drive.steer - kLineKp * err - kLineKd * (err - prev_err) / kDt, -1.0f, 1.0f);
                        prev_err = err;
                        for (const Window& w : windows)
                            if (now.d >= w.start && now.d < w.end)
                            {
                                in.drive.throttle = w.slow ? 0.0f : 1.0f;
                                in.drive.brake = w.slow ? w.brake : 0.0f;
                            }
                        return in;
                    },
                    s_.inputs.size() + 120 * 20,
                    [&](const Sample& now) {
                        max_d = std::max(max_d, now.d);
                        last_tick = now.tick;
                        if (now.d >= d_measure)
                        {
                            out.valid = true;
                            out.time = float(now.tick) * kDt;
                            out.speed = now.speed;
                            return true;
                        }
                        return false;
                    },
                    false);
                out.recoveries = r.recoveries;
                if (std::getenv("PROOFPLAY_DEBUG"))
                    std::printf("  plan run active=%zu: first %.0f measure %.0f reached %.0f at tick %llu (inputs %zu)\n", active, first, d_measure,
                        max_d, (unsigned long long)last_tick, s_.inputs.size());
                return true;
            }

        private:
            float LateralAtD(float d) const
            {
                auto it = std::lower_bound(by_d_.begin(), by_d_.end(), std::make_pair(d, size_t(0)));
                if (it == by_d_.end())
                    --it;
                return t_.samples[it->second].lateral;
            }

            const StepInput& AtD(float d) const
            {
                auto it = std::lower_bound(by_d_.begin(), by_d_.end(), std::make_pair(d, size_t(0)));
                if (it == by_d_.end())
                    --it;
                return t_.samples[it->second].input;
            }

            const Session& s_;
            const Trace& t_;
            std::vector<std::pair<float, size_t>> by_d_;
        };
    }

    namespace
    {
        std::vector<Passage> SortedPassages(const Trace& t, const std::vector<Corner>& corners, const Ideal& ideal)
        {
            std::vector<Passage> passages = FindPassages(t, corners, ideal);
            std::stable_sort(passages.begin(), passages.end(), [](const Passage& a, const Passage& b) { return a.loss_s > b.loss_s; });
            return passages;
        }
    }

    void SetSessionPath(const std::string& path) { g_session_path = path; }

    namespace
    {
        // Runs "whatif" worker processes, one per non-empty job spec, and returns every result row.
        // A worker that fails contributes nothing; callers treat missing rows as invalid runs.
        std::vector<json> RunWorkers(const std::vector<std::string>& specs)
        {
            std::vector<std::thread> pool;
            std::vector<std::string> outs(specs.size());
            for (size_t w = 0; w < specs.size(); ++w)
            {
                if (specs[w].empty())
                    continue;
                // System temp, not next to the session: a killed analysis used to leave empty
                // files in the player's folder (D-099). PID in the name keeps runs apart.
                std::error_code ec;
                outs[w] = (std::filesystem::temp_directory_path(ec) / ("proofplay_whatif_" + std::to_string(GetCurrentProcessId()) + "_" +
                           std::to_string(w) + ".json")).string();
                pool.emplace_back([&, w] {
                    const std::string cmd = "\"\"" + SelfPath() + "\" whatif \"" + g_session_path + "\" --jobs " + specs[w] + " --out \"" + outs[w] + "\"\"";
                    std::system(cmd.c_str());
                });
            }
            for (auto& th : pool)
                th.join();
            std::vector<json> rows;
            for (const std::string& f : outs)
            {
                if (f.empty())
                    continue;
                std::ifstream in(f, std::ios::binary);
                std::stringstream ss;
                ss << in.rdbuf();
                in.close();
                std::remove(f.c_str());
                if (ss.str().empty())
                    continue;
                for (const json& r : json::parse(ss.str()))
                    rows.push_back(r);
            }
            return rows;
        }

        // Plan job: P<lap>_<active>_<corner>x<metres>y<corner>x<metres>... (letters only: the spec
        // goes through cmd.exe unquoted).
        std::vector<WhatIf::Change> PlanChanges(const std::vector<Passage>& passages, int lap, const std::vector<std::pair<int, float>>& fixes)
        {
            std::vector<WhatIf::Change> changes;
            for (auto [corner, metres] : fixes)
            {
                const auto it = std::find_if(passages.begin(), passages.end(), [&](const Passage& q) { return q.corner == corner && q.lap + 1 == lap; });
                if (it != passages.end())
                    changes.push_back({ &*it, metres });
            }
            return changes;
        }
    }

    std::string RunWhatIfJobs(const Session& s, const std::string& jobs)
    {
        const Trace t = Replay(s);
        const Ideal& ideal = MakeIdeal(s.setup);
        const std::vector<Passage> passages = SortedPassages(t, FindCorners(s.setup), ideal);
        const WhatIf whatif(s, t);
        json out = json::array();
        std::stringstream ss(jobs);
        for (std::string job; std::getline(ss, job, ',');)
        {
            if (!job.empty() && job[0] == 'P')
            {
                const size_t a = job.find('_'), b = job.find('_', a + 1);
                if (a == std::string::npos || b == std::string::npos)
                    continue;
                const int lap = std::stoi(job.substr(1, a - 1));
                const size_t active = std::stoul(job.substr(a + 1, b - a - 1));
                std::vector<std::pair<int, float>> fixes;
                std::stringstream fs(job.substr(b + 1));
                for (std::string f; std::getline(fs, f, 'y');)
                    if (const size_t x = f.find('x'); x != std::string::npos)
                        fixes.push_back({ std::stoi(f.substr(0, x)), std::stof(f.substr(x + 1)) });
                WhatIf::Outcome o;
                const bool ran = whatif.TryPlan(PlanChanges(passages, lap, fixes), active, o);
                out.push_back({ { "plan", lap }, { "active", active }, { "valid", ran && o.valid }, { "time", o.time }, { "recoveries", o.recoveries } });
                continue;
            }
            const size_t colon = job.find(':');
            if (colon == std::string::npos)
                continue;
            const size_t pass = std::stoul(job.substr(0, colon)), shift = std::stoul(job.substr(colon + 1));
            if (pass >= passages.size() || shift >= kRuns)
                continue;
            WhatIf::Outcome o;
            std::vector<float> path;
            whatif.Try(passages[pass], kShifts[shift], o, &path);
            out.push_back({ { "pass", pass }, { "shift", shift }, { "valid", o.valid }, { "time", o.time }, { "speed", o.speed },
                            { "recoveries", o.recoveries }, { "path", path } });
        }
        return out.dump();
    }

    std::vector<Proof> ProveFixes(const Session& s, const Trace& t, const std::vector<Corner>& corners, int max_proofs)
    {
        const Ideal& ideal = MakeIdeal(s.setup);
        std::vector<Passage> passages = SortedPassages(t, corners, ideal);
        const WhatIf whatif(s, t);

        // Each corner is tried on its worst pass first; a pass that cannot be measured (the race
        // ended inside its window) falls through to the corner's next pass.
        std::map<int, std::vector<const Passage*>> by_corner;
        std::vector<int> corner_order;
        for (const Passage& p : passages)
        {
            if (by_corner[p.corner].empty())
                corner_order.push_back(p.corner);
            by_corner[p.corner].push_back(&p);
        }
        struct Run { WhatIf::Outcome o; std::vector<float> path; };
        // Every (pass, shift) re-simulation is independent. Threads in one process share Jolt's job
        // pool and stall each other (measured: slower than serial), so the jobs are split across
        // worker processes, each with its own physics pool (D-095).
        auto run_all = [&](const std::vector<const Passage*>& todo) {
            std::vector<std::vector<Run>> out(todo.size(), std::vector<Run>(kRuns));
            std::vector<std::pair<size_t, size_t>> jobs; // (index into todo, shift index)
            for (size_t k = 0; k < todo.size(); ++k)
                for (size_t i = 0; i < kRuns; ++i)
                    jobs.push_back({ k, i });
            const unsigned workers = WorkerCount();
            if (workers <= 1 || g_session_path.empty())
            {
                for (auto [k, i] : jobs)
                    whatif.Try(*todo[k], kShifts[i], out[k][i].o, &out[k][i].path);
                return out;
            }
            // Round-robin so each worker gets a mix of early (cheap) and late (expensive) passes.
            std::vector<std::string> specs(workers);
            for (size_t j = 0; j < jobs.size(); ++j)
            {
                const size_t pass_index = size_t(todo[jobs[j].first] - passages.data());
                specs[j % workers] += std::to_string(pass_index) + ":" + std::to_string(jobs[j].second) + ",";
            }
            std::map<size_t, size_t> todo_of; // pass index -> todo slot
            for (size_t k = 0; k < todo.size(); ++k)
                todo_of[size_t(todo[k] - passages.data())] = k;
            for (const json& r : RunWorkers(specs))
            {
                Run& run = out[todo_of.at(r.at("pass"))][r.at("shift")];
                run.o.valid = r.at("valid");
                run.o.time = r.at("time");
                run.o.speed = r.at("speed");
                run.o.recoveries = r.at("recoveries");
                run.path = r.at("path").get<std::vector<float>>();
            }
            return out;
        };

        std::map<int, Proof> found;
        std::map<int, size_t> attempt;
        std::vector<int> pending = corner_order;
        while (!pending.empty())
        {
            std::vector<const Passage*> todo;
            for (int c : pending)
                todo.push_back(by_corner[c][attempt[c]]);
            const auto results = run_all(todo);
            std::vector<int> retry;
            for (size_t k = 0; k < todo.size(); ++k)
            {
                const Passage& p = *todo[k];
                const WhatIf::Outcome& base = results[k][0].o;
                if (!base.valid)
                {
                    if (++attempt[p.corner] < by_corner[p.corner].size())
                        retry.push_back(p.corner);
                    continue;
                }
                Proof best;
                best.corner = p.corner;
                best.lap = p.lap + 1;
                best.kind = "brake_earlier";
                best.sims = int(kRuns);
                // Shifts in track order (-30 .. 0 .. +30); a failed run (crash, reset, never reached
                // the measuring point) is NaN. A human cannot hit a braking point to the metre, so a
                // fix counts as robust only when both neighbouring shifts are also safe (no crash, no
                // real loss). Robust fixes win over a bigger knife-edge gain (D-097).
                const std::vector<size_t> order = { 1, 2, 3, 4, 5, 0, 6, 7, 8, 9, 10 };
                std::vector<float> gain(order.size());
                for (size_t j = 0; j < order.size(); ++j)
                {
                    const WhatIf::Outcome& o = results[k][order[j]].o;
                    const bool failed = !o.valid || o.recoveries > base.recoveries;
                    gain[j] = order[j] == 0 ? 0.0f : failed ? NAN : base.time - o.time;
                    if (std::getenv("PROOFPLAY_DEBUG") && order[j])
                        std::printf("  corner %d lap %d action %d m %+.0f: valid %d rec %u/%u gain %+.3f\n", p.corner, p.lap + 1,
                            int(whatif.ActionPoint(p).action), kShifts[order[j]], int(o.valid), o.recoveries, base.recoveries, base.time - o.time);
                }
                auto safe = [&](size_t j) { return !std::isnan(gain[j]) && gain[j] > -0.03f; };
                auto robust = [&](size_t j) { return (j == 0 || safe(j - 1)) && (j + 1 == gain.size() || safe(j + 1)); };
                size_t pick = SIZE_MAX;
                for (size_t j = 0; j < gain.size(); ++j)
                {
                    if (order[j] == 0 || std::isnan(gain[j]) || gain[j] < 0.03f)
                        continue;
                    const bool better = pick == SIZE_MAX || (robust(j) && !robust(pick)) || (robust(j) == robust(pick) && gain[j] > gain[pick]);
                    if (better)
                        pick = j;
                }
                if (pick != SIZE_MAX)
                {
                    const size_t i = order[pick];
                    best.gain_s = gain[pick];
                    best.amount_m = std::fabs(kShifts[i]);
                    best.shift_m = kShifts[i];
                    best.kind = WhatIf::Kind(whatif.ActionPoint(p).action, kShifts[i]);
                    best.exit_speed_gain = results[k][i].o.speed - base.speed;
                    best.path_fix = results[k][i].path;
                    best.robust = robust(pick);
                    // The range of the same change that still gains time: "anywhere from 10 to 20 m".
                    size_t lo = pick, hi = pick;
                    while (lo > 0 && order[lo - 1] != 0 && !std::isnan(gain[lo - 1]) && gain[lo - 1] >= 0.03f)
                        --lo;
                    while (hi + 1 < gain.size() && order[hi + 1] != 0 && !std::isnan(gain[hi + 1]) && gain[hi + 1] >= 0.03f)
                        ++hi;
                    best.window_lo_m = std::min(std::fabs(kShifts[order[lo]]), std::fabs(kShifts[order[hi]]));
                    best.window_hi_m = std::max(std::fabs(kShifts[order[lo]]), std::fabs(kShifts[order[hi]]));
                }
                if (best.gain_s >= 0.03f)
                {
                    best.path_real = results[k][0].path;
                    found[p.corner] = std::move(best);
                }
            }
            pending = std::move(retry);
        }
        // Same order as before: corners by their worst pass's loss.
        std::vector<Proof> proofs;
        for (int c : corner_order)
            if (auto it = found.find(c); it != found.end() && int(proofs.size()) < max_proofs)
                proofs.push_back(std::move(it->second));
        return proofs;
    }

    std::string EvaluateAdvice(const Session& s, const std::string& advice_json)
    {
        const Trace t = Replay(s);
        const Ideal& ideal = MakeIdeal(s.setup);
        const std::vector<Passage> passages = FindPassages(t, FindCorners(s.setup), ideal);
        const WhatIf whatif(s, t);
        json out = json::array();
        for (const json& a : json::parse(advice_json))
        {
            json row = a;
            const int corner = a.at("corner"), lap = a.at("lap");
            const std::string kind = a.at("kind");
            const float metres = a.at("amount_m");
            const auto p = std::find_if(passages.begin(), passages.end(), [&](const Passage& q) { return q.corner == corner && q.lap + 1 == lap; });
            WhatIf::Outcome base, o;
            float shift = 0;
            bool testable = p != passages.end();
            if (testable && !whatif.Shift(*p, kind, metres, shift))
            {
                // Advice that does not match how the player took the corner (e.g. "brake later"
                // where they never braked): test it as the nearest change of that corner's kind.
                const float sign = kind.find("later") != std::string::npos ? -1.0f : 1.0f;
                testable = whatif.Shift(*p, WhatIf::Kind(whatif.ActionPoint(*p).action, sign), metres, shift);
            }
            if (!testable || !whatif.Try(*p, 0.0f, base, nullptr) || !base.valid)
                row["status"] = "not_testable";
            else
            {
                whatif.Try(*p, shift, o, nullptr);
                const bool crashed = !o.valid || o.recoveries > base.recoveries;
                const float gain = crashed ? 0.0f : base.time - o.time;
                row["gain_s"] = gain;
                row["status"] = crashed ? "crash" : gain >= 0.03f ? "works" : gain > -0.03f ? "no_effect" : "worse";
            }
            out.push_back(row);
        }
        return out.dump(1);
    }

    std::string AnalyzeToJson(const Session& s, bool with_proofs)
    {
        const Trace t = Replay(s);
        const Ideal& ideal = MakeIdeal(s.setup);
        const int pl = s.setup.player_index;
        const std::vector<Corner> corners = FindCorners(s.setup);
        const std::vector<Passage> passages = FindPassages(t, corners, ideal);

        // Raw skill measures.
        float pace_sum = 0;
        int pace_n = 0;
        for (const Sample& sm : t.samples)
            if (sm.speed > 3.0f)
            {
                pace_sum += std::min(1.1f, sm.speed / std::max(1.0f, ideal.profile.At(sm.s)));
                ++pace_n;
            }
        const float pace = pace_n ? pace_sum / pace_n : 0;
        float line = 0;
        for (const Passage& p : passages)
            line += p.mean_abs_lateral;
        line = passages.empty() ? 0 : line / passages.size();
        int uses = 0, hits = 0, blocked = 0, damaged = 0;
        float damage_taken = 0;
        for (const CombatEvent& e : t.events)
        {
            if (e.type == CombatEventType::Use && e.source == pl)
                ++uses;
            if (e.type == CombatEventType::Damage && e.source == pl && e.target != pl)
                ++hits;
            if (e.type == CombatEventType::Blocked && e.target == pl)
                ++blocked;
            if (e.type == CombatEventType::Damage && e.target == pl)
            {
                ++damaged;
                damage_taken += e.amount;
            }
        }
        // Consistency: how much the loss in the same corner varies between laps.
        std::map<int, std::vector<float>> per_corner;
        for (const Passage& p : passages)
            per_corner[p.corner].push_back(p.loss_s);
        float spread = 0;
        int spread_n = 0;
        for (auto& [c, v] : per_corner)
            if (v.size() > 1)
            {
                const auto [mn, mx] = std::minmax_element(v.begin(), v.end());
                spread += *mx - *mn;
                ++spread_n;
            }
        spread = spread_n ? spread / spread_n : 0;

        json attrs = json::array();
        auto attr = [&](const char* key, const char* name, float raw, float rating) {
            attrs.push_back({ { "key", key }, { "name", name }, { "raw", raw }, { "rating", int(std::lround(rating)) } });
        };
        // Calibration points: proofplay/calibration.md (Easy/Normal/Hard bot runs).
        attr("pace", "Pace", pace, Rate(pace, 0.70f, 0.95f));
        attr("line", "Line", line, Rate(-line, -4.0f, -1.0f));
        attr("combat", "Combat", uses ? float(hits) / uses : 0.0f, Rate(uses ? float(hits) / uses : 0.0f, 0.15f, 0.6f));
        attr("defense", "Defence", (blocked + damaged) ? float(blocked) / (blocked + damaged) : 1.0f,
            Rate((blocked + damaged) ? float(blocked) / (blocked + damaged) : 1.0f, 0.0f, 0.5f));
        attr("consistency", "Consistency", spread, Rate(-spread, -1.0f, -0.1f));
        attr("recovery", "Recovery", float(t.recoveries), Rate(-float(t.recoveries), -3.0f, 0.0f));
        float overall = 0;
        for (auto& a : attrs)
            overall += a["rating"].get<int>();
        overall /= attrs.size();

        Race route_race(s.setup);
        const TrackRoute& route = route_race.Session().Route();
        json corner_json = json::array();
        for (const Corner& c : corners)
        {
            json laps = json::array();
            for (const Passage& p : passages)
                if (p.corner == c.id)
                {
                    // Features a coach can read: speeds through the pass and where braking began.
                    float min_speed = 1e9f;
                    size_t onset = p.last + 1;
                    for (size_t k = p.first; k <= p.last; ++k)
                    {
                        min_speed = std::min(min_speed, t.samples[k].speed);
                        if (onset > p.last && t.samples[k].input.drive.brake >= 0.2f)
                            onset = k;
                    }
                    json pass = { { "lap", p.lap + 1 }, { "time_s", p.time_s }, { "loss_s", p.loss_s },
                                  { "entry_speed", t.samples[p.first].speed }, { "min_speed", min_speed },
                                  { "exit_speed", t.samples[p.last].speed }, { "mean_abs_lateral", p.mean_abs_lateral } };
                    pass["brake_onset_before_apex_m"] = onset <= p.last ? json(Ahead(t.samples[onset].s, c.apex_s, ideal.length)) : json(nullptr);
                    laps.push_back(pass);
                }
            const Vec2 a = route.PointAt(c.apex_s);
            corner_json.push_back({ { "id", c.id }, { "apex_s", c.apex_s }, { "x", a.x }, { "z", a.z }, { "ideal_speed", c.ideal_speed }, { "passes", laps } });
        }

        // every corner that has a proven fix; the fast pass (dashboard stage 2a) skips them
        const std::vector<Proof> proofs = with_proofs ? ProveFixes(s, t, corners, int(corners.size())) : std::vector<Proof>{};
        json proof_json = json::array();
        for (const Proof& p : proofs)
            proof_json.push_back({ { "corner", p.corner }, { "lap", p.lap }, { "kind", p.kind }, { "amount_m", p.amount_m },
                                   { "gain_s", p.gain_s }, { "exit_speed_gain", p.exit_speed_gain }, { "sims", p.sims },
                                   { "robust", p.robust }, { "window_lo_m", p.window_lo_m }, { "window_hi_m", p.window_hi_m },
                                   { "path_real", p.path_real }, { "path_fix", p.path_fix } });

        // The next-race plan: every proven fix applied on ONE lap, re-simulated together. That is
        // what the player will actually do, and it keeps the open-loop stretch within a lap (a plan
        // spanning laps 1-3 ran into the spot where the original race needed a reset, D-097).
        // Racing laps are preferred; the lap-1 start is the noisiest. All candidate laps run at once
        // in worker processes (serially they cost up to 6 race-length sims).
        json plan = nullptr;
        if (proofs.size() >= 2)
        {
            const WhatIf whatif(s, t);
            std::vector<int> laps_order;
            for (int lap = 2; lap <= s.setup.laps; ++lap)
                laps_order.push_back(lap);
            laps_order.push_back(1);
            // Fixes on each candidate lap, biggest single gain first.
            std::map<int, std::vector<std::pair<int, float>>> fixes_of;
            std::vector<const Proof*> by_gain;
            for (const Proof& p : proofs)
                by_gain.push_back(&p);
            std::stable_sort(by_gain.begin(), by_gain.end(), [](const Proof* a, const Proof* b) { return a->gain_s > b->gain_s; });
            for (int lap : laps_order)
                for (const Proof* p : by_gain)
                {
                    const auto it = std::find_if(passages.begin(), passages.end(), [&](const Passage& q) { return q.corner == p->corner && q.lap + 1 == lap; });
                    float shift = 0;
                    if (it == passages.end())
                        continue;
                    if (!whatif.Shift(*it, p->kind, p->amount_m, shift)) // taken differently on this lap: same direction
                        whatif.Shift(*it, WhatIf::Kind(whatif.ActionPoint(*it).action, p->shift_m), p->amount_m, shift);
                    if (shift != 0)
                        fixes_of[lap].push_back({ p->corner, shift });
                }
            // Fixes interact, and "do everything" is not always best (measured: all 3 together on
            // Bot_Normal lost 0.69 s, all 14 on L01 crashed). So the top-2..5 subsets and "all"
            // are each re-simulated against one shared baseline per lap, all in parallel.
            auto sizes = [&](int lap) {
                std::vector<size_t> k = { 0 };
                const size_t n = fixes_of[lap].size();
                for (size_t i = 2; i <= std::min<size_t>(n, 5); ++i)
                    k.push_back(i);
                if (n > 5)
                    k.push_back(n);
                return n >= 2 ? k : std::vector<size_t>{};
            };
            std::map<std::pair<int, size_t>, WhatIf::Outcome> runs;
            std::vector<std::string> jobs;
            for (int lap : laps_order)
                for (size_t k : sizes(lap))
                {
                    std::string spec = "P" + std::to_string(lap) + "_" + std::to_string(k) + "_";
                    for (auto [c, m] : fixes_of[lap])
                        spec += std::to_string(c) + "x" + std::to_string(m) + "y";
                    jobs.push_back(spec);
                }
            if (WorkerCount() > 1 && !g_session_path.empty())
            {
                std::vector<std::string> specs(std::min<size_t>(WorkerCount(), jobs.size()));
                for (size_t j = 0; j < jobs.size(); ++j)
                    specs[j % specs.size()] += jobs[j] + ",";
                for (const json& r : RunWorkers(specs))
                {
                    WhatIf::Outcome& o = runs[{ r.at("plan").get<int>(), r.at("active").get<size_t>() }];
                    o.valid = r.at("valid");
                    o.time = r.at("time");
                    o.recoveries = r.at("recoveries");
                }
            }
            else
                for (int lap : laps_order)
                    for (size_t k : sizes(lap))
                        whatif.TryPlan(PlanChanges(passages, lap, fixes_of[lap]), k, runs[{ lap, k }]);
            for (int lap : laps_order)
            {
                const auto base = runs.find({ lap, 0 });
                if (base == runs.end() || !base->second.valid)
                    continue; // the baseline itself did not get through this lap: use another one
                const size_t n = fixes_of[lap].size();
                size_t best_k = 0;
                float best = 0.03f;
                bool all_ok = false;
                float all_gain = 0;
                for (size_t k : sizes(lap))
                {
                    const auto r = runs.find({ lap, k });
                    if (!k || r == runs.end())
                        continue;
                    const bool ok = r->second.valid && r->second.recoveries <= base->second.recoveries;
                    const float gain = base->second.time - r->second.time;
                    if (std::getenv("PROOFPLAY_DEBUG"))
                        std::printf("plan lap %d: top %zu of %zu: valid %d rec %u/%u gain %+.2f\n", lap, k, n, int(r->second.valid),
                            r->second.recoveries, base->second.recoveries, gain);
                    if (k == n)
                    {
                        all_ok = ok;
                        all_gain = ok ? gain : 0.0f;
                    }
                    if (ok && gain > best)
                    {
                        best = gain;
                        best_k = k;
                    }
                }
                json corners = json::array();
                for (size_t i = 0; i < best_k; ++i)
                    corners.push_back(fixes_of[lap][i].first);
                plan = { { "lap", lap }, { "fixes", best_k }, { "corners", corners }, { "valid", best_k > 0 }, { "gain_s", best_k ? best : 0.0f },
                         { "all_fixes", n }, { "all_valid", all_ok }, { "all_gain_s", all_gain }, { "sims", jobs.size() } };
                break;
            }
        }

        // Self-reference: each lap against the player's own best time over the same 20 m of track.
        // A human is often faster than the AI's ideal profile in corners yet loses whole seconds
        // elsewhere (a hit, a spin, the start); this finds where, and tags what happened there.
        json losses = json::array();
        {
            const float L = ideal.length, bin = 20.0f;
            const int nbins = int(L / bin) + 1, laps = std::max(1, s.setup.laps);
            std::vector<std::vector<float>> tbin(size_t(laps), std::vector<float>(size_t(nbins), 0.0f));
            std::vector<std::vector<std::pair<size_t, size_t>>> span(size_t(laps), std::vector<std::pair<size_t, size_t>>(size_t(nbins), { SIZE_MAX, 0 }));
            for (size_t i = 0; i < t.samples.size(); ++i)
            {
                const Sample& sm = t.samples[i];
                if (sm.d < 0)
                    continue;
                const int lap = int(sm.d / L), b = int((sm.d - lap * L) / bin);
                if (lap >= laps || b >= nbins)
                    continue;
                tbin[size_t(lap)][size_t(b)] += kDt;
                auto& sp = span[size_t(lap)][size_t(b)];
                sp.first = std::min(sp.first, i);
                sp.second = std::max(sp.second, i);
            }
            std::vector<float> best(size_t(nbins), 1e9f);
            for (int l = 0; l < laps; ++l)
                for (int b = 0; b < nbins; ++b)
                    if (tbin[size_t(l)][size_t(b)] > 0)
                        best[size_t(b)] = std::min(best[size_t(b)], tbin[size_t(l)][size_t(b)]);
            struct Seg { int lap; int b0, b1; float loss; };
            std::vector<Seg> segs;
            for (int l = 0; l < laps; ++l)
            {
                Seg cur{ l, -1, -1, 0.0f };
                for (int b = 0; b <= nbins; ++b)
                {
                    const float loss = b < nbins && tbin[size_t(l)][size_t(b)] > 0 ? tbin[size_t(l)][size_t(b)] - best[size_t(b)] : 0.0f;
                    if (loss > 0.15f)
                    {
                        if (cur.b0 < 0)
                            cur.b0 = b;
                        cur.b1 = b;
                        cur.loss += loss;
                    }
                    else if (cur.b0 >= 0)
                    {
                        segs.push_back(cur);
                        cur = Seg{ l, -1, -1, 0.0f };
                    }
                }
            }
            std::sort(segs.begin(), segs.end(), [](const Seg& a, const Seg& b) { return a.loss > b.loss; });
            for (const Seg& g : segs)
            {
                if (losses.size() >= 6 || g.loss < 0.5f)
                    break;
                const size_t i0 = span[size_t(g.lap)][size_t(g.b0)].first, i1 = span[size_t(g.lap)][size_t(g.b1)].second;
                if (i0 == SIZE_MAX || i1 < i0)
                    continue;
                // What happened in that stretch: hits taken, wrecks, crawling, leaving the road.
                json causes = json::array();
                std::map<std::string, int> hit_by;
                bool wrecked = false;
                for (const CombatEvent& e : t.events)
                    if (e.target == pl && e.tick >= t.samples[i0].tick && e.tick <= t.samples[i1].tick)
                    {
                        if (e.type == CombatEventType::Damage && e.source >= 0 && e.source != pl)
                            ++hit_by[ToString(e.ability)];
                        if (e.type == CombatEventType::Wreck)
                            wrecked = true;
                    }
                for (const auto& [ability, n] : hit_by)
                    causes.push_back({ { "kind", "hit" }, { "ability", ability }, { "count", n } });
                if (wrecked)
                    causes.push_back({ { "kind", "wreck" } });
                float min_speed = 1e9f, max_lat = 0;
                for (size_t i = i0; i <= i1; ++i)
                {
                    min_speed = std::min(min_speed, t.samples[i].speed);
                    max_lat = std::max(max_lat, std::fabs(t.samples[i].lateral));
                }
                if (min_speed < 5.0f)
                    causes.push_back({ { "kind", "stopped" }, { "min_speed_kmh", min_speed * 3.6f } });
                if (max_lat > route.HalfWidth())
                    causes.push_back({ { "kind", "off_road" }, { "metres", max_lat - route.HalfWidth() } });
                if (g.lap == 0 && g.b0 * bin < 80.0f)
                    causes.push_back({ { "kind", "start" } });
                json seg_path = json::array();
                for (size_t i = i0; i <= i1; i += 6)
                {
                    seg_path.push_back(t.samples[i].x);
                    seg_path.push_back(t.samples[i].z);
                }
                const Vec2 mid = route.PointAt((g.b0 + g.b1 + 1) * bin * 0.5f);
                losses.push_back({ { "lap", g.lap + 1 }, { "from_m", g.b0 * bin }, { "to_m", (g.b1 + 1) * bin }, { "loss_s", g.loss },
                                   { "x", mid.x }, { "z", mid.z }, { "causes", causes }, { "path", seg_path } });
            }
        }

        json track = json::array();
        for (float d = 0; d < route.Length(); d += 5.0f)
        {
            const Vec2 p = route.PointAt(d);
            track.push_back({ p.x, p.z });
        }
        json path = json::array();
        for (size_t i = 0; i < t.samples.size(); i += 12)
            path.push_back({ t.samples[i].x, t.samples[i].z, t.samples[i].speed });

        json r;
        r["format"] = "proofplay-report/1";
        r["setup"] = { { "player", s.setup.player }, { "track", s.setup.track }, { "laps", s.setup.laps }, { "cars", s.setup.cars }, { "field", s.setup.difficulty } };
        r["result"] = { { "position", t.end.player_position }, { "finished", t.end.player_finished }, { "time_s", t.end.player_time_s },
                        { "lap_times", t.lap_times }, { "verified", Mismatch(s.end, t.end).empty() }, { "mismatch", Mismatch(s.end, t.end) } };
        r["combat"] = { { "uses", uses }, { "hits", hits }, { "blocked", blocked }, { "damaged", damaged }, { "damage_taken", damage_taken } };
        // Behaviour (anti-cheat signal): human hands cannot reverse a steering correction many times
        // a second, nor change the input on every 120 Hz step; a scripted driver does both.
        {
            int reversals = 0, changes = 0;
            float last_delta = 0;
            for (size_t i = 1; i < s.inputs.size(); ++i)
            {
                const float dlt = s.inputs[i].drive.steer - s.inputs[i - 1].drive.steer;
                if (dlt != 0.0f || s.inputs[i].drive.throttle != s.inputs[i - 1].drive.throttle || s.inputs[i].drive.brake != s.inputs[i - 1].drive.brake)
                    ++changes;
                if (std::fabs(dlt) > 0.01f)
                {
                    if (last_delta != 0.0f && (dlt > 0) != (last_delta > 0))
                        ++reversals;
                    last_delta = dlt;
                }
            }
            const float secs = std::max(1.0f, s.inputs.size() * kDt);
            r["behaviour"] = { { "steer_reversals_per_s", reversals / secs }, { "input_change_rate", s.inputs.size() > 1 ? float(changes) / (s.inputs.size() - 1) : 0.0f } };
        }
        r["overall"] = int(std::lround(overall));
        r["attributes"] = attrs;
        r["corners"] = corner_json;
        r["proofs"] = proof_json;
        r["plan"] = plan;
        r["proofs_done"] = with_proofs;
        r["losses"] = losses;
        r["track"] = track;
        r["track_half_width"] = route.HalfWidth();
        r["path"] = path;
        return r.dump();
    }
}
