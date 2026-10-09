# Qərar və mənbə jurnalı

## D-001 — Mühərrik v0.72.106-ya SHA ilə kilidlənir (2026-09-27)

Tag `27c0df16…` commit-inə həll edilib, submodule kimi saxlanılır. Plan §2.2.

## D-002 — Blender-də avtomobil burnu +Y (2026-09-27, QƏBUL EDİLDİ)

**Ölçmə** (`tools/check_fixture_axes.py`, `assets/fixtures/*`): Wicked v0.72.106 glTF importeri
Blender `(x, y, z)` nöqtəsini Wicked `(x, z, y)` nöqtəsinə çevirir. Səbəb: Blender exporter Y-up edir,
sonra Wicked `FlipZAxis` (`Editor/ModelImporter_GLTF.cpp:333`) sağ-əlli sistemi sol-əlliyə keçirir.
Yuxarı ox, metr miqyası və təkər pivotu düzgündür, güzgü effekti yoxdur.

**Qərar:** Blender authoring müqaviləsi: metr, Z-up, **burun +Y, sol −X**. Nəticədə maşın Wicked-də
+Z-yə baxır və runtime-da heç bir əlavə dönmə lazım olmur. Rədd edilən variant: cooker-də 180° yaw
(hər import yolunda ayrıca düzəliş tələb edərdi).

**Tətbiq:** `generate_vehicles.py` həndəsəni köhnə kimi qurur və `face_plus_y()` ilə bir dəfə çevirir.
Bu zaman köhnə generatorda FL/FR (və RL/RR) adlarının əks tərəfdə olduğu aşkarlandı və düzəldildi.
D01–D04 yenidən generasiya edilib; `fixture_axes` testi hər maşının dörd təkərini yoxlayır.

## D-003 — Oyun nüvəsi mühərrikdən asılı olmayan kitabxanadır (2026-09-27)

`game/core` (`racer_core`) Wicked-i link etmir. Yarış saatı, qaydalar və döyüş məntiqi GPU olmadan test olunur.
Plan §2.3.

## D-004 — Resurs sızmaları test qapısıdır (2026-09-27)

İstifadəçi tələbi: RAM, CPU və GPU istifadəsində sızma olmamalıdır. Qapılar:

| Qapı | Nə ölçür | Hədd |
|---|---|---|
| `import_leak_D01` | 60 dəfə import + GPU kadr flush; proses private bytes və DX12 video yaddaşı | RAM ≤4 MiB, VRAM ≤1 MiB artım |
| Debug `racer_core_tests`, `audio_fixture` | MSVC CRT debug heap: hər test öz ayırdığını azad etməlidir | 0 bayt |
| `SimClock` | Bir kadrda maksimum 8 fizika addımı; pause vaxtı atılır | CPU "catch-up spiral" yoxdur |

Tapılan və düzəldilən problemlər:

1. **Wicked mesh suballokasiyası.** Mesh buferləri 256 MiB-lıq bloklardan ayrılır
   (`wiRenderer.cpp:2742`) və yalnız `wi::renderer::UpdateGPUSuballocator()` çağırılanda geri qaytarılır.
   Bunu çağırmayan kadr dövrəsi hər D01 importunda ~12 MiB itirir; 21-ci importda yeni 256 MiB blok
   (RAM + VRAM) yaranırdı. **Qayda: öz kadr dövrəmiz `SubmitCommandLists()`-dən sonra bunu çağırmalıdır**
   (stok `wiApplication.cpp:389` belə edir).
2. **Blender generatorunda orphan data.** Silinmiş obyektlərin mesh/material/işıqları yaddaşda qalırdı;
   `.blend` faylı hər maşında böyüyürdü (D04: 4.0 MB). `orphans_purge` sonrası hər maşın ~1.7 MB-dır.
3. **miniaudio böyük oxuma.** 0.11.25-də iki səs qoşulu olanda bir dəfəlik 9600 kadrlıq
   `ma_engine_read_pcm_frames` planlaşdırılmış səsi itirir. 480 kadrlıq (10 ms) oxumalarda start
   sample-dəqiqdir. **Qayda: offline render/bake alətləri engine-i cihaz period ölçüsündə oxuyur.**

## D-005 — Audio: miniaudio 0.11.25 kilidlənir; stem-lər pitch-siz (2026-09-27)

`third_party/miniaudio` submodule, commit `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d`.
Ölçülüb: `MA_SOUND_FLAG_NO_PITCH` olan səsin planlaşdırılmış starti dəqiq kadrda başlayır;
pitch-li səsin (mühərrik loop-ları) linear resampler səbəbindən sabit 1 kadr gecikməsi var.
Musiqi stem-ləri `NO_PITCH` ilə yaradılır; pitch-li səslər üçün 1 kadr (0.02 ms) əhəmiyyətsizdir və testdə qeyd olunub.
