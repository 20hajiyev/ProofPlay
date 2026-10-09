# Combat Racer (Gamee) — Claude Code Game Studios setup

Original, Blur-inspired offline PC combat racer. Master plan: `C:\Users\Sanan\Downloads\PLAN (1).md`
(Azerbaijani, milestones M0–M8). The owner writes in Azerbaijani; reply in Azerbaijani.

This repo uses the Claude Code Game Studios (CCGS) framework
(https://github.com/Donchitos/Claude-Code-Game-Studios, MIT, commit 7ed2c3e, installed
2026-09-29): 34 studio agents in `.claude/agents/`, workflow skills in `.claude/skills/`,
validation hooks in `.claude/hooks/`. The Godot/Unity/Unreal specialist agents were set aside
in `.tools/ccgs-unused-agents/` — this game runs on a Wicked Engine C++ fork.

## Technology Stack

- **Engine**: Wicked Engine 0.72.106, C++ fork in `engine/WickedEngine` (patches in `engine/patches`,
  known quirks in `docs/ENGINE_BASELINE.md`). Physics: Jolt (inside Wicked). Audio: miniaudio.
- **Language**: C++20
- **Build**: CMake + Visual Studio 2022, build dir `build/game`; tests via ctest
  (`racer_core_tests`, `racer_handling_tests`, `racer_audio_tests`, fixtures)
- **Asset pipeline**: Blender 4.5 (`.tools/blender-4.5.14-windows-x64`) generators in `tools/*.py`
  → GLB → `import_check --cook` → `.wiscene`
- **Version control**: Git; nothing is committed without the owner asking

## Project Structure (differs from the CCGS default)

- `game/core` — engine-independent rules (race, AI, combat, career, audio logic)
- `game/runtime` — Wicked/Jolt glue (physics stepping, vehicles, race session, tracks)
- `game/audio`, `game/sandbox` (the playable game), `game/tests`
- `content/` — data (vehicles, tracks, events, localisation); `assets/` — generated art/audio
- No real-brand cars in the project: the old local imports (`assets/local_only`) and the realistic
  model folders were moved out on the owner's request (D-091, backup in
  `Documents/Gamee_local_cars_backup_2026-10-04`); new cars are original designs (`tools/car_designs.py`)
- `docs/DECISIONS*.md` — every decision with its measurement; `design/`, `production/` — CCGS
  templates. Where a CCGS skill says `src/`, read `game/`.

## Working Rules (override the CCGS "ask before every write" protocol)

The owner's standing instruction is full autonomy: do not ask questions or hand the owner TODOs;
decide, record the decision (with measurements) in `docs/DECISIONS*.md`, then report.
`project.yaml` sets `modes.automation: autonomous` accordingly. Still ask first for anything
irreversible or outward-facing (downloads of new third-party code, deleting user data, commits,
pushes, purchases).

Every subsystem gets measured gates: RAM/VRAM leak soaks, frame-time budgets, determinism,
and tests. Verify UI changes with `racer_sandbox --menu-script=...` screenshots and gameplay with
`--autotest`/`--flow-cycles` before claiming they work.

## Coordination Rules

@.claude/docs/coordination-rules.md
