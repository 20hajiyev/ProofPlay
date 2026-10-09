# Bərpa qeydi — səs generatoru üstündən yazılıb (2026-09-27, 21:32)

Claude Code sessiyası (M2 AudioRuntime işi) bu qovluğun əvvəlki sahibinin fayllarını görmədən `tools/generate_audio.py` adlı öz generatorunu yazdı və işə saldı. Nəticədə aşağıdakılar **itdi** və bu diskdən geri qaytarıla bilmir:

- `tools/generate_audio.py`: "Procedural audio draft 001" generatoru. `README.md` onu təkrar istehsal üçün göstərir.
- `assets/audio/manifest_fragment.json`: ID-lər, hash-lər, loop marker-ləri, provenance, preview cue sheet.
- `assets/audio/validation_report.json`: dekod edilmiş ölçmələr.

**Toxunulmayıb** (19:06–19:11 vaxtlı, olduğu kimi): `engines/` (119), `powerups/` (40), `cues/` (16), `ambience/` (4), `music/` (4 stem, 64 bar, 120 s), `preview/`, `README.md`, `model_audio_bindings.json`.

Bərpa üçün: həmin workstream-in öz tarixçəsindən (agent/alət loqu) `generate_audio.py`-ni yenidən götürüb işə salmaq manifest və hesabatı yenidən yaradacaq; WAV-lar deterministikdirsə, hash-lər eyni çıxmalıdır.

Oyunun M2 səs sistemi indi ayrıca yaşayır: `tools/generate_game_audio.py` → `assets/audio/game/` (öz manifesti və hesabatı ilə). Bu qovluğun qalan hissəsinə yazmır.
