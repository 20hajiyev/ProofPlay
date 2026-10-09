// Plan 5.3 combat tests on the engine-independent CombatSystem.
#include "TestHarness.h"
#include "racer/Combat.h"
#include "racer/CombatTuning.h"
#include <cstdio>
#include <nlohmann/json.hpp>
#include <sstream>
#include <fstream>

#include <cmath>

using namespace racer;

namespace
{
    constexpr float kDt = 1.0f / 120.0f;

    struct Arena
    {
        TrackRoute route{ { { 0, 0 }, { 0, 3000 }, { 30, 3000 }, { 30, 0 } }, 8.0f }; // long straight heading +Z
        CombatSystem sys{ route };
        std::vector<CombatCar> cars;
        std::vector<CombatCommand> cmds;
        std::vector<CombatEvent> log;
        uint64_t tick = 0;

        int Add(Vec2 pos, float hp = 100, float mass = 1300)
        {
            CombatCar c;
            c.position = pos;
            c.mass_kg = mass;
            c.route_s = route.Project(pos).s;
            c.race_progress = c.route_s;
            cars.push_back(c);
            cmds.push_back({});
            return sys.AddCombatant(hp);
        }
        void Use(int id, UseDirection d = UseDirection::Forward) { cmds[size_t(id)].use = true; cmds[size_t(id)].direction = d; }
        void Step(int n = 1)
        {
            for (int i = 0; i < n; ++i)
            {
                for (CombatCar& c : cars)
                {
                    c.position.x += c.velocity.x * kDt;
                    c.position.z += c.velocity.z * kDt;
                    c.route_s = route.Project(c.position, c.route_s).s;
                    c.race_progress = c.route_s;
                }
                sys.Step(++tick, cars, cmds, kDt);
                log.insert(log.end(), sys.Events().begin(), sys.Events().end());
                for (auto& c : cmds)
                    c = {};
            }
        }
        void Seconds(float s) { Step(int(s / kDt + 0.5f)); }
        int Count(CombatEventType t, int target = -1, Ability a = Ability::None) const
        {
            int n = 0;
            for (auto& e : log)
                n += e.type == t && (target < 0 || e.target == target) && (a == Ability::None || e.ability == a);
            return n;
        }
        float Health(int id) const { return sys.Get(id).health; }
    };
}

TEST_CASE("combat: pickup fills the first empty slot; a full inventory leaves the pad")
{
    Arena a;
    const int me = a.Add({ 0, 100 });
    a.sys.AddPickupPad({ 0, 100 }, Ability::Needle);
    a.Step();
    CHECK(a.sys.Get(me).slots[0].ability == Ability::Needle && a.sys.Get(me).slots[0].charges == 3);
    CHECK(a.sys.Pads()[0].respawn_left > 7.9f);
    a.sys.GiveAbility(me, Ability::Ward);
    a.sys.GiveAbility(me, Ability::Mend);
    a.Seconds(8.1f);                        // pad respawned, inventory full
    CHECK(a.sys.Pads()[0].respawn_left == 0.0f);
    CHECK(a.Count(CombatEventType::Pickup) == 1);
    CHECK(a.sys.Get(me).slots[1].ability == Ability::Ward); // nothing silently replaced
}

TEST_CASE("combat: Needle has three shots and the slot empties after the last")
{
    Arena a;
    const int me = a.Add({ 0, 100 });
    a.sys.GiveAbility(me, Ability::Needle);
    for (int shot = 0; shot < 3; ++shot)
    {
        a.Use(me);
        a.Step();
    }
    CHECK(a.sys.Get(me).slots[0].ability == Ability::None);
    CHECK(a.Count(CombatEventType::Use, -1, Ability::Needle) == 3);
}

TEST_CASE("combat: forward Lance homes onto the car ahead; backward Lance flies straight back")
{
    Arena a;
    const int me = a.Add({ 0, 100 });
    const int ahead = a.Add({ 4, 160 });  // slightly off-axis: needs homing
    const int behind = a.Add({ 0, 60 });
    a.sys.GiveAbility(me, Ability::Lance);
    a.sys.GiveAbility(me, Ability::Lance);
    a.Use(me);
    a.Seconds(2.0f);
    CHECK(a.Health(ahead) == 72.0f);
    CHECK(a.Health(behind) == 100.0f);
    a.Use(me, UseDirection::Backward);
    a.Seconds(2.0f);
    CHECK(a.Health(behind) == 72.0f);
}

TEST_CASE("combat: Lance target selection ignores cars not ahead along the route")
{
    // Hairpin: out leg at x = 0 heading +Z, return leg at x = 12 heading -Z. Driving back down
    // the return leg, a car on the out leg 40 m "ahead" is in the cone but ~1 km behind by route.
    Arena a;
    a.route = TrackRoute({ { 0, 0 }, { 0, 3000 }, { 12, 3000 }, { 12, 0 } }, 5.0f);
    const int me = a.Add({ 12, 500 });
    a.cars[size_t(me)].heading_rad = 3.14159265f;
    const int other_leg = a.Add({ 0, 460 });
    const int legit = a.Add({ 12, 440 }); // same leg, 60 m ahead of me along the route
    a.sys.GiveAbility(me, Ability::Lance);
    a.sys.GiveAbility(me, Ability::Lance);
    a.Use(me);
    a.Step();
    CHECK(a.sys.Projectiles().size() == 1 && a.sys.Projectiles()[0].target == legit);
    (void)other_leg;
    // Without the legit car only the other-leg car is in view: the Lance must fly unguided.
    a.cars[size_t(legit)].finished = true;
    a.Seconds(3.0f);
    a.Use(me);
    a.Step();
    CHECK(!a.sys.Projectiles().empty() && a.sys.Projectiles().back().target == -1);
}

TEST_CASE("combat: Ward blocks two hits, the third lands; Ward also expires after 4 s")
{
    Arena a;
    const int me = a.Add({ 0, 100 });
    const int target = a.Add({ 0, 115 });
    a.sys.GiveAbility(target, Ability::Ward);
    a.sys.GiveAbility(me, Ability::Needle);
    a.Use(target);
    a.Step();
    for (int i = 0; i < 3; ++i)
    {
        a.Use(me);
        a.Seconds(0.3f);
    }
    CHECK(a.Count(CombatEventType::Blocked, target) == 2);
    CHECK(a.Count(CombatEventType::WardDown, target) == 1);
    CHECK(a.Health(target) == 92.0f);

    Arena b;
    const int w = b.Add({ 0, 100 });
    b.sys.GiveAbility(w, Ability::Ward);
    b.Use(w);
    b.Seconds(3.9f);
    CHECK(b.sys.Get(w).ward_time > 0.0f);
    b.Seconds(0.2f);
    CHECK(b.sys.Get(w).ward_time == 0.0f && b.sys.Get(w).ward_hits == 0);
}

TEST_CASE("combat: Lance counters - timely Pulse, Trap behind and two Needles destroy it")
{
    { // Pulse
        Arena a;
        const int shooter = a.Add({ 0, 100 });
        const int target = a.Add({ 0, 140 });
        a.sys.GiveAbility(shooter, Ability::Lance);
        a.sys.GiveAbility(target, Ability::Pulse);
        a.Use(shooter);
        // The window is tight: the Lance is within 6 m of the target from ~0.45 s and hits
        // at ~0.51 s (70 m/s over 40 m).
        a.Seconds(0.46f);
        a.Use(target);
        a.Seconds(1.0f);
        CHECK(a.Health(target) == 100.0f);
        CHECK(a.Count(CombatEventType::Destroyed, shooter, Ability::Lance) == 1);
    }
    { // Trap dropped behind
        Arena a;
        const int shooter = a.Add({ 0, 100 });
        const int target = a.Add({ 0, 160 });
        a.sys.GiveAbility(shooter, Ability::Lance);
        a.sys.GiveAbility(target, Ability::Trap);
        a.Use(target, UseDirection::Backward);
        a.Seconds(0.5f);
        a.Use(shooter);
        a.Seconds(2.0f);
        CHECK(a.Health(target) == 100.0f);
    }
    { // Two Needle shots fired backward at the incoming Lance
        Arena a;
        const int shooter = a.Add({ 0, 100 });
        const int target = a.Add({ 0, 200 });
        a.sys.GiveAbility(shooter, Ability::Lance);
        a.sys.GiveAbility(target, Ability::Needle);
        a.Use(shooter);
        a.Step();
        for (int i = 0; i < 2; ++i)
        {
            a.Use(target, UseDirection::Backward);
            a.Step(3);
        }
        a.Seconds(2.0f);
        CHECK(a.Count(CombatEventType::Destroyed, shooter, Ability::Lance) == 1);
        CHECK(a.Health(target) == 100.0f);
    }
}

TEST_CASE("combat: Trap arms after 0.35 s, then hits once for 24")
{
    Arena a;
    const int layer = a.Add({ 0, 100 });
    const int victim = a.Add({ 0, 94 });  // right where the trap lands
    a.sys.GiveAbility(layer, Ability::Trap);
    a.Use(layer, UseDirection::Backward);
    a.Seconds(0.2f);
    CHECK(a.Health(victim) == 100.0f);    // not armed yet
    a.cars[size_t(victim)].position = { 0, 60 };
    a.Seconds(0.5f);
    CHECK(a.sys.Traps().size() == 1);
    a.cars[size_t(victim)].position = { 0, 95.6f }; // drive onto the armed trap
    a.Seconds(0.5f);
    CHECK(a.Health(victim) == 76.0f);
    CHECK(a.sys.Traps().empty());
    a.Seconds(1.0f);
    CHECK(a.Count(CombatEventType::Damage, victim) == 1);
}

TEST_CASE("combat: Trap counters - Ward, Pulse and two Needles")
{
    { // Ward
        Arena a;
        const int layer = a.Add({ 0, 100 });
        const int v = a.Add({ 0, 50 });
        a.sys.GiveAbility(layer, Ability::Trap);
        a.sys.GiveAbility(v, Ability::Ward);
        a.Use(layer, UseDirection::Backward);
        a.Seconds(1.0f);
        a.Use(v);
        a.Step();
        a.cars[size_t(v)].position = { 0, 95.6f };
        a.Seconds(0.2f);
        CHECK(a.Health(v) == 100.0f);
        CHECK(a.Count(CombatEventType::Blocked, v) == 1);
    }
    { // Pulse clears it
        Arena a;
        const int layer = a.Add({ 0, 100 });
        const int v = a.Add({ 0, 90 });
        a.sys.GiveAbility(layer, Ability::Trap);
        a.sys.GiveAbility(v, Ability::Pulse);
        a.Use(layer, UseDirection::Backward);
        a.cars[size_t(v)].position = { 0, 91 }; // 4.6 m from the trap: inside Pulse, outside the trigger
        a.Seconds(1.0f);
        a.Use(v);
        a.Step();
        CHECK(a.sys.Traps().empty());
    }
    { // Two Needles
        Arena a;
        const int layer = a.Add({ 0, 100 });
        const int v = a.Add({ 0, 60 });
        a.sys.GiveAbility(layer, Ability::Trap);
        a.sys.GiveAbility(v, Ability::Needle);
        a.Use(layer, UseDirection::Backward);
        a.Seconds(1.0f);
        a.Use(v);
        a.Seconds(0.5f); // ~35 m at 110 m/s
        CHECK(a.sys.Traps().size() == 1);
        a.Use(v);
        a.Seconds(0.5f);
        CHECK(a.sys.Traps().empty());
    }
}

TEST_CASE("combat: Pulse hits inside 6 m only, 18 damage")
{
    Arena a;
    const int me = a.Add({ 0, 100 });
    const int near_car = a.Add({ 5, 100 });
    const int far_car = a.Add({ 0, 107 });
    a.sys.GiveAbility(me, Ability::Pulse);
    a.Use(me);
    a.Step();
    CHECK(a.Health(near_car) == 82.0f);
    CHECK(a.Health(far_car) == 100.0f);
    CHECK(a.Health(me) == 100.0f);
}

TEST_CASE("combat: Mend heals 35 capped at max, and lands before same-step damage")
{
    Arena a;
    const int me = a.Add({ 0, 100 }, 90);
    const int enemy = a.Add({ 3, 100 });
    a.sys.GiveAbility(me, Ability::Mend);
    a.sys.GiveAbility(me, Ability::Mend);
    a.sys.GiveAbility(enemy, Ability::Pulse);
    a.Use(me);
    a.Step();
    CHECK(a.Health(me) == 90.0f);         // already full: capped
    // Bring health to 10, then Mend and a Pulse arrive in the same step.
    const_cast<Combatant&>(a.sys.Get(me)).health = 10.0f;
    a.Use(me);
    a.Use(enemy);
    a.Step();
    CHECK(a.Health(me) == 27.0f);         // 10 + 35 - 18, not wrecked
    CHECK(!a.sys.Get(me).wrecked);
}

TEST_CASE("combat: respawn protection - immune and unable to attack for 2 s")
{
    Arena a;
    const int me = a.Add({ 0, 100 });
    const int enemy = a.Add({ 3, 100 });
    a.sys.Respawn(me);
    a.sys.GiveAbility(me, Ability::Pulse);
    a.sys.GiveAbility(enemy, Ability::Pulse);
    a.Use(enemy);
    a.Use(me);
    a.Step();
    CHECK(a.Health(me) == 100.0f);
    CHECK(a.Health(enemy) == 100.0f);     // my Pulse did not fire
    CHECK(a.sys.Get(me).slots[0].ability == Ability::Pulse);
    a.Seconds(2.0f);
    a.Use(me);
    a.Step();
    CHECK(a.Health(enemy) == 82.0f);
}

TEST_CASE("combat: Lance whose target is wrecked flies on and never hits the wreck")
{
    Arena a;
    const int me = a.Add({ 0, 100 });
    const int target = a.Add({ 3, 180 }, 20);
    const int killer = a.Add({ 6, 180 });
    a.sys.GiveAbility(me, Ability::Lance);
    a.sys.GiveAbility(killer, Ability::Pulse);
    a.Use(me);
    a.Step();
    CHECK(a.sys.Projectiles().size() == 1 && a.sys.Projectiles()[0].target == target);
    a.Use(killer); // wrecks the target (20 HP - 18 = 2... then second pulse)
    a.Step();
    const_cast<Combatant&>(a.sys.Get(target)).health = 0.0f;
    a.Step();
    CHECK(a.sys.Get(target).wrecked);
    a.Seconds(3.0f);
    CHECK(a.Count(CombatEventType::Damage, target, Ability::Lance) == 0);
}

TEST_CASE("combat: one projectile hits once; one Storm zone hits each car once")
{
    Arena a;
    const int me = a.Add({ 0, 100 });
    const int t = a.Add({ 0, 115 });
    a.sys.GiveAbility(me, Ability::Needle);
    a.Use(me);
    a.Seconds(1.0f);
    CHECK(a.Count(CombatEventType::Damage, t) == 1);

    Arena s;
    const int user = s.Add({ 0, 100 });
    const int leader = s.Add({ -5, 300 });      // left side of the road (lateral -5)
    s.sys.GiveAbility(user, Ability::Storm);
    s.Use(user);
    s.Step();
    CHECK(s.sys.StormZones().size() == 3);
    CHECK(s.Count(CombatEventType::StormWarning) == 1);
    s.Seconds(1.0f);                              // warning phase: no damage yet
    CHECK(s.Health(leader) == 100.0f);
    s.cars[size_t(leader)].position = { -5, 385 }; // inside zone 1 (s 380..390), left band
    s.Seconds(2.0f);
    CHECK(s.Count(CombatEventType::Damage, leader, Ability::Storm) == 1);
}

TEST_CASE("combat: every Storm zone leaves a safe path")
{
    Arena s;
    const int user = s.Add({ 0, 100 });
    s.Add({ 0, 300 });
    s.sys.GiveAbility(user, Ability::Storm);
    s.Use(user);
    s.Step();
    const float hw = s.route.HalfWidth();
    for (const StormZone& z : s.sys.StormZones())
    {
        const float safe = (z.lateral_min > -hw ? z.lateral_min + hw : 0.0f) + (z.lateral_max < hw ? hw - z.lateral_max : 0.0f);
        CHECK(safe >= 5.0f); // wider than a car (~2 m) with margin
    }
}

TEST_CASE("combat: finished cars take no damage even when hit in the finishing step")
{
    Arena a;
    const int me = a.Add({ 0, 100 });
    const int t = a.Add({ 3, 100 });
    a.sys.GiveAbility(me, Ability::Pulse);
    a.cars[size_t(t)].finished = true;
    a.Use(me);
    a.Step();
    CHECK(a.Health(t) == 100.0f);
}

TEST_CASE("combat: a second heavy push within 0.8 s is reduced; heavier cars move less")
{
    Arena a;
    const int me = a.Add({ 0, 100 });
    const int light = a.Add({ 3, 100 }, 100, 1150);
    const int heavy = a.Add({ -3, 100 }, 120, 1850);
    a.sys.GiveAbility(me, Ability::Pulse);
    a.sys.GiveAbility(me, Ability::Pulse);
    a.Use(me);
    a.Step();
    float dv_light = 0, dv_heavy = 0;
    for (auto& e : a.log)
        if (e.type == CombatEventType::Knock)
            (e.target == light ? dv_light : dv_heavy) = e.delta_v;
    CHECK(dv_light > dv_heavy);
    a.log.clear();
    a.Seconds(0.3f);
    a.Use(me);
    a.Step();
    float dv_second = 0;
    for (auto& e : a.log)
        if (e.type == CombatEventType::Knock && e.target == light)
            dv_second = e.delta_v;
    CHECK(std::fabs(dv_second - dv_light * 0.3f) < 1e-4f);
    (void)heavy;
}

TEST_CASE("combat: Surge forward boosts 2.5 s; backward brakes first, then boosts")
{
    Arena a;
    const int me = a.Add({ 0, 100 });
    a.sys.GiveAbility(me, Ability::Surge);
    a.sys.GiveAbility(me, Ability::Surge);
    a.Use(me);
    a.Step();
    CHECK(a.sys.BoostAccel(me) > 0.0f);
    a.Seconds(2.5f);
    CHECK(a.sys.BoostAccel(me) == 0.0f);
    a.Use(me, UseDirection::Backward);
    a.Step();
    CHECK(a.sys.BoostAccel(me) < 0.0f);
    a.Seconds(0.4f);
    CHECK(a.sys.BoostAccel(me) > 0.0f);
}

TEST_CASE("combat: a wreck is credited to the car that dealt the last damage")
{
    // Destruction (plan 2.12) scores wrecks caused; the Wreck event must name the attacker.
    Arena a;
    const int hunter = a.Add({ 0, 100 });
    const int target = a.Add({ 0, 104 }, 10); // inside the 6 m Pulse radius
    a.sys.GiveAbility(hunter, Ability::Pulse);
    a.Use(hunter);
    a.Seconds(1.0f);
    CHECK(a.sys.Get(target).wrecked);
    int credited = -2;
    for (const CombatEvent& e : a.log)
        if (e.type == CombatEventType::Wreck && e.target == target)
            credited = e.source;
    CHECK(credited == hunter);
}
namespace
{
    std::string ReadTuningFile()
    {
        std::ifstream f(RACER_CONTENT_DIR "/combat/tuning.json", std::ios::binary);
        std::stringstream s;
        s << f.rdbuf();
        return s.str();
    }
}

TEST_CASE("tuning: content/combat/tuning.json parses and sets every CombatTuning field")
{
    CombatTuning t;
    std::vector<ValidationIssue> issues;
    CHECK(ParseCombatTuning(ReadTuningFile(), t, issues));
    for (auto& i : issues)
        std::printf("  ISSUE %s: %s\n", i.path.c_str(), i.message.c_str());
    CHECK(CombatTuningFieldCount() == int(nlohmann::json::parse(ReadTuningFile()).at("tuning").size())); // nothing left implicit
}

TEST_CASE("tuning: unknown fields, bad values and wrong schema are rejected; partial files keep defaults")
{
    CombatTuning t;
    std::vector<ValidationIssue> issues;
    CHECK(!ParseCombatTuning(R"({"schema_version":1,"tuning":{"lance_damag":30}})", t, issues)); // typo must not be silent
    issues.clear();
    CHECK(!ParseCombatTuning(R"({"schema_version":1,"tuning":{"pulse_radius":-2}})", t, issues));
    issues.clear();
    CHECK(!ParseCombatTuning(R"({"schema_version":2,"tuning":{}})", t, issues));
    issues.clear();
    CombatTuning partial;
    CHECK(ParseCombatTuning(R"({"schema_version":1,"tuning":{"lance_damage":31,"needle_shots":4}})", partial, issues));
    CHECK(partial.lance_damage == 31.0f && partial.needle_shots == 4);
    CHECK(partial.pulse_damage == CombatTuning{}.pulse_damage);
}
TEST_CASE("combat: a Storm never hits the car that called it (leader using Storm)")
{
    // Found by the D-043 balance pass: a solo AI leader drove into its own Storm zones, took the
    // hit and was knocked 9 m wide. The user is never a target of its own Storm.
    Arena s;
    const int user = s.Add({ 0, 300 }); // the leader itself
    s.sys.GiveAbility(user, Ability::Storm);
    s.Use(user);
    s.Step();
    CHECK(s.sys.StormZones().size() == 3);
    const StormZone z = s.sys.StormZones()[0];
    const float lat = (z.lateral_min + z.lateral_max) * 0.5f;
    s.Seconds(z.warning_left + 0.1f);
    s.cars[size_t(user)].position = { lat, z.route_s + 3.0f }; // straight route: s == z
    s.Seconds(1.0f);
    CHECK(s.Count(CombatEventType::Damage, user, Ability::Storm) == 0);
}
// --- Impact damage (D-061, owner: "a car hitting a wall should take damage").
TEST_CASE("combat: impact damage - none for braking-level losses, grows with the hit, capped")
{
    CHECK(ImpactDamage(0.0f) == 0.0f);
    CHECK(ImpactDamage(10.0f) == 0.0f);   // hard braking / a brush: no damage
    CHECK(ImpactDamage(40.0f) > 10.0f);   // a proper wall hit
    CHECK(ImpactDamage(40.0f) > ImpactDamage(25.0f));
    CHECK(ImpactDamage(300.0f) <= 30.0f); // one hit never wrecks a healthy car outright
}

TEST_CASE("combat: queued impact damage lands next step, ignores Ward, and repeated hits wreck")
{
    Arena a;
    const int car = a.Add({ 0, 100 }, 90);
    a.sys.GiveAbility(car, Ability::Ward);
    a.Use(car);
    a.Step();
    a.sys.QueueImpactDamage(car, 20.0f);
    a.Step();
    CHECK(std::fabs(a.sys.Get(car).health - 70.0f) < 1e-3f);
    bool damage_event = false;
    for (const CombatEvent& e : a.log)
        damage_event = damage_event || (e.type == CombatEventType::Damage && e.target == car && e.source == -1);
    CHECK(damage_event);
    for (int i = 0; i < 4; ++i)
    {
        a.sys.QueueImpactDamage(car, 25.0f);
        a.Step();
    }
    CHECK(a.sys.Get(car).wrecked);
}

// --- Mounted weapons (D-064): garage-fitted guns with their own cooldown, no pickup slot used.
TEST_CASE("combat: a minigun fires a three-round burst, then waits for its cooldown")
{
    Arena a;
    const int me = a.Add({ 0, 100 });
    a.Add({ 0, 160 });
    a.sys.SetMountedWeapon(me, MountedWeapon::Minigun);
    a.cmds[size_t(me)].fire_mounted = true;
    a.Seconds(0.5f);
    CHECK(a.Count(CombatEventType::Use, -1, Ability::Needle) == 3);
    CHECK(a.sys.Get(me).slots[0].ability == Ability::None); // no inventory used
    a.cmds[size_t(me)].fire_mounted = true;
    a.Step();
    a.Seconds(0.5f);
    CHECK(a.Count(CombatEventType::Use, -1, Ability::Needle) == 3); // still cooling down
    CHECK(a.sys.Get(me).mounted_cooldown > 0.0f);
    a.Seconds(6.0f);
    a.cmds[size_t(me)].fire_mounted = true;
    a.Seconds(0.5f);
    CHECK(a.Count(CombatEventType::Use, -1, Ability::Needle) == 6);
}

TEST_CASE("combat: rockets home like a Lance, oil drops a trap behind, spikes knock the car alongside")
{
    Arena a;
    const int me = a.Add({ 0, 100 });
    const int other = a.Add({ 3, 102 });
    a.sys.SetMountedWeapon(me, MountedWeapon::Rockets);
    a.cmds[size_t(me)].fire_mounted = true;
    a.Step();
    CHECK(a.Count(CombatEventType::Use, -1, Ability::Lance) == 1);
    a.sys.SetMountedWeapon(me, MountedWeapon::Oil);
    a.Seconds(12.0f);
    a.cmds[size_t(me)].fire_mounted = true;
    a.Step();
    CHECK(a.sys.Traps().size() == 1 && a.sys.Traps()[0].position.z < 100.0f);
    a.sys.SetMountedWeapon(me, MountedWeapon::Spikes);
    a.Seconds(12.0f);
    const int before = a.Count(CombatEventType::Damage, other);
    a.cmds[size_t(me)].fire_mounted = true;
    a.Step();
    CHECK(a.Count(CombatEventType::Damage, other) > before);
}

TEST_CASE("combat: no mounted weapon, or a wrecked car, fires nothing")
{
    Arena a;
    const int me = a.Add({ 0, 100 }, 10);
    a.cmds[size_t(me)].fire_mounted = true;
    a.Step();
    CHECK(a.Count(CombatEventType::Use) == 0);
    a.sys.SetMountedWeapon(me, MountedWeapon::Rockets);
    a.sys.QueueImpactDamage(me, 30.0f);
    a.Step();
    CHECK(a.sys.Get(me).wrecked);
    a.cmds[size_t(me)].fire_mounted = true;
    a.Step();
    CHECK(a.Count(CombatEventType::Use) == 0);
}
