#include "racer/Combat.h"

#include <algorithm>
#include <cmath>

namespace racer
{
    namespace
    {
        Vec2 Add(Vec2 a, Vec2 b) { return { a.x + b.x, a.z + b.z }; }
        Vec2 Sub(Vec2 a, Vec2 b) { return { a.x - b.x, a.z - b.z }; }
        Vec2 Mul(Vec2 a, float k) { return { a.x * k, a.z * k }; }
        float Dot(Vec2 a, Vec2 b) { return a.x * b.x + a.z * b.z; }
        float Len(Vec2 a) { return std::sqrt(Dot(a, a)); }
        Vec2 Norm(Vec2 a)
        {
            const float l = Len(a);
            return l > 1e-6f ? Mul(a, 1.0f / l) : Vec2{ 0, 1 };
        }
        Vec2 Forward(float heading) { return { std::sin(heading), std::cos(heading) }; }
        bool Offensive(Ability a) { return a == Ability::Lance || a == Ability::Pulse || a == Ability::Trap || a == Ability::Needle || a == Ability::Storm; }
    }

    const char* ToString(Ability a)
    {
        switch (a)
        {
        case Ability::None: return "None";
        case Ability::Surge: return "Surge";
        case Ability::Lance: return "Lance";
        case Ability::Pulse: return "Pulse";
        case Ability::Trap: return "Trap";
        case Ability::Needle: return "Needle";
        case Ability::Ward: return "Ward";
        case Ability::Mend: return "Mend";
        case Ability::Storm: return "Storm";
        default: return "?";
        }
    }

    CombatSystem::CombatSystem(const TrackRoute& route, CombatTuning tuning) : route_(route), tuning_(tuning) {}

    int CombatSystem::AddCombatant(float max_health)
    {
        Combatant c;
        c.health = c.max_health = max_health;
        combatants_.push_back(c);
        return int(combatants_.size()) - 1;
    }

    int CombatSystem::AddPickupPad(Vec2 position, Ability ability)
    {
        pads_.push_back({ position, ability, 0.0f });
        return int(pads_.size()) - 1;
    }

    void CombatSystem::GiveAbility(int id, Ability ability)
    {
        for (Slot& s : combatants_[size_t(id)].slots)
            if (s.ability == Ability::None)
            {
                s.ability = ability;
                s.charges = ability == Ability::Needle ? tuning_.needle_shots : 1;
                return;
            }
    }

    void CombatSystem::Respawn(int id)
    {
        Combatant& c = combatants_[size_t(id)];
        c.health = c.max_health;
        c.last_attacker = -1;
        c.wrecked = false;
        c.protect_time = tuning_.respawn_protect_s;
        c.surge_time = c.brake_time = c.heavy_chain_time = 0;
    }

    float CombatSystem::BoostAccel(int id) const
    {
        const Combatant& c = combatants_[size_t(id)];
        if (c.brake_time > 0)
            return -tuning_.surge_back_decel;
        if (c.surge_time > 0)
            return tuning_.surge_accel;
        return 0.0f;
    }

    bool CombatSystem::CanAttack(int id, const std::vector<CombatCar>& cars) const
    {
        const Combatant& c = combatants_[size_t(id)];
        return !c.wrecked && !cars[size_t(id)].finished && c.protect_time <= 0.0f;
    }

    bool CombatSystem::Targetable(int id, const std::vector<CombatCar>& cars) const
    {
        const Combatant& c = combatants_[size_t(id)];
        return !c.wrecked && !cars[size_t(id)].finished && c.protect_time <= 0.0f;
    }

    int CombatSystem::PickLanceTarget(int id, const std::vector<CombatCar>& cars) const
    {
        const CombatCar& me = cars[size_t(id)];
        const Vec2 fwd = Forward(me.heading_rad);
        int best = -1;
        float best_score = 1e30f;
        for (size_t j = 0; j < cars.size(); ++j)
        {
            if (int(j) == id || !Targetable(int(j), cars))
                continue;
            const Vec2 rel = Sub(cars[j].position, me.position);
            const float dist = Len(rel);
            if (dist < 1e-3f || dist > tuning_.lance_target_range)
                continue;
            const float angle = std::acos(std::clamp(Dot(Norm(rel), fwd), -1.0f, 1.0f));
            if (angle > tuning_.lance_target_cone_rad)
                continue;
            // Route consistency: the target must be ahead along the racing line, not on a
            // neighbouring section that happens to be in view.
            if (route_.Ahead(me.route_s, cars[j].route_s) > tuning_.lance_route_window)
                continue;
            const float score = dist * (1.0f + angle);
            if (score < best_score)
            {
                best_score = score;
                best = int(j);
            }
        }
        return best;
    }

    float ImpactDamage(float speed_loss_kmh)
    {
        constexpr float kFree = 12.0f, kPerKmh = 0.5f, kCap = 30.0f;
        return std::clamp((speed_loss_kmh - kFree) * kPerKmh, 0.0f, kCap);
    }

    void CombatSystem::Hit(int source, int target, Ability kind, float damage, Vec2 dir, float dv, float dv_up, bool heavy,
                           const std::vector<CombatCar>& cars, uint64_t tick)
    {
        if (!Targetable(target, cars))
            return;
        Combatant& t = combatants_[size_t(target)];
        if (t.ward_time > 0.0f && t.ward_hits > 0)
        {
            --t.ward_hits;
            Emit({ CombatEventType::Blocked, tick, source, target, kind });
            if (t.ward_hits == 0)
            {
                t.ward_time = 0.0f;
                Emit({ CombatEventType::WardDown, tick, source, target, Ability::Ward });
            }
            return;
        }
        t.health = std::max(0.0f, t.health - damage);
        if (source >= 0 && source != target)
            t.last_attacker = source;
        Emit({ CombatEventType::Damage, tick, source, target, kind, damage });

        float scale = tuning_.reference_mass_kg / std::max(300.0f, cars[size_t(target)].mass_kg);
        if (heavy)
        {
            if (t.heavy_chain_time > 0.0f)
                scale *= tuning_.heavy_chain_scale;
            t.heavy_chain_time = tuning_.heavy_chain_s;
        }
        CombatEvent k{ CombatEventType::Knock, tick, source, target, kind };
        k.impulse_dir = Norm(dir);
        k.delta_v = dv * scale;
        k.delta_v_up = dv_up * scale;
        Emit(k);
    }

    void CombatSystem::Use(int id, UseDirection dir, const std::vector<CombatCar>& cars, uint64_t tick)
    {
        Combatant& c = combatants_[size_t(id)];
        int slot_index = c.selected;
        if (c.slots[size_t(slot_index)].ability == Ability::None)
        {
            // Friendlier than firing nothing: fall back to the first filled slot.
            slot_index = -1;
            for (int i = 0; i < 3; ++i)
                if (c.slots[size_t(i)].ability != Ability::None)
                {
                    slot_index = i;
                    break;
                }
            if (slot_index < 0)
                return;
        }
        Slot& slot = c.slots[size_t(slot_index)];
        if (!Fire(id, slot.ability, dir, cars, tick))
            return;
        if (--slot.charges <= 0)
            slot = Slot{};
    }

    bool CombatSystem::Fire(int id, Ability a, UseDirection dir, const std::vector<CombatCar>& cars, uint64_t tick)
    {
        Combatant& c = combatants_[size_t(id)];
        if (Offensive(a) && !CanAttack(id, cars))
            return false;

        const CombatCar& me = cars[size_t(id)];
        const Vec2 fwd = Forward(me.heading_rad);
        const bool back = dir == UseDirection::Backward;
        const float T = tuning_.car_radius_m + 1.0f; // spawn clear of the car body

        switch (a)
        {
        case Ability::Surge:
            if (back)
            {
                c.brake_time = tuning_.surge_back_brake_s;
                c.surge_time = tuning_.surge_back_brake_s + tuning_.surge_back_boost_s;
            }
            else
            {
                c.surge_time = tuning_.surge_time_s;
            }
            break;
        case Ability::Lance:
        {
            Projectile p{ next_id_++, Ability::Lance, id };
            p.position = Add(me.position, Mul(fwd, back ? -T : T));
            p.velocity = back ? Add(me.velocity, Mul(fwd, -tuning_.lance_speed)) : Add(Mul(fwd, Dot(me.velocity, fwd)), Mul(fwd, tuning_.lance_speed));
            p.target = back ? -1 : PickLanceTarget(id, cars); // backward Lance flies straight (plan 2.8)
            p.life = tuning_.lance_life_s;
            projectiles_.push_back(p);
            break;
        }
        case Ability::Pulse:
        {
            for (size_t j = 0; j < cars.size(); ++j)
            {
                if (int(j) == id)
                    continue;
                const Vec2 rel = Sub(cars[j].position, me.position);
                if (Len(rel) <= tuning_.pulse_radius)
                    Hit(id, int(j), Ability::Pulse, tuning_.pulse_damage, rel, tuning_.pulse_knock, 1.0f, true, cars, tick);
            }
            // Clears hazards in range: projectiles and traps from anyone else.
            auto in_range = [&](Vec2 p) { return Len(Sub(p, me.position)) <= tuning_.pulse_radius; };
            for (auto it = projectiles_.begin(); it != projectiles_.end();)
                if (it->owner != id && in_range(it->position))
                {
                    Emit({ CombatEventType::Destroyed, tick, id, it->owner, it->kind });
                    it = projectiles_.erase(it);
                }
                else
                    ++it;
            for (auto it = traps_.begin(); it != traps_.end();)
                if (it->owner != id && it->flight_left <= 0 && in_range(it->position))
                {
                    Emit({ CombatEventType::Destroyed, tick, id, it->owner, Ability::Trap });
                    it = traps_.erase(it);
                }
                else
                    ++it;
            break;
        }
        case Ability::Trap:
        {
            Trap t{ next_id_++, id };
            if (back)
                t.position = Add(me.position, Mul(fwd, -(tuning_.car_radius_m + tuning_.trap_drop_m)));
            else
            {
                t.flight_left = 0.5f;
                t.position = Add(me.position, Add(Mul(fwd, tuning_.trap_throw_m), Mul(me.velocity, 0.5f)));
            }
            traps_.push_back(t);
            break;
        }
        case Ability::Needle:
        {
            Projectile p{ next_id_++, Ability::Needle, id };
            p.position = Add(me.position, Mul(fwd, back ? -T : T));
            p.velocity = Add(me.velocity, Mul(fwd, back ? -tuning_.needle_speed : tuning_.needle_speed));
            p.life = tuning_.needle_life_s;
            projectiles_.push_back(p);
            break;
        }
        case Ability::Ward:
            c.ward_time = tuning_.ward_time_s;
            c.ward_hits = tuning_.ward_hits;
            break;
        case Ability::Mend:
        {
            const float before = c.health;
            c.health = std::min(c.max_health, c.health + tuning_.mend_heal);
            Emit({ CombatEventType::Heal, tick, id, id, Ability::Mend, c.health - before });
            break;
        }
        case Ability::Storm:
        {
            // Plan 2.8 "öndəki marşrutda": placed ahead of the race leader so trailing cars can
            // come back (plan 2.1 principle 5). Bands alternate sides; the rest of the road stays safe.
            int leader = id;
            for (size_t j = 0; j < cars.size(); ++j)
                if (!cars[j].finished && !combatants_[j].wrecked && cars[j].race_progress > cars[size_t(leader)].race_progress)
                    leader = int(j);
            const float leader_s = cars[size_t(leader)].route_s;
            const float hw = route_.HalfWidth();
            const float covered = 2.0f * hw * tuning_.storm_cover_fraction;
            for (int k = 0; k < tuning_.storm_zones; ++k)
            {
                StormZone z{ next_id_++, id };
                z.route_s = leader_s + tuning_.storm_first_ahead_m + k * tuning_.storm_spacing_m;
                z.lateral_min = (k % 2 == 0) ? -hw : hw - covered;
                z.lateral_max = (k % 2 == 0) ? -hw + covered : hw;
                z.warning_left = tuning_.storm_warning_s;
                z.active_left = tuning_.storm_active_s;
                storms_.push_back(z);
            }
            Emit({ CombatEventType::StormWarning, tick, id, leader, Ability::Storm });
            break;
        }
        default:
            return false;
        }

        Emit({ CombatEventType::Use, tick, id, -1, a });
        return true;
    }

    MountedWeapon MountedWeaponFromString(const std::string& name)
    {
        return name == "minigun" ? MountedWeapon::Minigun : name == "rockets" ? MountedWeapon::Rockets : name == "oil" ? MountedWeapon::Oil
             : name == "spikes"  ? MountedWeapon::Spikes  : MountedWeapon::None;
    }

    float MountedCooldown(MountedWeapon w)
    {
        switch (w)
        {
        case MountedWeapon::Minigun: return 5.0f;
        case MountedWeapon::Rockets: return 9.0f;
        case MountedWeapon::Oil: return 7.0f;
        case MountedWeapon::Spikes: return 8.0f;
        default: return 0.0f;
        }
    }

    void CombatSystem::Step(uint64_t tick, const std::vector<CombatCar>& cars, const std::vector<CombatCommand>& commands, float dt)
    {
        events_.clear();

        // 1. Timers.
        for (size_t i = 0; i < combatants_.size(); ++i)
        {
            Combatant& c = combatants_[i];
            if (c.ward_time > 0.0f)
            {
                c.ward_time -= dt;
                if (c.ward_time <= 0.0f)
                {
                    c.ward_time = 0.0f;
                    c.ward_hits = 0;
                    Emit({ CombatEventType::WardDown, tick, int(i), int(i), Ability::Ward });
                }
            }
            c.protect_time = std::max(0.0f, c.protect_time - dt);
            c.heavy_chain_time = std::max(0.0f, c.heavy_chain_time - dt);
            c.surge_time = std::max(0.0f, c.surge_time - dt);
            c.brake_time = std::max(0.0f, c.brake_time - dt);
            c.mounted_cooldown = std::max(0.0f, c.mounted_cooldown - dt);
            c.burst_timer = std::max(0.0f, c.burst_timer - dt);
        }
        for (PickupPad& p : pads_)
            p.respawn_left = std::max(0.0f, p.respawn_left - dt);

        // 2. Own actions (so a Mend used this step lands before this step's hits).
        for (size_t i = 0; i < combatants_.size() && i < commands.size(); ++i)
        {
            Combatant& c = combatants_[i];
            const CombatCommand& cmd = commands[i];
            if (c.wrecked || cars[i].finished)
                continue;
            if (cmd.select >= 0 && cmd.select < 3)
                c.selected = cmd.select;
            if (cmd.cycle)
                c.selected = (c.selected + 1) % 3;
            if (cmd.drop)
                c.slots[size_t(c.selected)] = Slot{};
            if (cmd.use)
                Use(int(i), cmd.direction, cars, tick);
            // Mounted weapon (D-064): the cooldown starts when it fires; a minigun burst spreads
            // its three rounds 80 ms apart.
            if (cmd.fire_mounted && c.mounted != MountedWeapon::None && c.mounted_cooldown <= 0.0f && c.burst_left == 0)
            {
                bool fired = false;
                switch (c.mounted)
                {
                case MountedWeapon::Minigun: c.burst_left = 3; c.burst_timer = 0.0f; fired = true; break;
                case MountedWeapon::Rockets: fired = Fire(int(i), Ability::Lance, UseDirection::Forward, cars, tick); break;
                case MountedWeapon::Oil: fired = Fire(int(i), Ability::Trap, UseDirection::Backward, cars, tick); break;
                case MountedWeapon::Spikes: fired = Fire(int(i), Ability::Pulse, UseDirection::Forward, cars, tick); break;
                default: break;
                }
                if (fired)
                    c.mounted_cooldown = MountedCooldown(c.mounted);
            }
            if (c.burst_left > 0 && c.burst_timer <= 0.0f)
            {
                Fire(int(i), Ability::Needle, UseDirection::Forward, cars, tick);
                c.burst_timer = 0.08f;
                --c.burst_left;
            }
        }

        // 3. Pickups: a full inventory leaves the pad for someone else (no silent replacement).
        for (PickupPad& pad : pads_)
        {
            if (pad.respawn_left > 0.0f)
                continue;
            for (size_t i = 0; i < cars.size(); ++i)
            {
                Combatant& c = combatants_[i];
                if (c.wrecked || !c.can_pickup || cars[i].finished || Len(Sub(cars[i].position, pad.position)) > tuning_.pickup_radius)
                    continue;
                auto empty = std::find_if(c.slots.begin(), c.slots.end(), [](const Slot& s) { return s.ability == Ability::None; });
                if (empty == c.slots.end())
                    continue;
                empty->ability = pad.ability;
                empty->charges = pad.ability == Ability::Needle ? tuning_.needle_shots : 1;
                pad.respawn_left = tuning_.pickup_respawn_s;
                Emit({ CombatEventType::Pickup, tick, int(i), int(i), pad.ability });
                break;
            }
        }

        // 4. Traps: flight, arming, lifetime.
        for (auto it = traps_.begin(); it != traps_.end();)
        {
            if (it->flight_left > 0.0f)
                it->flight_left = std::max(0.0f, it->flight_left - dt);
            else
                it->age += dt;
            if (it->age > tuning_.trap_life_s)
                it = traps_.erase(it);
            else
                ++it;
        }

        // 5. Projectiles: move, intercept, hit.
        for (Projectile& p : projectiles_)
        {
            if (p.kind == Ability::Lance && p.target >= 0)
            {
                if (!Targetable(p.target, cars))
                    p.target = -1; // target wrecked/finished: fly on straight, never hit a corpse
                else
                {
                    const float speed = Len(p.velocity);
                    const Vec2 want = Norm(Sub(cars[size_t(p.target)].position, p.position));
                    const Vec2 have = Norm(p.velocity);
                    const float cross = have.x * want.z - have.z * want.x;
                    const float angle = std::atan2(cross, Dot(have, want));
                    const float turn = std::clamp(angle, -tuning_.lance_turn_rate * dt, tuning_.lance_turn_rate * dt);
                    const float cs = std::cos(turn), sn = std::sin(turn);
                    // Rotate in the x-z plane consistent with the cross product sign above.
                    const Vec2 rotated{ have.x * cs - have.z * sn, have.x * sn + have.z * cs };
                    p.velocity = Mul(rotated, speed);
                }
            }
            p.position = Add(p.position, Mul(p.velocity, dt));
            p.age += dt;
        }

        std::vector<uint32_t> dead;
        auto is_dead = [&](uint32_t pid) { return std::find(dead.begin(), dead.end(), pid) != dead.end(); };

        // Needles shoot down enemy Lances and Traps (plan counter table: 2 Needle shots).
        for (Projectile& n : projectiles_)
        {
            if (n.kind != Ability::Needle || is_dead(n.id))
                continue;
            for (Projectile& l : projectiles_)
            {
                if (l.kind != Ability::Lance || l.owner == n.owner || is_dead(l.id))
                    continue;
                if (Len(Sub(l.position, n.position)) <= tuning_.lance_radius + tuning_.needle_radius + 0.5f)
                {
                    dead.push_back(n.id);
                    if (++l.hits_taken >= tuning_.lance_needle_hits)
                    {
                        dead.push_back(l.id);
                        Emit({ CombatEventType::Destroyed, tick, n.owner, l.owner, Ability::Lance });
                    }
                    break;
                }
            }
            if (is_dead(n.id))
                continue;
            for (auto it = traps_.begin(); it != traps_.end(); ++it)
            {
                if (it->owner == n.owner || it->flight_left > 0.0f)
                    continue;
                if (Len(Sub(it->position, n.position)) <= tuning_.trap_radius)
                {
                    dead.push_back(n.id);
                    if (++it->hits_taken >= tuning_.trap_needle_hits)
                    {
                        Emit({ CombatEventType::Destroyed, tick, n.owner, it->owner, Ability::Trap });
                        traps_.erase(it);
                    }
                    break;
                }
            }
        }

        // A Lance flying into a landed Trap sets it off: both are destroyed (Trap counters Lance).
        for (Projectile& l : projectiles_)
        {
            if (l.kind != Ability::Lance || is_dead(l.id))
                continue;
            for (auto it = traps_.begin(); it != traps_.end(); ++it)
                if (it->flight_left <= 0.0f && Len(Sub(it->position, l.position)) <= tuning_.trap_radius + tuning_.lance_radius)
                {
                    dead.push_back(l.id);
                    Emit({ CombatEventType::Destroyed, tick, it->owner, l.owner, Ability::Lance });
                    traps_.erase(it);
                    break;
                }
        }

        // Projectile vs car.
        for (Projectile& p : projectiles_)
        {
            if (is_dead(p.id))
                continue;
            const float radius = tuning_.car_radius_m + (p.kind == Ability::Lance ? tuning_.lance_radius : tuning_.needle_radius);
            for (size_t j = 0; j < cars.size(); ++j)
            {
                if (int(j) == p.owner && p.age < tuning_.owner_grace_s)
                    continue;
                if (!Targetable(int(j), cars) || Len(Sub(cars[j].position, p.position)) > radius)
                    continue;
                const Vec2 dir = Norm(p.velocity);
                if (p.kind == Ability::Lance)
                    Hit(p.owner, int(j), Ability::Lance, tuning_.lance_damage, dir, tuning_.lance_knock, 2.0f, true, cars, tick);
                else
                    Hit(p.owner, int(j), Ability::Needle, tuning_.needle_damage, dir, tuning_.needle_knock, 0.0f, false, cars, tick);
                dead.push_back(p.id); // one projectile, one hit
                break;
            }
        }
        projectiles_.erase(std::remove_if(projectiles_.begin(), projectiles_.end(), [&](const Projectile& p) {
            return is_dead(p.id) || p.age > p.life;
        }), projectiles_.end());

        // 6. Armed traps vs cars (owner included once armed).
        for (auto it = traps_.begin(); it != traps_.end();)
        {
            bool fired = false;
            if (it->flight_left <= 0.0f && it->age >= tuning_.trap_arm_s)
            {
                for (size_t j = 0; j < cars.size(); ++j)
                {
                    if (!Targetable(int(j), cars) || Len(Sub(cars[j].position, it->position)) > tuning_.trap_radius + tuning_.car_radius_m * 0.5f)
                        continue;
                    Hit(it->owner, int(j), Ability::Trap, tuning_.trap_damage, Forward(cars[j].heading_rad), -2.0f, tuning_.trap_knock_up, true, cars, tick);
                    fired = true;
                    break;
                }
            }
            it = fired ? traps_.erase(it) : it + 1;
        }

        // 7. Storm zones: warning, then each zone damages each car at most once.
        for (auto it = storms_.begin(); it != storms_.end();)
        {
            StormZone& z = *it;
            if (z.warning_left > 0.0f)
                z.warning_left = std::max(0.0f, z.warning_left - dt);
            else
            {
                for (size_t j = 0; j < cars.size(); ++j)
                {
                    if (int(j) == z.owner) // a leader calling Storm must not drive into it (D-043)
                        continue;
                    if (std::find(z.already_hit.begin(), z.already_hit.end(), int(j)) != z.already_hit.end())
                        continue;
                    if (route_.Ahead(z.route_s, cars[j].route_s) > tuning_.storm_zone_length_m)
                        continue;
                    const float lateral = route_.Project(cars[j].position, cars[j].route_s).lateral;
                    if (lateral < z.lateral_min || lateral > z.lateral_max)
                        continue;
                    z.already_hit.push_back(int(j));
                    // Push toward the safe side of the road.
                    const Vec2 dir = route_.DirectionAt(z.route_s);
                    const float side = z.lateral_min < 0.0f ? 1.0f : -1.0f;
                    Hit(z.owner, int(j), Ability::Storm, tuning_.storm_damage, Vec2{ dir.z * side, -dir.x * side }, tuning_.storm_knock, 1.0f, false, cars, tick);
                }
                z.active_left -= dt;
            }
            it = (z.warning_left <= 0.0f && z.active_left <= 0.0f) ? storms_.erase(it) : it + 1;
        }

        // 7b. Crash damage queued by the runtime (walls, car-to-car).
        for (const auto& [id, dmg] : pending_impacts_)
            if (id >= 0 && size_t(id) < combatants_.size() && !combatants_[size_t(id)].wrecked && dmg > 0.0f)
            {
                Combatant& c = combatants_[size_t(id)];
                c.health = std::max(0.0f, c.health - dmg);
                Emit({ CombatEventType::Damage, tick, -1, id, Ability::None, dmg });
            }
        pending_impacts_.clear();

        // 8. Wrecks.
        for (size_t i = 0; i < combatants_.size(); ++i)
        {
            Combatant& c = combatants_[i];
            if (!c.wrecked && c.health <= 0.0f)
            {
                c.wrecked = true;
                Emit({ CombatEventType::Wreck, tick, c.last_attacker, int(i) });
            }
        }
    }
}
