#include "racer/AICombat.h"

#include <algorithm>
#include <cmath>

namespace racer
{
    namespace
    {
        Vec2 Sub(Vec2 a, Vec2 b) { return { a.x - b.x, a.z - b.z }; }
        float Len(Vec2 a) { return std::sqrt(a.x * a.x + a.z * a.z); }
        float Dot(Vec2 a, Vec2 b) { return a.x * b.x + a.z * b.z; }
        Vec2 Forward(float h) { return { std::sin(h), std::cos(h) }; }
    }

    AICombatParams MakeAICombatParams(AIDifficulty difficulty, AIPersonality personality, uint32_t seed)
    {
        AICombatParams p;
        p.reaction_s = MakeAIParams(difficulty, seed).reaction_s;
        p.personality = personality;
        switch (personality)
        {
        case AIPersonality::LineMaster: p.aggression = 0.4f; p.mend_below = 0.4f; break;
        case AIPersonality::Aggressor: p.aggression = 0.9f; p.mend_below = 0.3f; break;
        case AIPersonality::Defender: p.aggression = 0.3f; p.mend_below = 0.6f; break;
        case AIPersonality::PickupHunter: p.aggression = 0.6f; p.mend_below = 0.5f; break;
        }
        return p;
    }

    AICombatPlanner::AICombatPlanner(int self, const TrackRoute& route, AICombatParams params)
        : self_(self), route_(route), params_(params) {}

    AICombatPlanner::Decision AICombatPlanner::Choose(const CombatSystem& combat, const std::vector<CombatCar>& cars) const
    {
        const Combatant& me = combat.Get(self_);
        const CombatCar& car = cars[size_t(self_)];
        const Vec2 fwd = Forward(car.heading_rad);
        const CombatTuning& t = combat.Tuning();
        auto slot_of = [&](Ability a) {
            for (int i = 0; i < 3; ++i)
                if (me.slots[size_t(i)].ability == a)
                    return i;
            return -1;
        };
        auto decide = [&](Ability a, UseDirection d) {
            Decision x;
            x.slot = slot_of(a);
            x.valid = x.slot >= 0;
            x.dir = d;
            x.delay = params_.reaction_s;
            return x;
        };

        // Threat awareness: an enemy Lance homing on me, or any projectile about to reach me.
        bool incoming = false;
        for (const Projectile& p : combat.Projectiles())
        {
            if (p.owner == self_)
                continue;
            const Vec2 rel = Sub(car.position, p.position);
            const float dist = Len(rel);
            const float closing = Dot(rel, p.velocity) / std::max(1e-3f, dist);
            if ((p.target == self_ && dist < 60.0f) || (closing > 0.0f && dist < 25.0f))
                incoming = true;
        }

        // 1. Survival first.
        if (incoming)
        {
            if (Decision d = decide(Ability::Ward, UseDirection::Forward); d.valid)
                return d;
            if (Decision d = decide(Ability::Pulse, UseDirection::Forward); d.valid)
                return d;
        }
        if (me.health < me.max_health * params_.mend_below)
            if (Decision d = decide(Ability::Mend, UseDirection::Forward); d.valid)
                return d;

        // Observed neighbours.
        int ahead_target = -1, behind_close = -1, near_count = 0;
        float best_ahead = 1e9f;
        for (size_t j = 0; j < cars.size(); ++j)
        {
            if (int(j) == self_ || cars[j].finished)
                continue;
            const Vec2 rel = Sub(cars[j].position, car.position);
            const float dist = Len(rel);
            if (dist <= t.pulse_radius)
                ++near_count;
            const float along = Dot(rel, fwd);
            const float cosang = dist > 1e-3f ? along / dist : 0.0f;
            if (along > 5.0f && cosang > std::cos(t.lance_target_cone_rad) && dist < t.lance_target_range * 0.8f &&
                route_.Ahead(car.route_s, cars[j].route_s) < t.lance_route_window && dist < best_ahead)
            {
                best_ahead = dist;
                ahead_target = int(j);
            }
            if (along < -3.0f && along > -20.0f && std::fabs(Dot(rel, { fwd.z, -fwd.x })) < 4.0f)
                behind_close = int(j);
        }

        const bool can_attack = me.protect_time <= 0.0f;
        const bool trigger_happy = params_.aggression >= 0.5f;

        if (can_attack && near_count >= (trigger_happy ? 1 : 2))
            if (Decision d = decide(Ability::Pulse, UseDirection::Forward); d.valid)
                return d;
        if (can_attack && ahead_target >= 0)
        {
            if (Decision d = decide(Ability::Lance, UseDirection::Forward); d.valid)
                return d;
            if (best_ahead < 45.0f)
                if (Decision d = decide(Ability::Needle, UseDirection::Forward); d.valid)
                    return d;
        }
        if (can_attack && behind_close >= 0)
        {
            if (Decision d = decide(Ability::Trap, UseDirection::Backward); d.valid)
                return d;
            if (trigger_happy)
                if (Decision d = decide(Ability::Lance, UseDirection::Backward); d.valid)
                    return d;
        }

        // Storm only makes sense when someone else leads.
        if (can_attack)
        {
            bool i_lead = true;
            for (size_t j = 0; j < cars.size(); ++j)
                if (int(j) != self_ && !cars[j].finished && cars[j].race_progress > car.race_progress)
                    i_lead = false;
            if (!i_lead)
                if (Decision d = decide(Ability::Storm, UseDirection::Forward); d.valid)
                    return d;
        }

        // Surge where the road ahead is straight enough to use it.
        // The whole distance the boost covers (reaction delay + boost time at a boosted speed, at
        // least 60 m) must be straight, sampled every 10 m: comparing only "now" with "60 m ahead"
        // took an S-bend, whose exit points the same way as its entry, for a straight (D-043).
        const Vec2 d0 = route_.DirectionAt(car.route_s);
        const float speed = std::max(Len(car.velocity), 20.0f);
        const float reach = std::max(60.0f, (speed + 8.0f) * (params_.reaction_s + t.surge_time_s));
        bool straight = true;
        for (float ahead = 10.0f; ahead <= reach && straight; ahead += 10.0f)
            straight = Dot(d0, route_.DirectionAt(car.route_s + ahead)) > 0.97f;
        if (straight)
            if (Decision d = decide(Ability::Surge, UseDirection::Forward); d.valid)
                return d;

        return {};
    }

    CombatCommand AICombatPlanner::Think(const CombatSystem& combat, const std::vector<CombatCar>& cars, float dt)
    {
        CombatCommand cmd;
        if (pending_.valid)
        {
            pending_.delay -= dt;
            if (pending_.delay <= 0.0f)
            {
                // Re-check that the item is still there (a slot could have been used or dropped).
                if (combat.Get(self_).slots[size_t(pending_.slot)].ability != Ability::None)
                {
                    cmd.select = pending_.slot;
                    cmd.use = true;
                    cmd.direction = pending_.dir;
                }
                pending_ = {};
            }
            return cmd;
        }
        think_timer_ -= dt;
        if (think_timer_ > 0.0f)
            return cmd;
        think_timer_ = params_.think_interval_s;
        pending_ = Choose(combat, cars);
        return cmd;
    }
}
