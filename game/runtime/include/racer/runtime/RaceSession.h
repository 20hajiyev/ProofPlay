#pragma once
#include "racer/AI.h"
#include "racer/AICombat.h"
#include "racer/Combat.h"
#include "racer/Event.h"
#include "racer/Race.h"
#include "racer/Recovery.h"
#include "racer/Contact.h"
#include "racer/Damage.h"
#include "racer/Weather.h"
#include "racer/runtime/PhysicsStepper.h"
#include "racer/runtime/VehicleRuntime.h"

#include <memory>
#include <vector>

namespace racer::runtime
{
    struct GridSlot
    {
        XMFLOAT3 ground;
        float yaw_rad = 0.0f;
    };

    struct PickupSpec
    {
        Vec2 position;
        Ability ability;
    };

    struct RaceSetup
    {
        TrackRoute route;
        RaceConfig race;
        std::vector<VehicleDefinition> cars; // one per grid slot
        std::vector<GridSlot> grid;
        std::vector<PickupSpec> pickups;
        int player_index = -1;               // -1 = all AI (tests, attract mode)
        // Plan 2.12 modes. Destruction: the player hunts non-combatant targets for time_limit_s;
        // ranking is by hits dealt, the event ends on the clock. RivalDuel: one aggressive Hard rival.
        EventMode mode = EventMode::CombatRace;
        CombatTuning tuning;                 // content/combat/tuning.json (mode overrides applied on top)
        float time_limit_s = 0.0f;          // > 0: the event ends this long after the green light
        AIDifficulty difficulty = AIDifficulty::Normal;
        std::vector<AIDifficulty> car_difficulty; // optional per grid slot, overrides difficulty (mixed fields, test stand-ins)
        WeatherState weather;                // D-083: the same grip for every car; the AI drives to it
        bool disable_bumpers = false;        // D-088 A/B measurement only
    };

    // One race: rules, combat, AI and the cars' physics, advanced one authoritative step at a
    // time in the plan 2.4 order. Presentation reads it; it never reads presentation.
    class RaceSession
    {
    public:
        void SetMountedWeapon(size_t car, MountedWeapon w) { combat_->SetMountedWeapon(int(car), w); } // garage weapon (D-064)
        RaceSession(wi::scene::Scene& scene, RaceSetup setup);
        ~RaceSession();
        RaceSession(const RaceSession&) = delete;
        RaceSession& operator=(const RaceSession&) = delete;

        bool Ready() const { return ready_; }
        void Start(); // grid -> countdown
        void ResetCar(size_t i) { Recover(i, RecoveryReason::Stuck); } // player-requested reset
        void Step(const VehicleCommand& player_drive, const CombatCommand& player_combat, float dt);

        const RaceRules& Rules() const { return *rules_; }
        RaceRules& Rules() { return *rules_; }
        const CombatSystem& Combat() const { return *combat_; }
        const TrackRoute& Route() const { return setup_.route; }
        VehicleRuntime& Car(size_t i) { return *cars_[i]; }
        size_t CarCount() const { return cars_.size(); }
        const std::vector<CombatCar>& CombatCars() const { return combat_cars_; }
        uint64_t Tick() const { return tick_; }

        struct Stats
        {
            uint32_t uses = 0, hits = 0, blocks = 0, wrecks = 0, pickups = 0, recoveries = 0, insane_steps = 0;
            float max_car_overlap_m = 0.0f; // deepest visual car-into-car overlap seen (D-088)
            uint32_t bumper_contacts = 0, deep_overlap_steps = 0; // pair-steps sunk > 0.3 m
            uint32_t recover_fell = 0, recover_upside = 0, recover_stuck = 0;
            uint32_t uses_by_ability[size_t(Ability::Count)] = {};
        };
        const Stats& GetStats() const { return stats_; }

        // Per-car record for objectives and the results screen (plan 2.12).
        RaceSummary Summary(size_t i) const;
        float RaceTime(size_t i) const; // seconds from the green light (finish time if finished)
        void SetDebugLogRecoveries(bool on) { debug_log_recoveries_ = on; }

    private:
        std::vector<float> catch_up_accel_; // duel rival rubber band, m/s^2 per car (D-059)
        struct Impact { float speed[4] = {}, vx[4] = {}, vz[4] = {}; int head = 0, samples = 0; float cooldown = 0.0f, dent_cooldown = 0.0f; };
        std::vector<Impact> impact_; // crash damage detection per car (D-061)
        std::vector<DentState> dents_; // visible dents per car (D-085)
        void ApplyBumpers();
    public:
        const DentState& Dents(size_t car) const { static const DentState none; return car < dents_.size() ? dents_[car] : none; }
    private:
        void RefreshCombatCars();
        void ApplyCombatEffects(float dt);
        void Recover(size_t i, RecoveryReason reason);

        wi::scene::Scene& scene_;
        RaceSetup setup_;
        PhysicsStepper stepper_;
        std::unique_ptr<RaceRules> rules_;
        std::unique_ptr<CombatSystem> combat_;
        std::vector<Puddle> puddles_; // D-084: standing water on a wet road
    public:
        const std::vector<Puddle>& Puddles() const { return puddles_; }
    private:
        std::vector<std::unique_ptr<VehicleRuntime>> cars_;
        std::vector<std::unique_ptr<AIDriver>> drivers_;
        std::vector<std::unique_ptr<AICombatPlanner>> planners_;
        std::vector<RecoveryMonitor> recovery_;
        std::vector<CombatCar> combat_cars_;
        std::vector<CombatEvent> pending_knocks_;
        CommandBuffer player_commands_;
        uint64_t tick_ = 0;
        bool ready_ = false;
        bool debug_log_recoveries_ = false;
        uint64_t green_tick_ = 0; // first Racing tick
        bool time_up_ = false;
    public:
        bool TimeUp() const { return time_up_; }
        float TimeLeft() const;
        int WrecksCaused(size_t i) const { return car_stats_[i].wrecks_caused; }
    private:
        struct CarStats { int uses = 0, hits_dealt = 0, wrecks = 0, wrecks_caused = 0; float damage_taken = 0; };
        std::vector<CarStats> car_stats_;
        static constexpr float kRecoverBehind_m = 12.0f;
        Stats stats_;
    };
}
