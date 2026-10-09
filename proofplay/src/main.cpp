// proofplay: headless record / replay / analysis of RIVAL LINE races.
//   proofplay record --out s.json [--track S01] [--laps 2] [--cars 4] [--field Normal]
//                    [--bot Normal] [--flaw late_brake|early_brake:<route_s>:<metres>]
//   proofplay replay s.json        re-runs the inputs and checks the end state matches
//   proofplay analyze s.json --out report.json [--fast]   skill attributes, losses, proven fixes (--fast: no fixes)
//   proofplay whatif s.json --jobs pass:shift,... --out r.json   worker for the parallel what-if search
#include "Analyze.h"
#include "Session.h"

#include "racer/AI.h"
#include "racer/AICombat.h"
#include "racer/runtime/PhysicsStepper.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#define NOMINMAX
#include <windows.h>
#include <psapi.h>

using namespace racer;
using namespace racer::runtime;
using namespace proofplay;

namespace
{
    using Clock = std::chrono::steady_clock;

    std::map<std::string, std::string> Flags(int argc, char** argv, int first)
    {
        std::map<std::string, std::string> f;
        for (int i = first; i < argc; ++i)
        {
            if (std::strcmp(argv[i], "--fast") == 0)
                f["fast"] = "1"; // switch, takes no value
            else if (std::strncmp(argv[i], "--", 2) == 0 && i + 1 < argc)
            {
                f[argv[i] + 2] = argv[i + 1];
                ++i;
            }
            else
                f["_"] = argv[i];
        }
        return f;
    }

    std::string Get(const std::map<std::string, std::string>& f, const char* k, const char* def)
    {
        auto it = f.find(k);
        return it == f.end() ? def : it->second;
    }

    std::string ReadAll(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    AIDifficulty Skill(const std::string& s)
    {
        return s == "Easy" ? AIDifficulty::Easy : s == "Hard" ? AIDifficulty::Hard : AIDifficulty::Normal;
    }

    // A planted, known weakness so tests can check ProofPlay finds it.
    struct Flaw
    {
        std::string kind;
        float s = -1;      // route position of the corner entry
        float metres = 0;  // late_brake: keeps full throttle this far into the braking zone
        bool Active(float route_s) const { return !kind.empty() && route_s >= s - metres && route_s < s; }
    };

    Flaw ParseFlaw(const std::string& text)
    {
        Flaw f;
        if (text.empty())
            return f;
        std::stringstream ss(text);
        std::string part;
        std::getline(ss, f.kind, ':');
        std::getline(ss, part, ':');
        f.s = std::stof(part);
        std::getline(ss, part, ':');
        f.metres = std::stof(part);
        return f;
    }

    int Record(const std::map<std::string, std::string>& f)
    {
        Session session;
        session.setup.track = Get(f, "track", "S01");
        session.setup.laps = std::stoi(Get(f, "laps", "2"));
        session.setup.cars = std::stoi(Get(f, "cars", "4"));
        session.setup.difficulty = Get(f, "field", "Normal");
        session.setup.build_id = BuildId();
        session.setup.player = Get(f, "player", "Bot");
        const std::string out = Get(f, "out", "session.json");
        const Flaw flaw = ParseFlaw(Get(f, "flaw", ""));
        const AIDifficulty skill = Skill(Get(f, "bot", "Normal"));

        Race race(session.setup);
        RaceSession& rs = race.Session();
        VehicleRuntime& car = rs.Car(size_t(race.Player()));
        AIDriver pilot(rs.Route(), car.Definition(), MakeAIParams(skill, 7));
        AICombatPlanner gunner(race.Player(), rs.Route(), MakeAICombatParams(skill, AIPersonality::LineMaster, 7));

        StatsTracker stats;
        const auto t0 = Clock::now();
        while (race.Running() && session.inputs.size() < 120u * 900u)
        {
            const VehicleTelemetry& t = car.Telemetry();
            const float route_s = rs.Rules().Participant(race.Player()).s;
            const AIObservation o{ { t.position[0], t.position[2] }, t.heading_rad, t.forward_speed_kmh / 3.6f, route_s };
            StepInput in;
            in.drive = pilot.Drive(o, PhysicsStepper::kStepSeconds);
            in.combat = gunner.Think(rs.Combat(), rs.CombatCars(), PhysicsStepper::kStepSeconds);
            if (flaw.Active(route_s) && flaw.kind == "late_brake")
            {
                in.drive.brake = 0.0f;
                in.drive.throttle = 1.0f;
            }
            if (flaw.Active(route_s) && flaw.kind == "early_brake")
            {
                in.drive.brake = 1.0f; // over-cautious: brakes hard before the braking point
                in.drive.throttle = 0.0f;
            }
            race.Step(in);
            session.inputs.push_back(in);
            stats.Step(rs, race.Player(), false);
        }
        const double secs = std::chrono::duration<double>(Clock::now() - t0).count();
        session.end = race.End();
        session.stats = stats.Finish(rs, race.Player());
        std::ofstream(out, std::ios::binary) << ToJson(session);
        const double sim = session.inputs.size() * double(PhysicsStepper::kStepSeconds);
        std::printf("recorded %s: %zu steps (%.1f s race), player P%d %s %.2f s, wall %.2f s (%.1fx realtime)\n",
            out.c_str(), session.inputs.size(), sim, session.end.player_position,
            session.end.player_finished ? "finished" : "DNF", session.end.player_time_s, secs, sim / secs);
        return 0;
    }

    int Replay(const std::map<std::string, std::string>& f)
    {
        const std::string path = Get(f, "_", "");
        std::ifstream in(path, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        Session session;
        std::string error;
        if (!FromJson(ss.str(), session, error))
        {
            std::printf("cannot read %s: %s\n", path.c_str(), error.c_str());
            return 2;
        }
        if (session.setup.build_id != BuildId())
            std::printf("warning: session from build '%s', this is '%s'\n", session.setup.build_id.c_str(), BuildId().c_str());
        const auto t0 = Clock::now();
        Race race(session.setup);
        for (const StepInput& i : session.inputs)
            race.Step(i);
        const double secs = std::chrono::duration<double>(Clock::now() - t0).count();
        const EndState e = race.End();
        const std::string diff = Mismatch(session.end, e);
        const bool ok = diff.empty();
        const double sim = session.inputs.size() * double(PhysicsStepper::kStepSeconds);
        std::printf("replay %s: %s (hash %llu vs %llu), P%d %.2f s, wall %.2f s (%.1fx realtime)\n", path.c_str(),
            ok ? "MATCH" : "MISMATCH", (unsigned long long)e.hash, (unsigned long long)session.end.hash,
            e.player_position, e.player_time_s, secs, sim / secs);
        if (!ok)
            std::printf("  differs: %s(claimed P%d %.2f s)\n", diff.c_str(), session.end.player_position, session.end.player_time_s);
        return ok ? 0 : 1;
    }

    int Analyze(const std::map<std::string, std::string>& f)
    {
        const std::string path = Get(f, "_", "");
        std::ifstream in(path, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        Session session;
        std::string error;
        if (!FromJson(ss.str(), session, error))
        {
            std::printf("cannot read %s: %s\n", path.c_str(), error.c_str());
            return 2;
        }
        const auto t0 = Clock::now();
        SetSessionPath(path);
        const std::string report = AnalyzeToJson(session, f.count("fast") == 0);
        const std::string out = Get(f, "out", "report.json");
        std::ofstream(out, std::ios::binary) << report;
        std::printf("analyzed %s -> %s in %.1f s\n", path.c_str(), out.c_str(), std::chrono::duration<double>(Clock::now() - t0).count());
        return 0;
    }
}

// Peak memory of this process, printed at exit (PROOFPLAY_MEM=1) to keep RAM use measured.
static void PrintPeakMemory()
{
    if (!std::getenv("PROOFPLAY_MEM"))
        return;
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        std::printf("memory: peak working set %.0f MB, peak private %.0f MB\n", pmc.PeakWorkingSetSize / 1048576.0, pmc.PeakPagefileUsage / 1048576.0);
}

int main(int argc, char** argv)
{
    std::atexit(PrintPeakMemory);
    if (argc < 2)
    {
        std::printf("usage: proofplay record|replay ...\n");
        return 2;
    }
    try
    {
        InitializeEngine();
        const auto f = Flags(argc, argv, 2);
        if (std::strcmp(argv[1], "record") == 0)
            return Record(f);
        if (std::strcmp(argv[1], "replay") == 0)
            return Replay(f);
        if (std::strcmp(argv[1], "analyze") == 0)
            return Analyze(f);
        if (std::strcmp(argv[1], "whatif") == 0)
        {
            Session session;
            std::string error;
            if (!FromJson(ReadAll(Get(f, "_", "")), session, error))
                return 2;
            std::ofstream(Get(f, "out", "whatif.json"), std::ios::binary) << RunWhatIfJobs(session, Get(f, "jobs", ""));
            return 0;
        }
        if (std::strcmp(argv[1], "evaluate") == 0)
        {
            Session session;
            std::string error;
            if (!FromJson(ReadAll(Get(f, "_", "")), session, error))
            {
                std::printf("cannot read session: %s\n", error.c_str());
                return 2;
            }
            std::ofstream(Get(f, "out", "evaluation.json"), std::ios::binary) << EvaluateAdvice(session, ReadAll(Get(f, "advice", "advice.json")));
            return 0;
        }
        std::printf("unknown command %s\n", argv[1]);
        return 2;
    }
    catch (const std::exception& e)
    {
        std::printf("error: %s\n", e.what());
        return 1;
    }
}
