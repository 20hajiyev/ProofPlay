# Combat Tuning Proposal — 8 abilities (P01–P08)

Status: Proposed (2026-09-29). Target file: `content/combat/tuning.json` (`tuning` block, same field names as `racer::CombatTuning`).
Sources: plan §2.8 / §2.12, `Combat.h`, `Combat.cpp`, `AICombat.cpp`, `RaceSession.cpp`, `docs/DECISIONS_M2.md` D-021/D-039/D-040,
track pad lists `content/tracks/L01_container_run.json`, `S01_dock_loop.json`, vehicle health `content/vehicles/D01–D04.json`.
Pillars used as the yardstick: readable combat, every attack has a counter (plan §2.8 counter table), no dominant strategy.

---

## 1. Diagnosis

### 1.1 Measured facts and derived rates

| Event | Cars / laps / lap length | Duration | Pickups | Uses | Hits (Damage events) | Wrecks |
|---|---|---|---|---|---|---|
| S01 E05 | 20 / 3 / 2.2 km | not recorded | — | ~240 | — | — (18+/20 finish) |
| L01 E07 | 20 / 3 / 3.6 km | 435 s | 472 | 555 | 174 | 14 |
| L01 E08 duel | 2 / 2 / 3.6 km | 266 s | — | 28 | 4 | 0 |
| S01 E06 Destruction | hunter + 7 passive | 90 s | (respawn 3 s, radius 4 m override) | 15 (hunter) | 10 | 1 |

Notes: `stats_.hits` counts every `Damage` event (`RaceSession.cpp:299`), so Pulse/Storm multi-target hits and self-hits (own Trap
after arming, own Storm) are included; `uses` counts every `Use` event, i.e. each Needle shot separately.

| Derived metric | E05 | E07 | E08 | E06 |
|---|---|---|---|---|
| Uses per car per lap | 4.0 | 9.25 | 7.0 | — |
| Uses per car-km | 1.82 | 2.57 | 1.94 | — |
| Field-wide uses per second | — | 1.28 | 0.11 | 0.17 |
| Hit rate = hits / uses | — | 0.31 | 0.14 | 0.67 |
| Hits per minute (per attacker) | — | 1.20 per car | 0.90 total | 6.7 |
| Wrecks per car per minute | — | 0.097 (14 / 145 car-min) | 0 | — |
| Hits per wreck | — | 12.4 | — | 10 |

### 1.2 Pad-mix model (L01, explains the "uses" number)

L01 has 57 pads in 19 rows of 3, pads 4.0 m apart across the road (row every ~190 m). Mix: Lance 9, Surge 10, Ward 8, Needle 8,
Mend 6, Trap 6, Pulse 6, Storm 4. If pickups follow the mix and every item is eventually used:

`expected_uses = pickups × (1 + (needle_shots − 1) × N_needle / N_pads) = 472 × (1 + 2 × 8/57) = 604`

Measured 555 → ~50 items still held at the finish (capacity 20 × 3 = 60; consistent with AI hoarding Ward/Mend, which it only
fires on threat / low health). Model consequences:
- **Needle ≈ 199 of ~555 uses (~36 %)** while a full Needle clip is worth 24 damage — less than one Lance. Needle inflates the
  "uses" count far more than it moves health bars.
- Offensive uses (Lance, Pulse, Trap, Needle, Storm) ≈ 405 → offensive hit rate ≈ 174 / 405 ≈ **0.43**.
- Pickups per car per lap = 7.9 of 19 rows (41 %): one new item every ~18 s per car, one use every ~16 s per car.

### 1.3 Double pickup (geometry bug in tuning, not in code)

Pickup test is `|car − pad| ≤ pickup_radius` on the car centre (`Combat.cpp:364`), per pad, in one step. With pads 4.0 m apart:

`double_band = max(0, 2 × pickup_radius − pad_spacing)` per adjacent pad pair.

| Symbol | Type | Range | Description |
|---|---|---|---|
| pickup_radius | float | 1.5–4.0 m | `CombatTuning::pickup_radius` |
| pad_spacing | float | 4.0 m (L01, S01) | lateral distance between pads in a row |
| double_band | float | 0–4 m | lateral band where a car collects two pads in one pass |

Output bounded to [0, pad_spacing]. Example: radius 3.0 → band 2.0 m per pair, 4.0 m of the 14 m pickable span (29 %) takes two
items per pass. Radius 1.9 → band 0. This breaks the plan's "choose one of three visible pads" read and silently inflates
pickups/uses in every race event. (E06 overrides radius to 4.0 on purpose; unaffected by this proposal.)

### 1.4 Time-to-kill (hits of one ability to wreck a full-health car)

`TTK_hits = ceil(health / damage)` — health ∈ {90 D01, 95 D02, 100 D03, 120 D04}, damage = the ability field; output integer ≥ 1.

| Ability (field) | Damage | D01 90 | D02 95 | D03 100 | D04 120 |
|---|---|---|---|---|---|
| Lance (`lance_damage`) | 28 | 4 | 4 | 4 | 5 |
| Trap (`trap_damage`) | 24 | 4 | 4 | 5 | 5 |
| Pulse (`pulse_damage`) | 18 | 5 | 6 | 6 | 7 |
| Storm, per zone (`storm_damage`) | 15 | 6 | 7 | 7 | 8 |
| Needle, per shot (`needle_damage`) | 8 | 12 | 12 | 13 | 15 |

Example: 3 Lances = 84 → no D-class car dies to three clean Lance hits; `mend_heal` 35 erases 1.25 Lances. Combined with 12.4
hits per wreck, damage is **chip-heavy**: many small events, few decisive moments. That is the opposite of "readable".

### 1.5 Per-ability read

- **Lance** (homing, 28 dmg): the heavy hitter, but below the 3-hit breakpoint. Its **timed-Pulse counter is unreadable**:
  `W = (pulse_radius − (car_radius_m + lance_radius)) / lance_speed = (6 − 2.0) / 70 = 57 ms` (D-021 measured 0.45–0.51 s after
  launch, i.e. a ~60 ms window, ~3.4 frames at 60 fps). A counter no human can hit reliably fails the "every attack has a counter"
  contract. Target selection range 120 m (`lance_target_range`) is why the duel is sparse: with 2 cars the rival is often > 120 m
  away (E08: 4 hits in 266 s, hit rate 0.14).
- **Pulse** (6 m, 18 dmg, AI fires at ≥1 neighbour if aggression ≥ 0.5): fine as offence in packs; its problem is the counter window.
- **Trap** (24 dmg, 12 s life): est. ~50 traps per E07 → ~1.4 live traps on track on average (50 × 12 / 435). Not a clutter
  problem; no change.
- **Needle**: under-performs as damage, performs as counter (2 shots kill a Lance/Trap). Its 3 charges are the single largest
  source of use volume. Kept as-is (plan: "3 sürətli atış"); `needle_shots` 3→2 is the reserve lever (see §4).
- **Storm** (3 zones, warning 1.2 s, active 3.0 s, zones at leader +80/+160/+240 m): all zones expire at
  `warning + active = 4.2 s` after use. Leader arrival at zone k is `(first_ahead + k × spacing) / v`. At the L01 average
  27 m/s (98 km/h): zone 1 at 3.0 s (live), zone 2 at 5.9 s (expired), zone 3 at 8.9 s (expired). **Storm delivers ~1 of its
  3 zones** in normal play → under-performs its plan spec (3 × 15) and its catch-up role (plan §2.1 principle 5).
- **Ward / Mend / Surge**: no measured failure; Mend 35 vs typical health ~100 is a sensible ~1-heavy-hit undo. No change.

---

## 2. Target metrics

| Metric (source test) | Now | Target band | Player-experience rationale |
|---|---|---|---|
| Uses per car per lap, E07 | 9.25 | 6.5–8.0 | One decision every ~20 s per car keeps each attack legible in a 20-car field. |
| Field uses per second, E07 | 1.28 | 0.9–1.1 | Screen-wide event rate a player can parse with sound + shape cues (plan §2.8). |
| Total uses, E05 | ~240 | 170–215 | Same readability goal on the short loop (−10…−30 %). |
| Pickups, E07 | 472 | 350–420 | One pad per car per pass; pad choice becomes a real decision. |
| Hit rate hits/uses, E07 | 0.31 | 0.30–0.40 | Fewer but more meaningful attacks; hitting should feel earned, not random. |
| Hits per wreck, E07 | 12.4 | 8–11 | A wreck is the result of a readable sequence (≈3 heavies + chip), not attrition. |
| Wrecks per race, E07 | 14 | 10–16 | Wrecks stay common enough to matter but ≤ 0.8 per car so racing still decides. |
| Wrecks per car per minute, E07 | 0.097 | 0.07–0.11 | Same, time-normalised for other tracks. |
| Duel hits, E08 | 4 | 7–14 | A rival duel must contain visible combat both ways (~2 hits per minute). |
| Duel hit rate, E08 | 0.14 | ≥ 0.25 | Items in a 1v1 should rarely be wasted for lack of a target. |
| Destruction hunter hits, E06 | 10 | 9–13 (hard gate ≥ 7) | Plan objective (7 hits) stays reachable, not trivial. |
| Lance Pulse-counter window | 57 ms | ≥ 80 ms | Timed counters must be humanly hittable (≥ 5 frames). |
| Storm zones reachable at 27 m/s | 1 of 3 | ≥ 2 of 3 | Storm must do what the UI promises. |
| Guardrails E05/E07 | 18+/20, 0 recov. | ≥ 18/20, recoveries not up, 0 insane steps, ≤ 4 ms/frame | Tuning must not break racing or physics. |

---

## 3. Proposed changes

| Field | Current | Proposed | Δ | Reason |
|---|---|---|---|---|
| `pickup_radius` | 3.0 | 1.9 | −37 % | **Above ±30 %, deliberate.** Must be < half the 4.0 m pad spacing to remove double pickups (§1.3); 3 pads + 1.9 m still cover 11.8 m of the 16 m road. Largest volume lever that also restores pad choice. E06 keeps its 4.0 override. |
| `pickup_respawn_s` | 8.0 | 10.0 | +25 % | Throttles volume only where pads are contested (20-car packs); a 2-car duel passes each row ~once per 130 s and is unaffected. E06 keeps its 3 s override. |
| `lance_damage` | 28 | 34 | +21 % | 3-hit breakpoint: 3 × 34 = 102 wrecks D01/D02/D03, D04 (120) still needs 4 — the heavy car keeps its identity. Mend 35 ≈ exactly one Lance undone. Compensates wreck count for the volume cut. |
| `lance_speed` | 70 | 60 | −14 % | Pulse-counter window 57 → 83 ms (with new radius); AI/human warning for a homing Lance at 60 m goes 0.86 → 1.0 s. Reach check: 150 m closed in 2.5 s < `lance_life_s` 4.0. |
| `lance_target_range` | 120 | 150 | +25 % | Duel sparsity: rival is often out of the 120 m lock. In packs a target within 120 m almost always exists, so pack behaviour barely changes. AI locks at 0.8 × range = 120 m. |
| `lance_route_window` | 150 | 180 | +20 % | Must stay ≥ range (route distance ≥ straight-line distance) or the new range is capped. Keeps the "ahead on the racing line" check. |
| `pulse_radius` | 6.0 | 7.0 | +17 % | Widens the timed-Pulse counter vs Lance/Trap/Needle. Area +36 %. |
| `pulse_damage` | 18 | 15 | −17 % | Offsets the +36 % area: expected pack damage per Pulse ×1.36 × 0.83 ≈ +13 %, near-neutral. Pulse stays a counter/space tool, not a primary killer. |
| `storm_warning_s` | 1.2 | 1.5 | +25 % | See-decide-steer for a ~5 m lane change; 1.2 s is below comfortable. Zone 1 (80 m) is still live before a leader arrives at ≤ 53 m/s (192 km/h). |
| `storm_active_s` | 3.0 | 4.0 | +33 % | **Slightly above ±30 %.** Live window becomes [1.5, 5.5] s; required for zone 2 to exist at average speed (§1.5). |
| `storm_spacing_m` | 80 | 60 | −25 % | Zones at +80/+140/+200 m. Arrivals at 27 m/s: 3.0 / 5.2 / 7.4 s → 2 of 3 live; at 40 m/s: 2.0 / 3.5 / 5.0 s → 3 of 3. |

Unchanged on purpose: `needle_*` (plan identity; reserve lever `needle_shots` 3→2 = −12 % uses if §2 volume band is missed),
`trap_*` (no clutter, fixed `trap_arm_s` 0.35 by plan), `ward_*`, `mend_heal`, `surge_*`, `heavy_chain_*`, `respawn_protect_s`
(plan 2 s), `owner_grace_s`, `car_radius_m`, `reference_mass_kg`, `lance_turn_rate` (turn radius already tightens 39 → 35 m at
27 m/s via the slower Lance; change one homing dial at a time), `storm_damage`, `storm_first_ahead_m`, `storm_cover_fraction`
(6.4 m safe band ≥ D-021's 5 m rule).

### 3.1 Key formulas used

**Pulse counter window vs Lance**

`W = (pulse_radius − (car_radius_m + lance_radius)) / v_close`, with `v_close ≈ lance_speed` for a target ahead at the shooter's speed
(the Lance inherits the shooter's forward speed, `Combat.cpp:211`).

| Symbol | Type | Range | Description |
|---|---|---|---|
| pulse_radius | float | 5–8 m | Pulse clear radius |
| car_radius_m + lance_radius | float | 2.0 m | Lance impact distance |
| v_close | float | 50–80 m/s | Lance closing speed on its target |
| W | float | 0–0.12 s | time during which Pulse clears the Lance before impact |

Bounded (≥ 0; 0 if radius ≤ 2.0). Example: now (6 − 2)/70 = 0.057 s; proposed (7 − 2)/60 = 0.083 s (+46 %).

**Storm zone reachability**

`live_k = [warning ≤ (first_ahead + k × spacing) / v_leader ≤ warning + active]`, k = 0…storm_zones−1.

| Symbol | Type | Range | Description |
|---|---|---|---|
| first_ahead, spacing | float | 40–120 m | `storm_first_ahead_m`, `storm_spacing_m` |
| warning, active | float | 1–5 s | `storm_warning_s`, `storm_active_s` |
| v_leader | float | 20–60 m/s | leader speed after the Storm is fired |
| live_k | bool | {0,1} | zone k is active when the leader reaches it |

Example at 27 m/s: now live = {1,0,0}; proposed live = {1,1,0}. Cars behind the leader arrive later, so they need the longer window too.

**Wreck-count sanity (expected direction, not a prediction)**

`wrecks' ≈ wrecks × (uses'/uses) × (hit_rate'/hit_rate) × (dmg_per_hit'/dmg_per_hit)`.
Example: 14 × 0.80 × 1.05 × 1.12 ≈ 13 → inside the 10–16 band. The 1.12 comes from Lance +21 % on ~25 % of damage events plus Storm
zones 1 → ~2, minus Pulse −17 %; it is an estimate that §4 must confirm.

---

## 4. Verification plan

Apply in three stages so each effect is attributable; run each stage with the existing tests in `game/tests/HandlingTests.cpp`.
Runs are deterministic per build (determinism test), so run each event on 3 grid/AI-seed variants when available and use the median;
treat differences < 10 % as noise.

Before stage A (instrumentation, test output only): print `stats_.uses_by_ability` (already collected in `RaceSession`) and
`stats_.blocks` in the E07/E08 INFO lines; add `hits_by_ability` if cheap. Without per-ability data the §1.2 model stays a model.

| Stage | Fields | Test (TEST_CASE) | Must move | Must not move |
|---|---|---|---|---|
| A: volume | `pickup_radius`, `pickup_respawn_s` | "R01_E07 - 20 cars, 3 laps, full combat" | pickups 472 → 350–420; uses 555 → 390–445 | finished ≥ 18; recoveries 0; wrecks not below 8 |
| A | same | "R01_E05 - 20 cars, 3 laps, full combat" | uses ~240 → 170–215 | finished ≥ 18; recoveries ≤ previous |
| A | same | "Rival Duel (E08)" | uses within ±15 % of 28 (duel should be unaffected) | 2/2 finish |
| A | same | "Destruction (E06)" | identical (overrides) | hunter hits ≥ 7 |
| B: Lance/Pulse | `lance_*`, `pulse_*` | E08 | hits 4 → ≥ 7; hits/uses ≥ 0.25 | 2/2 finish; recoveries 0 |
| B | same | E07 | hits/wreck → 8–11; hits/uses 0.30–0.40; wrecks 10–16 | finished ≥ 18; recoveries 0 |
| B | same | E06 | hunter hits 9–13 | hits ≥ 7 (hard gate) |
| B | same | Combat unit tests (D-021 counters) | "timed Pulse vs Lance" window measured ≥ 0.08 s | all 18 counter tests pass unmodified in intent |
| C: Storm | `storm_*` | Storm unit test: leader at 27 m/s crosses zones | ≥ 2 of 3 zones deal damage; safe band still passes untouched | Ward still blocks Storm |
| C | same | E07 | wrecks stay 10–16; Storm share of Damage events rises | leader (P1 at lap 2) finishing position not systematically lost |

Accept the proposal when, after stage C, every "Must move" row is inside its band and every guardrail holds. If E07 uses remain
> 8.0 per car per lap after stage A, apply the reserve lever `needle_shots` 3 → 2 and re-run stage A only. If E07 wrecks drop below
10, revert `pulse_damage` to 16–18 before touching Lance. If E08 hits stay < 7, the limit is AI item routing (duel rival
hoarding Ward/Mend), not tuning — escalate to AI planner work instead of raising damage.

Open risks: (1) Lance with 180 m route window may home across corners on tight sections — watch for hits through walls in E07 logs.
(2) Human players dodge Storm by lane choice while AI drivers may not; Storm buff could over-punish AI leaders — check E07 leader
damage after stage C. (3) D-class top speed above ~190 km/h makes Storm zone 1 activate after the leader passes; if so raise
`storm_first_ahead_m` rather than shorten the warning.
