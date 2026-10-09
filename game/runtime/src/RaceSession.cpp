#include "racer/runtime/RaceSession.h"

#include <algorithm>
#include "racer/Contact.h"

#include "wiPhysics.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace wi::scene;

namespace racer::runtime
{
    RaceSession::RaceSession(Scene& scene, RaceSetup setup)
        : scene_(scene), setup_(std::move(setup)), stepper_(scene)
    {
        rules_ = std::make_unique<RaceRules>(setup_.route, setup_.race);
        CombatTuning tuning = setup_.tuning;
        if (setup_.mode == EventMode::Destruction)
        {
            // A 90 s hunt on a 2.2 km loop passes only ~12 pickup rows; with the race default the
            // E06 target (7 hits) was out of reach even for a hunter that used everything (measured
            // 3 hits). Faster respawn and a wider pad keep the hunter armed; targets never pick up.
            tuning.pickup_respawn_s = 3.0f;
            tuning.pickup_radius = 4.0f; // 6 m changed nothing (D-088: same 21 hits in 5 runs)
        }
        combat_ = std::make_unique<CombatSystem>(setup_.route, tuning);
        for (const PickupSpec& p : setup_.pickups)
            combat_->AddPickupPad(p.position, p.ability);
        // Weather (D-083): every car's tyres get the same grip factor, and the AI plans its corner
        // and braking budget with it - otherwise it brakes for a dry road and slides off a wet one.
        const WeatherEffects weather = EffectsOf(setup_.weather);
        for (VehicleDefinition& c : setup_.cars)
            c = ApplyWeather(c, weather);
        puddles_ = MakePuddles(setup_.route, weather.wetness, 9173u); // fixed seed: the same puddles every race on a track

        for (size_t i = 0; i < setup_.grid.size() && i < setup_.cars.size(); ++i)
        {
            const GridSlot& g = setup_.grid[i];
            const VehicleDefinition& def = setup_.cars[i];
            cars_.push_back(std::make_unique<VehicleRuntime>(scene, def, g.ground, g.yaw_rad));
            rules_->AddParticipant({ g.ground.x, g.ground.z });
            combat_->AddCombatant(def.health);
            recovery_.emplace_back();
            if (int(i) == setup_.player_index)
            {
                drivers_.emplace_back();
                planners_.emplace_back();
                continue;
            }
            const AIDifficulty skill = i < setup_.car_difficulty.size() ? setup_.car_difficulty[i] : setup_.difficulty;
            AIParams drive = MakeAIParams(skill, uint32_t(i));
            drive.lane_offset_m = (i % 2 ? 2.5f : -2.5f);
            drive.lateral_accel *= weather.grip;
            drive.brake_decel *= weather.grip;
            drivers_.push_back(std::make_unique<AIDriver>(setup_.route, def, drive));
            AIPersonality persona = AIPersonality(i % 4);
            if (setup_.mode == EventMode::RivalDuel)
                persona = AIPersonality::Aggressor; // the region's rival fights for the win
            // The hunter is the player; in all-AI runs (tests, attract) the last grid slot hunts, so
            // the targets start ahead of it (from pole it outran them all and never saw one).
            const int hunter = setup_.player_index >= 0 ? setup_.player_index : int(setup_.grid.size() < setup_.cars.size() ? setup_.grid.size() : setup_.cars.size()) - 1;
            if (setup_.mode == EventMode::Destruction && int(i) != hunter)
            {
                // Targets drive the route but never attack: the event is the player's hunt.
                // Targets cruise (Easy cornering, 70% of the car's top speed) so the hunter - the
                // player - can close in: at race pace an equal car never caught one in 90 s (measured).
                drive.lane_offset_m = 0.0f;
                drive.lateral_accel = std::min(drive.lateral_accel, 6.0f);
                VehicleDefinition cruise = def;
                cruise.top_speed_target_kmh = def.top_speed_target_kmh * 0.7f;
                drivers_.back() = std::make_unique<AIDriver>(setup_.route, cruise, drive);
                combat_->SetCanPickup(int(i), false);
                planners_.emplace_back();
                continue;
            }
            AICombatParams cp = MakeAICombatParams(skill, persona, uint32_t(i));
            if (setup_.mode == EventMode::Destruction)
                cp.aggression = 1.0f; // a hunt: nothing to save items for
            planners_.push_back(std::make_unique<AICombatPlanner>(int(i), setup_.route, cp));
        }
        stepper_.Prepare();
        ready_ = ValidateRaceConfig(setup_.route, setup_.race).empty();
        for (auto& c : cars_)
            ready_ &= c->ApplyTuning();
        combat_cars_.resize(cars_.size());
        car_stats_.resize(cars_.size());
        RefreshCombatCars();
    }

    float RaceSession::RaceTime(size_t i) const
    {
        if (green_tick_ == 0)
            return 0.0f;
        const ParticipantState& p = rules_->Participant(int(i));
        const uint64_t end = p.finished ? p.finish_tick : tick_;
        return end > green_tick_ ? float(end - green_tick_) * PhysicsStepper::kStepSeconds : 0.0f;
    }

    float RaceSession::TimeLeft() const
    {
        if (setup_.time_limit_s <= 0.0f || green_tick_ == 0)
            return setup_.time_limit_s;
        return std::max(0.0f, setup_.time_limit_s - float(tick_ - green_tick_) * PhysicsStepper::kStepSeconds);
    }

    RaceSummary RaceSession::Summary(size_t i) const
    {
        const ParticipantState& p = rules_->Participant(int(i));
        RaceSummary s;
        s.position = p.position;
        if (setup_.mode == EventMode::Destruction)
        {
            // Rank by hits dealt (ties: fewer wrecks); only the hunter scores, so the player is 1st
            // or last. Finishing means surviving to the clock.
            s.position = 1;
            for (size_t j = 0; j < car_stats_.size(); ++j)
                if (j != i && (car_stats_[j].hits_dealt > car_stats_[i].hits_dealt ||
                               (car_stats_[j].hits_dealt == car_stats_[i].hits_dealt && car_stats_[j].wrecks < car_stats_[i].wrecks)))
                    ++s.position;
        }
        s.participants = int(cars_.size());
        s.time_s = RaceTime(i);
        s.finished = setup_.mode == EventMode::Destruction ? time_up_ : p.finished;
        s.uses = car_stats_[i].uses;
        s.hits_dealt = car_stats_[i].hits_dealt;
        s.wrecks = car_stats_[i].wrecks;
        s.damage_taken = car_stats_[i].damage_taken;
        return s;
    }

    RaceSession::~RaceSession() = default;

    void RaceSession::Start()
    {
        rules_->BeginGrid();
    }

    void RaceSession::RefreshCombatCars()
    {
        for (size_t i = 0; i < cars_.size(); ++i)
        {
            const VehicleTelemetry& t = cars_[i]->Telemetry();
            const ParticipantState& p = rules_->Participant(int(i));
            CombatCar& c = combat_cars_[i];
            c.position = { t.position[0], t.position[2] };
            c.velocity = { t.velocity[0], t.velocity[2] };
            c.heading_rad = t.heading_rad;
            c.route_s = p.s;
            c.race_progress = rules_->TotalProgress(p);
            c.mass_kg = cars_[i]->Definition().mass_kg;
            c.finished = p.finished;
        }
    }

    void RaceSession::Recover(size_t i, RecoveryReason reason)
    {
        // Plan 2.9: a safe anchor behind the car on the road, facing along the route. A raw
        // "last good position" is not enough: recorded just before a crash it already points at
        // the wall, and the car was sent into the same wall hundreds of times (E05 soak).
        const TrackRoute& route = setup_.route;
        const float s = rules_->Participant(int(i)).s - kRecoverBehind_m;
        const Vec2 p = route.PointAt(s), d = route.DirectionAt(s);
        const Vec2 right{ d.z, -d.x };
        float lateral = 0.0f;
        for (float candidate : { 0.0f, -3.5f, 3.5f })
        {
            // Pick the first lane with nobody within 6 m (plan: check approaching cars).
            const Vec2 q{ p.x + right.x * candidate, p.z + right.z * candidate };
            bool free = true;
            for (size_t j = 0; j < cars_.size() && free; ++j)
            {
                if (j == i)
                    continue;
                const VehicleTelemetry& o = cars_[j]->Telemetry();
                free = std::hypot(o.position[0] - q.x, o.position[2] - q.z) > 6.0f;
            }
            if (free)
            {
                lateral = candidate;
                break;
            }
        }
        RecoveryRequest r;
        r.reason = reason;
        r.position[0] = p.x + right.x * lateral;
        r.position[1] = cars_[i]->BodyOriginHeight() + route.ElevationAt(s); // on the deck when raised (D-059)
        r.position[2] = p.z + right.z * lateral;
        r.heading_rad = std::atan2(d.x, d.z);
        if (debug_log_recoveries_ && stats_.recoveries < 40)
        {
            const VehicleTelemetry& t = cars_[i]->Telemetry();
            std::printf("  RECOVER t=%6.1f car %2zu reason %d at (%.1f, %.1f) s=%.0f kmh %.1f -> (%.1f, %.1f) lap %d started %d\n", tick_ / 120.0f, i, int(reason),
                        t.position[0], t.position[2], rules_->Participant(int(i)).s, t.forward_speed_kmh, r.position[0], r.position[2],
                        rules_->Participant(int(i)).laps_done, rules_->Participant(int(i)).started);
        }
        const float ground_y = r.position[1] - cars_[i]->BodyOriginHeight() + 0.05f;
        cars_[i]->Teleport(XMFLOAT3(r.position[0], ground_y, r.position[2]), r.heading_rad);
        recovery_[i].Reset(r.position, r.heading_rad);
        if (i < impact_.size())
            impact_[i].samples = 0; // a teleport is no crash: restart the impact history (no dent, no damage)
        ++stats_.recoveries;
    }

    // Car-to-car bumpers (D-088): the chassis boxes are 86% of a car's length, so cars nose to tail
    // sank into each other. Where two full visual footprints overlap, the approaching speed along the
    // contact normal is cancelled (an inelastic bump, momentum kept) plus a gentle push apart that
    // grows with the depth. Car pairs only: the walls, and the AI's lines past them, are unchanged.
    void RaceSession::ApplyBumpers()
    {
        if (tick_ < 2)
            return; // telemetry is filled after the first physics step: until then every car reads (0, 0)
        for (size_t i = 0; i < cars_.size(); ++i)
            for (size_t j = i + 1; j < cars_.size(); ++j)
            {
                const VehicleTelemetry &ta = cars_[i]->Telemetry(), &tb = cars_[j]->Telemetry();
                const VehicleDefinition &da = setup_.cars[i], &db = setup_.cars[j];
                const float dx = tb.position[0] - ta.position[0], dz = tb.position[2] - ta.position[2];
                const float reach = (da.length_m + db.length_m) * 0.5f;
                if (dx * dx + dz * dz > reach * reach || std::fabs(tb.position[1] - ta.position[1]) > 1.2f)
                    continue; // far apart, or one car above the other (a jump)
                const Overlap o = FootprintOverlap({ { ta.position[0], ta.position[2] }, ta.heading_rad, da.length_m * 0.5f, da.width_m * 0.47f },
                                                   { { tb.position[0], tb.position[2] }, tb.heading_rad, db.length_m * 0.5f, db.width_m * 0.47f });
                if (!o.hit)
                    continue;
                if (o.depth > stats_.max_car_overlap_m && std::getenv("RACER_BUMPER_LOG"))
                    std::printf("  BUMP t=%.2f cars %zu-%zu depth %.2f n(%.2f,%.2f) pos (%.1f,%.1f)-(%.1f,%.1f) hdg %.2f %.2f\n", tick_ / 120.0f, i, j, o.depth,
                                o.normal.x, o.normal.z, ta.position[0], ta.position[2], tb.position[0], tb.position[2], ta.heading_rad, tb.heading_rad);
                stats_.max_car_overlap_m = std::max(stats_.max_car_overlap_m, o.depth);
                stats_.deep_overlap_steps += o.depth > 0.3f;
                if (setup_.disable_bumpers || std::getenv("RACER_NO_BUMPERS"))
                    continue; // measured, not acted on
                RigidBodyPhysicsComponent* ra = scene_.rigidbodies.GetComponent(cars_[i]->Body());
                RigidBodyPhysicsComponent* rb = scene_.rigidbodies.GetComponent(cars_[j]->Body());
                if (!ra || !rb)
                    continue;
                ++stats_.bumper_contacts;
                const float ma = da.mass_kg, mb = db.mass_kg, mu = ma * mb / (ma + mb);
                const float closing = (tb.velocity[0] - ta.velocity[0]) * o.normal.x + (tb.velocity[2] - ta.velocity[2]) * o.normal.z;
                const float push = std::min(o.depth * 6.0f, 1.5f); // target separating speed, m/s (0.6 left 4x the deep overlaps, same E06/E08)
                const float j_ = mu * std::max(0.0f, push - closing); // raise the separating speed to push, never add on top
                wi::physics::ApplyImpulse(*ra, XMFLOAT3(-o.normal.x * j_, 0, -o.normal.z * j_));
                wi::physics::ApplyImpulse(*rb, XMFLOAT3(o.normal.x * j_, 0, o.normal.z * j_));
            }
    }

    void RaceSession::ApplyCombatEffects(float dt)
    {
        // Knocks from last step's hits: horizontal delta-v plus a small lift, as an impulse.
        for (const CombatEvent& e : pending_knocks_)
        {
            RigidBodyPhysicsComponent* rb = scene_.rigidbodies.GetComponent(cars_[size_t(e.target)]->Body());
            if (rb == nullptr)
                continue;
            const float m = cars_[size_t(e.target)]->Definition().mass_kg;
            wi::physics::ApplyImpulse(*rb, XMFLOAT3(e.impulse_dir.x * e.delta_v * m, e.delta_v_up * m, e.impulse_dir.z * e.delta_v * m));
        }
        pending_knocks_.clear();

        // Surge boost / back-use brake as a longitudinal force for this step.
        for (size_t i = 0; i < cars_.size(); ++i)
        {
            const float a = combat_->BoostAccel(int(i)) + (i < catch_up_accel_.size() ? catch_up_accel_[i] : 0.0f);
            if (a == 0.0f)
                continue;
            RigidBodyPhysicsComponent* rb = scene_.rigidbodies.GetComponent(cars_[i]->Body());
            const VehicleTelemetry& t = cars_[i]->Telemetry();
            const float m = cars_[i]->Definition().mass_kg;
            float accel = a;
            if (a < 0.0f && t.forward_speed_kmh < 20.0f)
                accel = 0.0f; // the brake half never drives the car backwards
            wi::physics::ApplyForce(*rb, XMFLOAT3(std::sin(t.heading_rad) * accel * m, 0, std::cos(t.heading_rad) * accel * m));
        }
        (void)dt;
    }

    void RaceSession::Step(const VehicleCommand& player_drive, const CombatCommand& player_combat, float dt)
    {
        ++tick_;
        rules_->Tick(dt);
        if (!time_up_ && setup_.time_limit_s > 0.0f && green_tick_ != 0 && TimeLeft() <= 0.0f)
        {
            time_up_ = true;
            rules_->Abort(); // stops the race state machine; Summary() still reports the result
        }
        const bool locked = rules_->InputsLocked() || time_up_;
        if (!locked && green_tick_ == 0)
            green_tick_ = tick_ - 1;

        // 1-3. Inputs, AI planning and combat commands.
        player_commands_.Submit(player_drive);
        const VehicleCommand player_step = player_commands_.ForStep();
        std::vector<CombatCommand> combat_cmds(cars_.size());
        std::vector<VehicleCommand> drive(cars_.size());
        for (size_t i = 0; i < cars_.size(); ++i)
        {
            const ParticipantState& p = rules_->Participant(int(i));
            if (int(i) == setup_.player_index)
            {
                drive[i] = player_step;
                combat_cmds[i] = player_combat;
            }
            else
            {
                const VehicleTelemetry& t = cars_[i]->Telemetry();
                if (setup_.mode == EventMode::RivalDuel)
                {
                    // The regional rival fights, it does not just race (plan 2.12 duel / Blur bosses):
                    // more than 25 m ahead of the player it eases off (down to 82% pace at 70 m),
                    // behind it drives flat out, and more than 25 m behind (a crash, a hit) it gets a
                    // catch-up push - without it one wall hit ended the duel (L01 with its hump and
                    // jump: rival 12 s behind, 18% of the time within 50 m).
                    // Measured before pacing: median gap 68 m, 24% of the time within 50 m, 2 hits.
                    const int player = setup_.player_index >= 0 ? setup_.player_index : 0;
                    if (int(i) != player)
                    {
                        const float lead = rules_->TotalProgress(p) - rules_->TotalProgress(rules_->Participant(player));
                        drivers_[i]->SetPaceScale(lead <= 25.0f ? 1.0f : 1.0f - 0.18f * std::min(1.0f, (lead - 25.0f) / 45.0f));
                        // Catch-up (Blur-boss rubber band): the AI already drives at the car's limit,
                        // so a pace scale cannot help; a mild push on the straights (up to 2.5 m/s^2 from 25 m behind) can.
                        catch_up_accel_.resize(cars_.size(), 0.0f);
                        // Only on the throttle with the wheel near straight: pushing into a corner sent it into the wall.
                        const DriverInput& in = cars_[i]->LastInput();
                        const bool straight = in.forward > 0.9f && in.brake < 0.05f && std::fabs(in.steer) < 0.15f;
                        catch_up_accel_[i] = straight && lead < -25.0f && t.forward_speed_kmh > 30.0f ? 2.5f * std::min(1.0f, (-lead - 25.0f) / 75.0f) : 0.0f;
                    }
                }
                AIObservation o{ { t.position[0], t.position[2] }, t.heading_rad, t.forward_speed_kmh / 3.6f, p.s };
                drive[i] = drivers_[i]->Drive(o, dt);
                if (planners_[i])
                    combat_cmds[i] = planners_[i]->Think(*combat_, combat_cars_, dt);
            }
            if (locked || p.finished)
            {
                // Held on the handbrake: the foot brake at standstill means "reverse" in
                // DriveAssist, which rolled the whole grid backwards during the countdown.
                drive[i] = VehicleCommand{};
                drive[i].handbrake = 1.0f;
                drive[i].brake = p.finished && cars_[i]->Telemetry().forward_speed_kmh > 5.0f ? 0.3f : 0.0f;
                combat_cmds[i] = CombatCommand{};
            }
        }

        // 4. Assists, forces, impulses.
        for (size_t i = 0; i < cars_.size(); ++i)
        {
            if (!puddles_.empty())
            {
                // each wheel's contact patch from the body pose (heading 0 = +z, right = +x)
                const VehicleTelemetry& t = cars_[i]->Telemetry();
                const VehicleDefinition& d = setup_.cars[i];
                const float sh = std::sin(t.heading_rad), ch = std::cos(t.heading_rad);
                const float fx[4] = { -0.5f, 0.5f, -0.5f, 0.5f }, fz[4] = { 0.5f, 0.5f, -0.5f, -0.5f };
                for (int w = 0; w < 4; ++w)
                {
                    const float lx = fx[w] * d.track_m, lz = fz[w] * d.wheelbase_m;
                    cars_[i]->SetWheelGrip(w, PuddleGrip(puddles_, { t.position[0] + lx * ch + lz * sh, t.position[2] - lx * sh + lz * ch }));
                }
            }
            cars_[i]->PrePhysics(drive[i], dt);
        }
        ApplyBumpers();
        ApplyCombatEffects(dt);

        // 5-6. Physics.
        stepper_.Step();

        // 7. Rules, combat, recovery.
        for (size_t i = 0; i < cars_.size(); ++i)
        {
            const VehicleTelemetry& t = cars_[i]->PostPhysics(tick_);
            stats_.insane_steps += !IsTelemetrySane(t);
            rules_->Update(int(i), { t.position[0], t.position[2] }, tick_);
            // Crash damage (D-061): horizontal speed lost over the last 3 steps (25 ms). Braking
            // loses ~1 km/h in that time, a wall or car hit tens; one charge per 0.3 s per hit.
            impact_.resize(cars_.size());
            Impact& im = impact_[i];
            const float v = std::hypot(t.velocity[0], t.velocity[2]) * 3.6f;
            im.cooldown = std::max(0.0f, im.cooldown - dt);
            const int old = (im.head + 1) % 4;
            const float loss = im.speed[old] - v; // 3 steps ago vs now
            // Dents (D-085): the velocity change in the car's frame says where the hit landed, and its
            // size (not just the speed lost - a side swipe barely slows a car) how hard.
            const float dvx = t.velocity[0] - im.vx[old], dvz = t.velocity[2] - im.vz[old];
            const float dv = std::hypot(dvx, dvz) * 3.6f;
            im.speed[im.head] = v;
            im.vx[im.head] = t.velocity[0];
            im.vz[im.head] = t.velocity[2];
            im.head = (im.head + 1) % 4;
            im.dent_cooldown = std::max(0.0f, im.dent_cooldown - dt);
            dents_.resize(cars_.size());
            if (im.samples >= 4 && im.dent_cooldown <= 0.0f && dv > kDentFreeKmh)
            {
                const float sh = std::sin(t.heading_rad), ch = std::cos(t.heading_rad); // heading 0 = +z, right = +x
                AddDent(dents_[i], ZoneOfHit(dvx * ch - dvz * sh, dvx * sh + dvz * ch), dv);
                im.dent_cooldown = 0.3f;
            }
            if (!locked && im.cooldown <= 0.0f && im.samples >= 4 && loss > 0.0f)
                if (const float dmg = ImpactDamage(loss); dmg > 0.0f)
                {
                    combat_->QueueImpactDamage(int(i), dmg);
                    im.cooldown = 0.3f;
                }
            im.samples = std::min(4, im.samples + 1);
        }
        rules_->Rank();
        RefreshCombatCars();
        if (!locked)
            combat_->Step(tick_, combat_cars_, combat_cmds, dt);

        for (const CombatEvent& e : combat_->Events())
        {
            switch (e.type)
            {
            case CombatEventType::Knock: pending_knocks_.push_back(e); break;
            case CombatEventType::Use:
                ++stats_.uses;
                ++stats_.uses_by_ability[size_t(e.ability)];
                ++car_stats_[size_t(e.source)].uses;
                break;
            case CombatEventType::Damage:
                stats_.hits += e.source >= 0; // crash damage (source -1) is not a combat hit
                if (e.source >= 0 && e.source != e.target)
                    ++car_stats_[size_t(e.source)].hits_dealt;
                car_stats_[size_t(e.target)].damage_taken += e.amount;
                break;
            case CombatEventType::Blocked: ++stats_.blocks; break;
            case CombatEventType::Pickup: ++stats_.pickups; break;
            case CombatEventType::Wreck:
                ++stats_.wrecks;
                ++car_stats_[size_t(e.target)].wrecks;
                if (e.source >= 0 && e.source != e.target)
                    ++car_stats_[size_t(e.source)].wrecks_caused;
                Recover(size_t(e.target), RecoveryReason::Stuck);
                combat_->Respawn(e.target);
                break;
            default: break;
            }
        }

        for (size_t i = 0; i < cars_.size(); ++i)
        {
            const bool wants_progress = !locked && !rules_->Participant(int(i)).finished && (drive[i].throttle > 0.1f || drive[i].brake > 0.1f);
            const RecoveryRequest r = recovery_[i].Update(cars_[i]->Telemetry(), dt, wants_progress);
            if (r.reason != RecoveryReason::None)
            {
                stats_.recover_fell += r.reason == RecoveryReason::Fell;
                stats_.recover_upside += r.reason == RecoveryReason::UpsideDown;
                stats_.recover_stuck += r.reason == RecoveryReason::Stuck;
                Recover(i, r.reason);
            }
        }
    }
}
