<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="proofplay/dashboard/assets/proofplay-logo-dark.svg">
    <img src="proofplay/dashboard/assets/proofplay-logo-light.svg" alt="ProofPlay" width="420">
  </picture>
</p>

<p align="center"><b>The race coach that only gives advice it has proven.</b></p>

ProofPlay records a race inside the game, replays it exactly, and tells the player where the
time went and what to change next race. Every corner fix it shows was first **re-simulated in
the game's own physics** with the player's inputs changed, so the advice comes with proof
instead of a guess. The same replay makes the leaderboard tamper-proof: a result only counts
if it reproduces from its own inputs.

Built for the AI Gaming track on top of **RIVAL LINE**, our original Blur-inspired offline
combat racer (C++20, a fork of Wicked Engine with Jolt physics).

## Why

AI coaches read statistics and guess. We measured how often that guess is right by testing
every tip in the game itself (14 races with planted mistakes,
[`proofplay/results/benchmark.md`](proofplay/results/benchmark.md)):

| Coach | Tips that save time | Tips that make it worse or crash | Mean gain |
|---|---|---|---|
| Rule-based coach | 37.5 % | 59 % | 0.31 s |
| LLM coach (gpt-5-mini) | 76.5 % | 21 % | 0.63 s |
| **ProofPlay** | **100 %** (31/31) | **0 %** | **1.05 s** |

ProofPlay is 100 % by construction: a fix that was not proven faster is never shown.

## How it works

1. **Record.** The game saves every physics step of the player's inputs plus the race setup
   (1-2 MB per race). No screen recording.
2. **Replay.** The game is deterministic, so the replay is bit-identical; an end-state hash
   proves it. The game's results-screen numbers match the replay 12/12.
3. **Analyse.** Skill profile (Pace, Line, Combat, Defence, Consistency, Recovery), where time
   was lost against the player's own best lap (and why: hit, stopped, ran wide, wreck, start),
   and corners that differ lap to lap.
4. **Prove.** For each corner, 11 human-sized changes (brake or lift 5-30 m earlier or later)
   are re-simulated in parallel worker processes. A fix is kept only if it gains time without
   a crash, and it is marked *robust* when its neighbouring distances also work, because no
   one hits a braking point to the metre.
5. **Plan.** Fixes interact: on one race three single fixes together were 0.69 s *slower*, on
   another all fourteen together crashed. ProofPlay re-simulates the best combinations on one
   lap and shows the plan that really works.
6. **Verify.** Forged sessions are rejected: 10/10 in
   [`proofplay/tests/tamper_test.py`](proofplay/tests/tamper_test.py).

First results appear a few seconds after the finish line; proven fixes follow in about a minute.

## Using it

Double-click `proofplay/ProofPlay.pyw` (Python 3.10+). The dashboard opens in the browser.
Open the game, press **Start session**, type your name and drive. Every race you finish is
filed under your name and analysed by itself. Races can also be uploaded from another PC
(upload button, drag and drop, or `proofplay/agent/uploader.py`).

A portable build for another computer: `python proofplay/package.py --zip`. Details, network
setup and security notes: [`docs/hackathon/PROOFPLAY_DEPLOY.md`](docs/hackathon/PROOFPLAY_DEPLOY.md).

## Building

Windows, Visual Studio 2022, CMake, Python 3.10+.

```bash
git clone --recursive https://github.com/20hajiyev/ProofPlay.git
cd ProofPlay
git -C engine/WickedEngine apply ../patches/0001-expose-vehicle-constraint.patch ../patches/0002-deterministic-body-creation.patch ../patches/0003-toon-crease-ink.patch ../patches/0004-small-rain-pool.patch
cmake -S . -B build/game -G "Visual Studio 17 2022"
cmake --build build/game --config Release --target racer_sandbox proofplay
python proofplay/dashboard/server.py
```

Then open http://localhost:8787. The game is `build/game/game/sandbox/Release/racer_sandbox.exe`.

## Repository

| Path | What |
|---|---|
| `proofplay/src` | Recorder, deterministic replay, analysis, what-if search, plan (C++) |
| `proofplay/dashboard` | Dashboard (`index.html`), server, plain-language explanations (`advice.py`) |
| `proofplay/bench`, `proofplay/tests`, `proofplay/results` | Coach benchmark, tamper test, measured results |
| `game/` | RIVAL LINE: rules, AI, combat, career (`core`), Wicked/Jolt glue (`runtime`), the game (`sandbox`), tests |
| `content/`, `assets/` | Tracks, vehicles, events; generated art and audio (Blender generators in `tools/`) |
| `engine/` | Wicked Engine submodule and our patches |
| `docs/` | Every design decision with its measurement (`DECISIONS*.md`), hackathon docs |

All cars and tracks are original designs; no real-world brands are used.

## Team

| Name | Role |
|---|---|
| Rufat Dostaliyev | Data & ML |
| Sanan Hajiyev | Full-stack developer |
| Farrukh Mammadli | Designer |
| Ravan Khanbabayev | IT Business Analyst |
| Omar Musazade | Product Manager |
