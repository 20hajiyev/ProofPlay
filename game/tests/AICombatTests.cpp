#include "TestHarness.h"
#include "racer/AICombat.h"

using namespace racer;

namespace
{
    constexpr float kDt = 1.0f / 120.0f;

    struct Fight
    {
        TrackRoute route{ { { 0, 0 }, { 0, 3000 }, { 30, 3000 }, { 30, 0 } }, 8.0f };
        CombatSystem sys{ route };
        std::vector<CombatCar> cars;
        std::vector<AICombatPlanner> ai;
        uint64_t tick = 0;
        Fight() = default;
        explicit Fight(TrackRoute r) : route(std::move(r)), sys(route) {}

        int Add(Vec2 pos, float reaction = 0.3f, AIPersonality persona = AIPersonality::Aggressor)
        {
            CombatCar c;
            c.position = pos;
            c.route_s = route.Project(pos).s;
            c.race_progress = c.route_s;
            cars.push_back(c);
            const int id = sys.AddCombatant(100);
            AICombatParams p = MakeAICombatParams(AIDifficulty::Normal, persona, uint32_t(id));
            p.reaction_s = reaction;
            ai.emplace_back(id, route, p);
            return id;
        }
        // Runs until `id` uses something (returns seconds) or the timeout passes (-1).
        float UntilUse(int id, float timeout, Ability* used = nullptr)
        {
            for (int i = 0; i < int(timeout / kDt); ++i)
            {
                std::vector<CombatCommand> cmds(cars.size());
                for (size_t k = 0; k < ai.size(); ++k)
                    cmds[k] = ai[k].Think(sys, cars, kDt);
                sys.Step(++tick, cars, cmds, kDt);
                for (auto& e : sys.Events())
                    if (e.type == CombatEventType::Use && e.source == id)
                    {
                        if (used)
                            *used = e.ability;
                        return (i + 1) * kDt;
                    }
            }
            return -1.0f;
        }
    };
}

TEST_CASE("ai combat: fires a Lance at a car ahead, only after its reaction time")
{
    Fight f;
    const int me = f.Add({ 0, 100 }, 0.3f);
    f.Add({ 0, 150 }, 5.0f, AIPersonality::LineMaster);
    f.sys.GiveAbility(me, Ability::Lance);
    Ability used = Ability::None;
    const float t = f.UntilUse(me, 3.0f, &used);
    std::printf("  INFO Lance fired after %.3f s (reaction 0.30 s)\n", t);
    CHECK(used == Ability::Lance);
    CHECK(t >= 0.3f && t < 0.45f);
}

TEST_CASE("ai combat: raises Ward against an incoming homing Lance")
{
    Fight f;
    const int shooter = f.Add({ 0, 100 }, 10.0f);
    const int me = f.Add({ 0, 180 }, 0.2f, AIPersonality::Defender);
    f.sys.GiveAbility(me, Ability::Ward);
    f.sys.GiveAbility(shooter, Ability::Lance);
    std::vector<CombatCommand> fire(2);
    fire[size_t(shooter)].use = true;
    f.sys.Step(++f.tick, f.cars, fire, kDt);
    Ability used = Ability::None;
    CHECK(f.UntilUse(me, 1.0f, &used) > 0.0f);
    CHECK(used == Ability::Ward);
    f.UntilUse(shooter, 2.0f);
    CHECK(f.sys.Get(me).health == 100.0f);
}

TEST_CASE("ai combat: mends when hurt, keeps Mend when healthy")
{
    Fight f;
    const int me = f.Add({ 0, 100 });
    f.sys.GiveAbility(me, Ability::Mend);
    CHECK(f.UntilUse(me, 1.0f) < 0.0f);
    const_cast<Combatant&>(f.sys.Get(me)).health = 30.0f;
    Ability used = Ability::None;
    CHECK(f.UntilUse(me, 1.0f, &used) > 0.0f);
    CHECK(used == Ability::Mend);
}

TEST_CASE("ai combat: no attacks during respawn protection")
{
    Fight f;
    const int me = f.Add({ 0, 100 }, 0.1f);
    f.Add({ 0, 140 }, 5.0f, AIPersonality::LineMaster);
    f.sys.GiveAbility(me, Ability::Lance);
    f.sys.Respawn(me);
    CHECK(f.UntilUse(me, 1.8f) < 0.0f);
    CHECK(f.UntilUse(me, 1.0f) > 0.0f);
}

TEST_CASE("ai combat: the leader holds Storm, a chaser uses it")
{
    Fight f;
    const int leader = f.Add({ 0, 400 }, 0.1f);
    const int chaser = f.Add({ 0, 100 }, 0.1f);
    f.sys.GiveAbility(leader, Ability::Storm);
    f.sys.GiveAbility(chaser, Ability::Storm);
    Ability used = Ability::None;
    CHECK(f.UntilUse(chaser, 1.0f, &used) > 0.0f);
    CHECK(used == Ability::Storm);
    CHECK(f.sys.Get(leader).slots[0].ability == Ability::Storm);
}

TEST_CASE("ai combat: no Surge into an S-bend whose exit points the same way as its entry")
{
    // D-043: the planner compared the heading now with the heading 60 m ahead only. Through an
    // S-bend both are equal, so it boosted into the bends (L01 container corridor): the car ran
    // 9 m wide. The whole distance a Surge covers must be straight enough.
    // Straight 0..200, then an S-bend (right then left) back to the same heading, then straight.
    Fight f(TrackRoute({ { 0, 0 }, { 0, 200 }, { 12, 225 }, { 12, 245 }, { 0, 270 }, { 0, 1500 }, { 40, 1500 }, { 40, 0 } }, 8.0f));
    const int me = f.Add({ 0, 180 }, 0.1f);
    f.cars[size_t(me)].velocity = { 0, 35 };
    f.sys.GiveAbility(me, Ability::Surge);
    CHECK(f.UntilUse(me, 0.5f) < 0.0f);                // S-bend within reach: hold it
    f.cars[size_t(me)].position = { 0, 400 };          // long straight ahead
    f.cars[size_t(me)].route_s = f.route.Project({ 0, 400 }).s;
    CHECK(f.UntilUse(me, 1.0f) > 0.0f);                // now it boosts
}