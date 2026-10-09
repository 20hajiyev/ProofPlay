# Product

<!-- impeccable:product-schema 1 -->

Scope: the ProofPlay race-coach layer (`proofplay/`, dashboard at `proofplay/dashboard/`). The game
itself (RIVAL LINE, a Blur-inspired offline PC combat racer) is the host product it analyses.
Facts below come from the owner's hackathon brief and this project's measured work (D-094, D-095);
items marked **[inferred]** were not confirmed in an interview because the owner works without
question rounds — revisit them when convenient.

## Platform

web

## Users

- **Primary: the player who just finished a race.** They sit at the same PC, the race results
  screen just closed, and they want to know why they lost time and what to change before the next
  race. Ranges from keyboard newcomers to fast players (a measured human was faster than the bots on
  laps 2–3 but lost 23 s in the first 820 m).
- **Hackathon judges (AI Gaming track).** They watch a 5-minute live demo and score Value 25 ·
  Prototype + AI 30 · Testing 20 · Feasibility 15 · Originality 10. They need to see the scenario
  running, the AI's measurable contribution, failures shown honestly, and costs.
- **[inferred] Competitive drivers comparing themselves** on the verified leaderboard.

## Product Purpose

ProofPlay records a race inside the game, replays it deterministically, and tells the player where
time went and what to change. Every corner fix it offers is re-simulated in the game's own physics
first, so the player gets proof instead of a guess. Success: the player can act on the top findings
in their next race, and judges see measured evidence that the advice works (and where it does not).

## Positioning

"Every AI coach can give advice. ProofPlay only gives advice it has proven." Coaching tools and LLM
coaches read statistics and guess; ProofPlay changes the inputs and re-runs the exact race (the game
is deterministic — replay is bit-identical). The same mechanism makes the leaderboard tamper-proof:
a submitted result must reproduce from its own inputs.

## Operating Context

- Analysis runs in stages after each race: results-screen stats (~1 s) → replay analysis with skill
  profile and time losses (~9 s) → proven fixes by parallel re-simulation (~25–70 s) → verification.
- Sessions arrive as files from the game (`proofplay/sessions/<driver>/*.json`); a local Python
  server (`proofplay/dashboard/server.py`) analyses them and serves the dashboard on localhost:8787.
- Used on the same PC as the game, typically right after a race, and on a laptop/projector during the
  hackathon demo. Must work without internet except web fonts.

## Capabilities and Constraints

- Findings types: proven fix (re-simulated), time lost vs the player's own best lap (with causes:
  hit, stopped, ran wide, wreck, start), inconsistent corner across laps, results-screen stats.
- Skill profile: Pace, Line, Combat, Defence, Consistency, Recovery (0–99) and tiers Bronze / Silver /
  Gold / Elite.
- Tracks: any `content/tracks/<ID>_*.json`; corners are found from geometry; map art rendered from the
  game scene by `tools/render_proofplay_assets.py`.
- Racing only today. Other genres need an adapter (save/load state, step, outcome) — future work, not
  a current claim.
- Explanations are deterministic templates over measured numbers; no LLM text in the product path.
- Language: English UI.

## Brand Commitments

- Name: **ProofPlay**; host game working title **RIVAL LINE**.
- Owner's visual direction (binding until changed): monochrome / restrained colour, modern and
  high-quality, not a plain vector drawing — real game imagery (track render, car renders) on the map.
- No real-world car brands anywhere (project rule D-091).

## Evidence on Hand

- Coach comparison (`proofplay/results/benchmark.md`): rule coach 37.5 % of tips work / 59 % make it
  worse; LLM coach (gpt-5-mini) 76.5 % / 21 %; ProofPlay 100 % by construction, 0 % worse, mean gain
  1.05 s.
- Tamper test (`proofplay/results/tamper.json`): 10/10 forged sessions rejected.
- Results-screen stats vs replay: 12/12 values identical across two races.
- Human vs bot input signal: inputs change on 2.4 % of steps for a human vs 91–95 % for bots (n=1).
- Car renders and track renders in `proofplay/dashboard/assets/`.
- Absent, must not be fabricated: user testimonials, user counts, a human pilot study (planned, not
  run), pricing, deployment.

## Product Principles

1. **Proof over plausibility.** Never present a fix that was not re-simulated; label unverified
   numbers as such.
2. **Say when there is nothing to fix.** "No proven fix" is a valid, shown result.
3. **Fast first answer, deeper answer next.** Show what is known within seconds; never block on the
   slow stage.
4. **The player's own best is the reference** when the AI ideal is not meaningful.
5. **Honest failures are part of the product** (judges score them).

## Accessibility & Inclusion

[inferred] WCAG AA contrast for text, full keyboard use of the findings list and map, reduced-motion
support. No product-specific requirement beyond that was stated.
