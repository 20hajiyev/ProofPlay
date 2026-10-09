#pragma once
#include "racer/Race.h"

#include <array>
#include <cstdint>
#include <vector>

namespace racer
{
    // Plan 2.8 working names; ids P01..P08 in that order.
    enum class Ability : uint8_t { None, Surge, Lance, Pulse, Trap, Needle, Ward, Mend, Storm, Count };
    const char* ToString(Ability a);

    // Starting parameters from plan 2.8. Every number here is a design dial, not a measurement.
    struct CombatTuning
    {
        float car_radius_m = 1.4f;
        float reference_mass_kg = 1300.0f; // knock delta-v is scaled by reference / car mass

        float surge_time_s = 2.5f, surge_accel = 6.0f;
        float surge_back_brake_s = 0.35f, surge_back_decel = 12.0f, surge_back_boost_s = 1.5f;

        float lance_damage = 28, lance_speed = 70, lance_turn_rate = 2.5f, lance_life_s = 4.0f, lance_radius = 0.6f;
        float lance_knock = 7.0f, lance_target_range = 120.0f, lance_target_cone_rad = 0.61f, lance_route_window = 150.0f;
        int lance_needle_hits = 2;

        float pulse_radius = 6.0f, pulse_damage = 18, pulse_knock = 6.0f;

        float trap_damage = 24, trap_life_s = 12.0f, trap_arm_s = 0.35f, trap_radius = 2.0f, trap_knock_up = 5.0f;
        float trap_throw_m = 25.0f, trap_drop_m = 3.0f;
        int trap_needle_hits = 2;

        int needle_shots = 3;
        float needle_damage = 8, needle_speed = 110, needle_life_s = 1.5f, needle_radius = 0.3f, needle_knock = 1.5f;

        float ward_time_s = 4.0f;
        int ward_hits = 2;

        float mend_heal = 35;

        int storm_zones = 3;
        float storm_damage = 15, storm_first_ahead_m = 80, storm_spacing_m = 80, storm_warning_s = 1.2f, storm_active_s = 3.0f;
        float storm_zone_length_m = 10.0f, storm_cover_fraction = 0.6f, storm_knock = 3.0f;

        float heavy_chain_s = 0.8f, heavy_chain_scale = 0.3f; // second heavy push within 0.8 s is reduced
        float respawn_protect_s = 2.0f;
        float pickup_radius = 3.0f, pickup_respawn_s = 8.0f;
        float owner_grace_s = 0.3f; // own projectiles cannot hit the owner right after launch
    };

    // Per-step view of a car, filled by the runtime from authoritative physics.
    struct CombatCar
    {
        Vec2 position;
        Vec2 velocity;
        float heading_rad = 0.0f;
        float route_s = 0.0f;
        float race_progress = 0.0f; // lap-aware, RaceRules::TotalProgress; picks the Storm leader
        float mass_kg = 1300.0f;
        bool finished = false; // finished cars neither attack nor take damage
    };

    enum class UseDirection : uint8_t { Forward, Backward };

    // One step's combat input; flags are edges (already de-duplicated by CommandBuffer).
    struct CombatCommand
    {
        bool use = false;
        UseDirection direction = UseDirection::Forward;
        bool drop = false;
        bool cycle = false;
        int select = -1; // 0..2 to pick a slot directly
        bool fire_mounted = false; // the garage-fitted weapon (D-064)
    };

    // Garage-fitted weapons (D-064): fire on their own cooldown without a pickup, each reusing an
    // ability's behaviour - Minigun: a 3-round Needle burst; Rockets: a homing Lance; Oil: a Trap
    // dropped behind; Spikes: a short-range ram shockwave (Pulse).
    enum class MountedWeapon : uint8_t { None, Minigun, Rockets, Oil, Spikes };
    MountedWeapon MountedWeaponFromString(const std::string& name);
    float MountedCooldown(MountedWeapon w);

    struct Slot
    {
        Ability ability = Ability::None;
        int charges = 0;
    };

    struct Combatant
    {
        float health = 100, max_health = 100;
        std::array<Slot, 3> slots;
        int selected = 0;
        float ward_time = 0;
        int ward_hits = 0;
        float protect_time = 0;     // after respawn: immune and cannot attack
        float heavy_chain_time = 0;
        float surge_time = 0;
        float brake_time = 0;
        bool wrecked = false;
        bool can_pickup = true;     // Destruction targets drive past pads without taking them
        int last_attacker = -1;     // credited with the wreck (Destruction scoring)
        MountedWeapon mounted = MountedWeapon::None;
        float mounted_cooldown = 0; // seconds until the mounted weapon can fire again
        int burst_left = 0;         // minigun rounds still to fire in this burst
        float burst_timer = 0;
    };

    enum class CombatEventType : uint8_t { Pickup, Use, Damage, Blocked, Knock, Heal, Wreck, Destroyed, StormWarning, WardDown };

    struct CombatEvent
    {
        CombatEventType type;
        uint64_t tick = 0;
        int source = -1;
        int target = -1;
        Ability ability = Ability::None;
        float amount = 0;       // damage / heal
        Vec2 impulse_dir;       // knock: horizontal direction
        float delta_v = 0;      // knock: horizontal speed change (m/s), already mass-scaled
        float delta_v_up = 0;
    };

    struct Projectile
    {
        uint32_t id;
        Ability kind;           // Lance or Needle
        int owner;
        int target = -1;        // Lance homing target
        Vec2 position, velocity;
        float age = 0, life = 0;
        int hits_taken = 0;     // Needle hits absorbed (Lance only)
    };

    struct Trap
    {
        uint32_t id;
        int owner;
        Vec2 position;
        float age = 0;
        float flight_left = 0; // thrown forward: lands after this
        int hits_taken = 0;
    };

    struct StormZone
    {
        uint32_t id;
        int owner;
        float route_s;
        float lateral_min, lateral_max; // covered band; the rest of the road is the safe path
        float warning_left, active_left;
        std::vector<int> already_hit;
    };

    struct PickupPad
    {
        Vec2 position;
        Ability ability;
        float respawn_left = 0; // 0 = available
    };

    // Crash damage for a sudden speed loss (km/h over ~25 ms): braking and brushes cost nothing, a
    // real hit a lot, one hit never more than 30 HP (D-061).
    float ImpactDamage(float speed_loss_kmh);

    class CombatSystem
    {
    public:
        CombatSystem(const TrackRoute& route, CombatTuning tuning = {});

        int AddCombatant(float max_health);
        int AddPickupPad(Vec2 position, Ability ability);

        // Order inside a step: timers -> own actions (use/drop/select, incl. Mend) -> pickups ->
        // projectile/trap/pulse/storm contacts -> wreck checks. Deterministic for replays.
        void Step(uint64_t tick, const std::vector<CombatCar>& cars, const std::vector<CombatCommand>& commands, float dt);

        void Respawn(int id);
        void SetMountedWeapon(int id, MountedWeapon w) { combatants_[size_t(id)].mounted = w; }
        // Wall/car crash damage measured by the runtime (D-061). Applied in the next Step before the
        // wreck check; Ward does not stop it (it is the car hitting the world, not a weapon).
        void QueueImpactDamage(int id, float damage) { pending_impacts_.push_back({ id, damage }); }
        void SetCanPickup(int id, bool value) { combatants_[size_t(id)].can_pickup = value; }
        void GiveAbility(int id, Ability ability); // tests/debug: fill the first empty slot

        const Combatant& Get(int id) const { return combatants_[size_t(id)]; }
        const std::vector<CombatEvent>& Events() const { return events_; } // this step only
        const std::vector<Projectile>& Projectiles() const { return projectiles_; }
        const std::vector<Trap>& Traps() const { return traps_; }
        const std::vector<StormZone>& StormZones() const { return storms_; }
        const std::vector<PickupPad>& Pads() const { return pads_; }
        // Longitudinal acceleration the runtime must apply this step (Surge boost / back-use brake).
        float BoostAccel(int id) const;
        const CombatTuning& Tuning() const { return tuning_; }

    private:
        void Use(int id, UseDirection dir, const std::vector<CombatCar>& cars, uint64_t tick);
        bool Fire(int id, Ability a, UseDirection dir, const std::vector<CombatCar>& cars, uint64_t tick); // the ability's effect
        void Hit(int source, int target, Ability kind, float damage, Vec2 dir, float dv, float dv_up, bool heavy,
                 const std::vector<CombatCar>& cars, uint64_t tick);
        int PickLanceTarget(int id, const std::vector<CombatCar>& cars) const;
        bool CanAttack(int id, const std::vector<CombatCar>& cars) const;
        bool Targetable(int id, const std::vector<CombatCar>& cars) const;
        void Emit(CombatEvent e) { events_.push_back(e); }

        const TrackRoute& route_;
        CombatTuning tuning_;
        std::vector<Combatant> combatants_;
        std::vector<PickupPad> pads_;
        std::vector<Projectile> projectiles_;
        std::vector<Trap> traps_;
        std::vector<StormZone> storms_;
        std::vector<CombatEvent> events_;
        std::vector<std::pair<int, float>> pending_impacts_; // car, damage: applied next step (D-061)
        uint32_t next_id_ = 1;
    };
}
