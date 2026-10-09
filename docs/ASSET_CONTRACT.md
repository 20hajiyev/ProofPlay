# Asset production contract — revision 1

Project: original Blur-inspired offline PC combat racer. Windows, Wicked Engine fork, 1080p60 target; performance has not been measured.

## Current production gate
Build the golden Harbor slice before mass-producing 16 cars and 6 regions. Current workers own vehicles D01–D04, Harbor modular assets and Dock Loop, procedural audio, and shared vector UI. Everything requires Wicked import/gameplay/performance validation later. Never label generated imagery as a usable 3D model or procedural audio as a real engine recording.

## Technical rules
- Blender sources: metres, Z up, vehicle nose +Y, vehicle left -X (docs/DECISIONS.md D-002). Imports into Wicked facing +Z with no runtime correction. Export standard GLB.
- Car root on ground plane, wheel pivots centered; pivots named WHEEL_PIVOT_FL/FR/RL/RR (FL = front-left from the driver's seat). `fixture_axes` test verifies placement for every vehicle LOD0.
- Generators must purge orphan data between assets (no memory carry-over between cars/LODs).
- Car total LOD triangle ceilings: 60000 / 30000 / 12000 / 4000. Maximum 8 primary material groups.
- Editable source required. Each file must be attributable to a generator/source and reproducible.
- Collision proxies separate from visual geometry. No claim of Jolt collision cooking yet.
- Audio: 48000Hz WAV masters, mono positional effects, stereo music; explicit sample loop boundaries; synthetic provenance.
- UI: editable SVG. Ability recognition uses shape as well as color. Text remains editable/localizable.

## Manifest interoperability
Each worker writes manifest_fragment.json under its owned asset root. Prefer an object with `assets` array. Every record has asset_id, category, status, source_paths, files/runtime_paths, provenance, dependencies, validation. Project paths are relative and forward slashed. No absolute tool paths in runtime content. Technical validation is distinct from visual/listening review and engine integration.

## Ownership
- Vehicle worker: tools/generate_vehicles.py and assets/vehicles/**.
- Environment worker: tools/generate_environment.py and assets/environment/**, assets/tracks/**.
- Audio worker: tools/generate_audio.py and assets/audio/**.
- Integrator: remaining tools, docs, UI, global manifest, review site.

Statuses: specified, draft, built, validated, reviewed, integrated. `Integrated` requires target-engine verification. Keep failures explicit.
