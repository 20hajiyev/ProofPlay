# İstehsal statusu və əhatə

Bu repo əvvəl razılaşdırılmış oyunun asset istehsalı üçündür. İlk keyfiyyət qapısı: 4 D-sinif avtomobili, Harbor liman dəsti, Dock Loop trasının geometriyası, səkkiz qabiliyyətin UI sistemi və audio nümunələri.

Tam plan: 16 avtomobil; 6 məkan (Harbor, Metro, Ridge, Foundry, Coast, Airfield); hərəsində 2 tras; 48 karyera eventi. Qalan məzmun ilk paket oyun mühərrikində yoxlanmadan tamamlanmış sayılmır.

Mənbə faylları və generasiya skriptləri saxlanır. Prosedural modellər və sintez edilmiş səslər yaradılır; real avtomobil qeydi və ya son kommersiya keyfiyyəti iddia edilmir. Performance, Wicked import, AI yarış tamamlama və bədii/listening təsdiqi ayrıca mərhələlərdir.

## İstifadəçinin avtomobil seçimi — 2026-09-28

İlkin orijinal D01–D04 saxlanılır. Əlavə real avtomobil formalarına əsaslanan 8 prosedural model hazırlanır:

- Volkswagen Golf GTI Mk5; BMW M3 E46; Subaru Impreza WRX STI 2005; Ford Ranger Raptor 2019 — istifadəçinin seçimi.
- BMW M4 G82; Mercedes-AMG A45 S W177; Porsche 911 GT3 (992); Nissan Z RZ34 — istifadəçinin əlavə müasir modellər istəyinə əsasən seçilib.

Bu paket ilkin, sadələşdirilmiş 3D rekonstruksiyalardır. Hər modeldə redaktə edilə bilən Blender mənbəyi, 4 GLB LOD, collision proksisi, ölçü/material metadatası və real modeldən çəkilmiş render olmalıdır. Foto-real nəticə, CAD dəqiqliyi və tam avtomobil salonu hələ qəbul meyarını keçməyib. UV/PBR teksturalar, damage deformasiyası, salon detalları, fərdi fizika/audio sazlaması və oyun daxilində performans ayrıca qalır. Tam oyunun əvvəlki 16 avtomobil / 12 tras hədəfi bu ilkin paketlə tamamlanmış hesab edilmir.

Faktiki yaradılmış fayllar: `assets/catalog.json`. Struktur yoxlama: `assets/validation_summary.json`. İnteraktiv baxış: `review/index.html` (lokal HTTP serverlə). Sonrakı agent tapşırıqları: `docs/ASSET_AGENT_PROMPTS.md`.
