#pragma once
#include "racer/AI.h"
#include "racer/Combat.h"

namespace racer
{
    // Plan 2.10 personalities.
    enum class AIPersonality { LineMaster, Aggressor, Defender, PickupHunter };

    struct AICombatParams
    {
        float think_interval_s = 0.05f; // 20 Hz planner
        float reaction_s = 0.4f;        // decisions are acted on after this delay
        float mend_below = 0.5f;        // fraction of max health
        float aggression = 0.6f;        // 0..1: how readily it spends offensive items
        AIPersonality personality = AIPersonality::LineMaster;
    };
    AICombatParams MakeAICombatParams(AIDifficulty difficulty, AIPersonality personality, uint32_t seed);

    // Decides item use from what a driver could observe: cars, projectiles, traps and its own
    // inventory/health. It never reads other racers' inventories.
    class AICombatPlanner
    {
    public:
        AICombatPlanner(int self, const TrackRoute& route, AICombatParams params);

        CombatCommand Think(const CombatSystem& combat, const std::vector<CombatCar>& cars, float dt);

    private:
        struct Decision { bool valid = false; int slot = 0; UseDirection dir = UseDirection::Forward; float delay = 0; };
        Decision Choose(const CombatSystem& combat, const std::vector<CombatCar>& cars) const;

        int self_;
        const TrackRoute& route_;
        AICombatParams params_;
        float think_timer_ = 0.0f;
        Decision pending_;
    };
}
