# Car roster and mod system plan (D-089)

Owner (2026-10-04): "increase the cars to 40–50, inspired by real cars and cars from other games, and make them not look alike; take the mod system from other games too."

## Principles

- **No real brand, name or logo** (project rule: real-brand cars stay only in `assets/local_only`). Each car takes a recognisable silhouette or era from a real or game car, under an original name.
- **The cars must not look alike.** Each one is a distinct mix of archetype (body form), nose, tail, lamps, grille and stance. The design table checks that no two designs share all of archetype + nose + lamps + tail.
- **Data-driven:** a car is a row in `tools/car_designs.py`. The body, cabin, lamps and mods are built by one generic builder. D01–D05 keep their hand-built functions.

## Archetypes (body forms)

hatch, coupe, fastback, sedan, wagon, pickup, suv, van (one-box), mid (mid-engine supercar), muscle, kei (micro), roadster (open), rally (sedan or hatch with flares).

## Classes and physics

| Class | Top speed | Role |
|---|---|---|
| D | 170 km/h | the region R01 field (as today) |
| C | 190 km/h | |
| B | 215 km/h | |
| A | 240 km/h | |

- The physics JSON comes from the design: mass from size, drivetrain, torque for the class top speed, and grip from archetype (supercar high, SUV/van low).
- Balance gate: within each class, the S01 lap spread stays under 6% (D-081 test, per class).
- The AI field races the player's class (like D-048 for local cars).

## Mod system (inspiration)

| Source | What we take |
|---|---|
| NFS Underground | body kit packages (front/rear bumper, skirts as one style); neon underglow; window tint; lamp tint; vinyls/liveries |
| Forza Horizon | performance tiers already exist (D-071); a PI class letter (D/C/B/A/S) from the PI; tuning sliders (gearing, downforce, brake bias, stiffness) |
| Midnight Club | stance: ride height and camber, wheel offset |
| Gran Turismo | paint finishes: gloss / metallic / matte / pearl |

## Tickets (in order, blockers first)

1. **Parametric generator and design table**, first batch of 10 cars. Body by archetype, lamps/grille/intake variety. Review renders.
2. **Game integration:** cars from the content folder (no hard-coded lists), cook every car in CMake, class-matched AI fields, menu paging.
3. **Physics generation and per-class balance tests.**
4. **Batches 2–4:** cars up to 45.
5. **Mods 1:** body kit packages, neon, window tint, rim colour (models + catalogue + garage).
6. **Mods 2:** PI class letter, tuning sliders, stance, paint finishes, liveries.
