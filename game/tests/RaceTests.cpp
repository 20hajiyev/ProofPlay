#include "TestHarness.h"
#include "racer/Race.h"

#include <cmath>

using namespace racer;

namespace
{
    // 200 x 400 m rectangle, driven anticlockwise seen from above: start at (0,0) heading +Z.
    TrackRoute Rect() { return TrackRoute({ { 0, 0 }, { 0, 400 }, { 200, 400 }, { 200, 0 } }, 7.0f); }

    RaceConfig Config(int laps = 3)
    {
        RaceConfig c;
        c.laps = laps;
        c.checkpoints_s = { 0, 300, 600, 900 };
        return c;
    }

    void StartRace(RaceRules& r)
    {
        r.BeginGrid();
        r.Tick(0.0f); // -> Countdown
        for (int i = 0; i < 400 && r.Phase() != RacePhase::Racing; ++i)
            r.Tick(1.0f / 120.0f);
    }

    // Drives participant `id` forward along the route in 5 m steps.
    void Drive(RaceRules& r, const TrackRoute& route, int id, float from_s, float metres, uint64_t& tick)
    {
        for (float d = 0; d <= metres; d += 5.0f)
        {
            r.Update(id, route.PointAt(from_s + d), ++tick);
            r.Rank();
        }
    }
}

TEST_CASE("race: route length, projection and lateral side")
{
    TrackRoute route = Rect();
    CHECK(std::fabs(route.Length() - 1200.0f) < 1e-3f);
    TrackRoute::Projection p = route.Project({ 5, 100 });
    CHECK(std::fabs(p.s - 100.0f) < 1e-3f);
    CHECK(std::fabs(p.lateral - 5.0f) < 1e-3f); // +X is right of travel when heading +Z
    CHECK(std::fabs(route.Ahead(1150, 50) - 100.0f) < 1e-3f);
}

TEST_CASE("race: projection near a hairpin stays on the section the car is on")
{
    // Out and back 12 m apart: a global nearest-point search would snap to the other leg.
    TrackRoute pin({ { 0, 0 }, { 0, 300 }, { 12, 300 }, { 12, 0 } }, 4.0f);
    CHECK(pin.Project({ 7, 150 }).s > 400.0f);          // global: the return leg is nearer
    CHECK(std::fabs(pin.Project({ 7, 150 }, 150.0f).s - 150.0f) < 1e-3f); // hinted: stays on the out leg
}

TEST_CASE("race: three clean laps finish the race")
{
    TrackRoute route = Rect();
    RaceRules r(route, Config(3));
    CHECK(ValidateRaceConfig(route, Config(3)).empty());
    const int id = r.AddParticipant({ 20, 0 }); // on the grid, 20 m before the line
    StartRace(r);
    uint64_t tick = 0;
    Drive(r, route, id, 1180, 20 + 1200 * 3 + 10, tick);
    const ParticipantState& p = r.Participant(id);
    CHECK(p.finished);
    CHECK(p.laps_done == 3);
    CHECK(p.position == 1);
    CHECK(r.Phase() == RacePhase::Finished);
}

TEST_CASE("race: inputs are locked until the countdown ends")
{
    TrackRoute route = Rect();
    RaceRules r(route, Config());
    r.AddParticipant({ 20, 0 });
    CHECK(r.InputsLocked());
    r.BeginGrid();
    r.Tick(0.0f);
    CHECK(r.Phase() == RacePhase::Countdown);
    for (int i = 0; i < 300; ++i) // 2.5 s
        r.Tick(1.0f / 120.0f);
    CHECK(r.InputsLocked());
    for (int i = 0; i < 70; ++i)
        r.Tick(1.0f / 120.0f);
    CHECK(r.Phase() == RacePhase::Racing);
    CHECK(!r.InputsLocked());
}

TEST_CASE("race: driving before the start does not earn progress")
{
    TrackRoute route = Rect();
    RaceRules r(route, Config(1));
    const int id = r.AddParticipant({ 20, 0 });
    r.BeginGrid();
    r.Tick(0.0f);
    uint64_t tick = 0;
    Drive(r, route, id, 1180, 200, tick); // jumps the start during the countdown
    CHECK(!r.Participant(id).started);
    CHECK(r.Participant(id).laps_done == 0);
}

TEST_CASE("race: cutting across the infield earns no lap")
{
    TrackRoute route = Rect();
    RaceRules r(route, Config(1));
    const int id = r.AddParticipant({ 20, 0 });
    StartRace(r);
    uint64_t tick = 0;
    Drive(r, route, id, 1180, 120, tick); // over the line, up to s = 100
    // Straight across the middle, well off the road, to rejoin just before the line.
    for (float x = 5; x <= 195; x += 5)
    {
        r.Update(id, { x, 100 }, ++tick);
        r.Rank();
    }
    Drive(r, route, id, 900 + 200, 120, tick); // back on the road, crosses the line again
    CHECK(!r.Participant(id).finished);
    CHECK(r.Participant(id).laps_done == 0);
}

TEST_CASE("race: a reset/teleport forward earns no checkpoint")
{
    TrackRoute route = Rect();
    RaceRules r(route, Config(1));
    const int id = r.AddParticipant({ 20, 0 });
    StartRace(r);
    uint64_t tick = 0;
    Drive(r, route, id, 1180, 300, tick);           // to s = 280, next gate at 300
    CHECK(r.Participant(id).next_checkpoint == 1);
    r.Update(id, route.PointAt(340), ++tick);       // 60 m jump over the gate
    CHECK(r.Participant(id).next_checkpoint == 1);  // gate not credited
}

TEST_CASE("race: wrong way is flagged and a backwards line crossing is not a lap")
{
    TrackRoute route = Rect();
    RaceRules r(route, Config(1));
    const int id = r.AddParticipant({ 20, 0 });
    StartRace(r);
    uint64_t tick = 0;
    Drive(r, route, id, 1180, 60, tick); // over the line to s = 40
    for (float s = 40; s >= -40; s -= 5)  // reverse back across the line
    {
        r.Update(id, route.PointAt(s), ++tick);
        r.Rank();
    }
    CHECK(r.Participant(id).wrong_way);
    CHECK(r.Participant(id).laps_done == 0);
    Drive(r, route, id, 1160, 50, tick); // turn round and drive forward again
    CHECK(!r.Participant(id).wrong_way);
}

TEST_CASE("race: ranking uses laps, then gates, then distance, then finish time")
{
    TrackRoute route = Rect();
    RaceRules r(route, Config(1));
    const int a = r.AddParticipant({ 20, 0 });
    const int b = r.AddParticipant({ 30, 0 });
    StartRace(r);
    uint64_t tick = 0;
    Drive(r, route, a, 1180, 520, tick); // a reaches s = 500
    Drive(r, route, b, 1170, 330, tick); // b reaches s = 300
    CHECK(r.Participant(a).position == 1 && r.Participant(b).position == 2);
    Drive(r, route, b, 300, 1000, tick); // b finishes first
    Drive(r, route, a, 500, 800, tick);
    CHECK(r.Participant(b).finished && r.Participant(a).finished);
    CHECK(r.Participant(b).position == 1 && r.Participant(a).position == 2);
}

TEST_CASE("race: pause returns to the same phase, restart resets everyone")
{
    TrackRoute route = Rect();
    RaceRules r(route, Config(1));
    const int id = r.AddParticipant({ 20, 0 });
    StartRace(r);
    uint64_t tick = 0;
    Drive(r, route, id, 1180, 400, tick);
    r.SetPaused(true);
    CHECK(r.Phase() == RacePhase::Paused);
    r.Tick(10.0f);
    r.SetPaused(false);
    CHECK(r.Phase() == RacePhase::Racing);
    r.Restart();
    CHECK(r.Phase() == RacePhase::Grid);
    CHECK(!r.Participant(id).started && r.Participant(id).laps_done == 0);
}

TEST_CASE("race: invalid configurations are rejected")
{
    TrackRoute route = Rect();
    RaceConfig c = Config();
    c.checkpoints_s = { 0 };
    CHECK(!ValidateRaceConfig(route, c).empty());
    c.checkpoints_s = { 10, 300 };
    CHECK(!ValidateRaceConfig(route, c).empty());
    c.checkpoints_s = { 0, 600, 300 };
    CHECK(!ValidateRaceConfig(route, c).empty());
    c = Config();
    c.laps = 0;
    CHECK(!ValidateRaceConfig(route, c).empty());
}
