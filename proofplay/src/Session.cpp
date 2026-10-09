#include "Session.h"

#include "racer/CombatTuning.h"
#include "racer/Customization.h"
#include "racer/Event.h"
#include "racer/Performance.h"
#include "racer/Track.h"
#include "racer/Weather.h"
#include "racer/runtime/PhysicsStepper.h"
#include "racer/runtime/TrackBuilder.h"

#include "wiGraphicsDevice_DX12.h"
#include "wiJobSystem.h"
#include "wiScene.h"

#define NOMINMAX
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

using namespace racer;
using namespace racer::runtime;

namespace proofplay
{
    struct RaceScene
    {
        wi::scene::Scene scene;
    };

    namespace
    {
        std::string ReadFile(const std::string& path)
        {
            std::ifstream f(path, std::ios::binary);
            std::stringstream s;
            s << f.rdbuf();
            return s.str();
        }

        // content/tracks/<id>_<stem>.json, found the way RacePath finds it, so any new track works.
        TrackDefinition LoadTrack(const std::string& id)
        {
            std::string file;
            std::error_code ec;
            for (const auto& e : std::filesystem::directory_iterator(ContentDir() + "/tracks", ec))
                if (e.path().extension() == ".json" && e.path().filename().string().rfind(id + "_", 0) == 0)
                    file = e.path().string();
            if (file.empty())
                throw std::runtime_error("no track definition for '" + id + "'");
            TrackDefinition t;
            std::vector<ValidationIssue> issues;
            if (!ParseTrackDefinition(ReadFile(file), t, issues))
                throw std::runtime_error("track " + id + " invalid");
            return t;
        }

        VehicleDefinition LoadCar(const std::string& id)
        {
            VehicleDefinition d;
            std::vector<ValidationIssue> issues;
            if (!ParseVehicleDefinition(ReadFile(ContentDir() + "/vehicles/" + id + ".json"), d, issues))
                throw std::runtime_error("vehicle " + id + " invalid");
            return d;
        }

        AIDifficulty ParseDifficulty(const std::string& s)
        {
            return s == "Easy" ? AIDifficulty::Easy : s == "Hard" ? AIDifficulty::Hard : AIDifficulty::Normal;
        }
    }

    // PROOFPLAY_CONTENT, else a content/ folder next to or above the exe (the packaged layout is
    // bin/proofplay.exe + content/), else the source tree it was built from.
    std::string ContentDir()
    {
        static const std::string dir = [] {
            if (const char* e = std::getenv("PROOFPLAY_CONTENT"); e && *e)
                return std::string(e);
            wchar_t buf[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, buf, MAX_PATH);
            const std::filesystem::path exe = std::filesystem::path(buf).parent_path();
            std::error_code ec;
            for (const auto& c : { exe / "content", exe.parent_path() / "content" })
                if (std::filesystem::exists(c / "tracks", ec))
                    return c.string();
            return std::string(RACER_CONTENT_DIR);
        }();
        return dir;
    }

    void InitializeEngine()
    {
        static bool done = false;
        if (done)
            return;
        done = true;
        wi::jobsystem::Initialize();
        // Headless: physics never touches the GPU, so no DX12 device (it cost most of each
        // process's memory). PROOFPLAY_GPU=1 brings it back if a code path ever needs one.
        if (std::getenv("PROOFPLAY_GPU"))
        {
            static std::unique_ptr<wi::graphics::GraphicsDevice_DX12> device = std::make_unique<wi::graphics::GraphicsDevice_DX12>();
            wi::graphics::GetDevice() = device.get();
        }
        InitializePhysicsRuntime();
    }

    // Mirrors RacePath::Start (game/sandbox/RacePath.cpp): the same roster, garage parts, event
    // rules, weather and mounted weapon, in the same order, so the physics sees the same race.
    Race::Race(const SessionSetup& s)
        : scene(std::make_unique<RaceScene>())
        , player(s.player_index)
    {
        const TrackDefinition track = LoadTrack(s.track);
        BuildTrackPhysics(scene->scene, track);

        static const char* kRoster[] = { "D01", "D02", "D03", "D04", "D05" };
        std::vector<VehicleDefinition> cars;
        for (int i = 0; i < s.cars; ++i)
        {
            const std::string id = size_t(i) < s.grid.size() ? s.grid[size_t(i)] : kRoster[i % 5];
            VehicleDefinition d = LoadCar(id);
            if (i == s.player_index)
            {
                CarSetup perf;
                perf.parts = s.player_parts;
                d = ApplyPerformance(d, perf); // garage performance upgrades (D-071)
            }
            cars.push_back(d);
        }
        if (EventMode(s.mode) == EventMode::RivalDuel)
            for (auto& c : cars)
                c = cars[size_t(s.player_index)]; // mirror match (D-044)

        RaceSetup setup = MakeTrackRaceSetup(track, cars, s.laps);
        setup.difficulty = ParseDifficulty(s.difficulty);
        if (s.restrict_abilities)
        {
            if (s.allowed_abilities.empty())
                setup.pickups.clear();
            else
            {
                std::vector<Ability> authored, allowed;
                for (auto& p : setup.pickups)
                    authored.push_back(p.ability);
                for (int a : s.allowed_abilities)
                    allowed.push_back(Ability(a));
                const auto filtered = FilterPickupAbilities(authored, allowed);
                for (size_t i = 0; i < setup.pickups.size(); ++i)
                    setup.pickups[i].ability = filtered[i];
            }
        }
        setup.player_index = s.player_index;
        setup.mode = EventMode(s.mode);
        setup.weather = WeatherState{ WeatherKind(s.weather_kind), s.weather_intensity };
        std::vector<ValidationIssue> issues;
        if (!ParseCombatTuning(ReadFile(ContentDir() + "/combat/tuning.json"), setup.tuning, issues))
            throw std::runtime_error("combat tuning invalid");
        setup.time_limit_s = s.time_limit_s;

        race = std::make_unique<RaceSession>(scene->scene, setup);
        if (!race->Ready())
            throw std::runtime_error("race session not ready");
        if (const auto w = s.player_parts.find("weapon"); w != s.player_parts.end())
            race->SetMountedWeapon(size_t(s.player_index), MountedWeaponFromString(w->second)); // D-064
        race->Start();
    }

    Race::~Race()
    {
        race.reset(); // bodies leave the scene before it goes
    }

    bool Race::Running() const { return RaceRunning(*race); }

    void Race::Step(const StepInput& in)
    {
        if (in.reset)
            race->ResetCar(size_t(player));
        race->Step(in.drive, in.combat, PhysicsStepper::kStepSeconds);
    }

    EndState Race::End() const { return ComputeEnd(*race, player); }
}
