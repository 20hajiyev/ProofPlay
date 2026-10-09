# M2 — Oynanıla bilən nümunə: qərarlar və ölçmələr (davam edir)

Rəqəmlər bu kompüterdə ölçülüb: i5-13450HX, RTX 4050 Laptop, Release, 1600×900.

## D-019 — Yarış qaydaları (`racer_core/Race`)

Plan §2.9. Marşrut yer müstəvisində qapalı xətdir; "s" (xətt üzrə məsafə) qaydaların, sıralamanın və AI-nin yeganə irəliləyiş ölçüsüdür.

- Checkpoint-lər ciddi sıra ilə keçilir; checkpoint 0 start/finiş xəttidir. Grid xəttin arxasındadır, xətti ilk keçmək dövrə 1-i başladır.
- Addımda 25 m-dən böyük sıçrayış (teleport, reset) və yoldan kənar (yarım en + 4 m) hərəkət checkpoint qazandırmır.
- Sıralama: finiş (vaxta görə) → dövrələr → növbəti keçilməmiş checkpoint-lə məhdudlanmış məsafə. Kəsilmiş yol sıralamada xal vermir.
- Proyeksiya əvvəlki mövqenin ±60 m ətrafında axtarılır. Tras özünə yaxın keçəndə (hairpin) səhv hissəyə "tullanma" olmur (test: 12 m aralı paralel hissələr).
- Vəziyyətlər: Loading → Grid → Countdown (3 s) → Racing → Finished → Results, üstəgəl Paused/Restarting/Aborted. Countdown bitənə qədər input kilidlidir.
- 11 test: tam 3 dövrə, trası kəsmə, teleport, geri sürmə və "wrong way", sıralama, pauza/restart, səhv konfiqurasiya.

## D-020 — AI sürücü (`racer_core/AI`)

Plan §2.10. Sürət profili: əyrilikdən v = √(a_yan·R), sonra əyləc (geri) və sürətlənmə (irəli) keçidləri. Sükan: "pure pursuit" (qabaqdakı nöqtə = 6 m + 0.55·v); DriveAssist-in sürətə bağlı sükan limiti kompensasiya olunur.

Çətinlik yalnız reaksiya vaxtı (plan pəncərələri: asan 450–750, normal 300–500, çətin 180–350 ms), yan təcil marjası və əyləc ehtiyatı ilə verilir. Maşın eyni fizika obyektidir, gizli tork və ya tutuş yoxdur. Per-car variasiya deterministik hash-dir (global RNG yoxdur).

| Ölçmə | Nəticə |
|---|---|
| Kinematik modeldə sürət profili | döngə 13.9 m/s (nəzəri 14.1) |
| Zolaq ofseti +3 m | orta 3.02 m |
| 2 dövrə asan / çətin | 96.8 / 86.1 s |
| Real Jolt, 1 maşın, 2 dövrə (1348 m) | finiş, xətdən ən çox 3.25 m |
| Real Jolt, 20 maşın | 20/20 finiş, aşma yoxdur, 0.36 ms/addım |

## D-021 — Döyüş sistemi (`racer_core/Combat`)

Plan §2.8: 3 yuvalı inventar, 8 qabiliyyət, qarşılıqlı cavablar cədvəli. Parametrlər `CombatTuning`-dədir: planın başlanğıc rəqəmləri. JSON-a köçürmə M3 (content pipeline) işidir.

Mühərrikdən asılı deyil. Fizika təsirləri hadisə kimi çıxır: itələmə delta-v kimi, kütləyə görə miqyaslanır (ağır maşın az itələnir); boost isə uzununa təcil kimi. Runtime bunları Jolt-a impuls və qüvvə kimi tətbiq edir. Zərər hissəciklərdən deyil, oyun həcmindən hesablanır.

Planın müəyyən etmədiyi qərarlar:
- **Storm** liderin (dövrəni nəzərə alan irəliləyişə görə) qabağına düşür. Zolaqlar növbəli tərəflərdədir, hər zonada ≥5 m təhlükəsiz yol qalır. Səbəb: plan §2.1 prinsip 5, geridə qalanın qayıdışı.
- Eyni addımda **əvvəl oyunçunun öz hərəkəti** (məs. Mend), sonra toqquşmalar hesablanır.
- Dolu inventarla pickup pad-də qalır, başqası götürə bilər.
- Seçilmiş yuva boşdursa, istifadə ilk dolu yuvanı işlədir.
- Finiş etmiş maşın nə hücum edir, nə zərər alır. Respawn-dan sonra 2 s immun olur və hücum edə bilmir, müdafiə qabiliyyətləri işləyir.
- Arxaya atılan Lance hədəflənmir, düz uçur (plan). Hədəf konusu 35°, məsafə 120 m, həmçinin **marşrut üzrə qabaqda** olmalıdır; qonşu tras hissəsindəki maşın seçilmir.

Qarşılıqlı cavablar (hamısı testlidir):
- **Lance:** Ward, vaxtında Pulse (pəncərə ~0.45–0.51 s), arxaya atılmış Trap, 2 Needle.
- **Trap:** Ward, Pulse, 2 Needle.
- **Needle:** Ward, Pulse.
- **Storm:** Ward və ya təhlükəsiz zolaq.

18 test planın §5.3 siyahısını əhatə edir.

## D-022 — AI döyüş planlaşdırıcısı (`racer_core/AICombat`)

20 Hz-də qərar verir, reaksiya gecikməsi ilə icra edir. Yalnız müşahidə olunanı görür (maşınlar, mərmilər, tələlər, öz inventarı); başqasının inventarını oxumur. Prioritet: gələn təhlükəyə Ward/Pulse → az sağlamlıqda Mend → yaxın rəqibə Pulse → qabaqdakına Lance/Needle → arxadakına Trap/arxaya Lance → lider deyilsə Storm → düz yolda Surge. Plan §2.10-dakı 4 şəxsiyyət aqressiya və Mend həddi ilə fərqlənir.

## D-023 — `RaceSession`: plan §2.4 addım sırası

input → AI planlaşdırma → qabiliyyət əmrləri → assist-lər, qüvvələr, əvvəlki addımın itələmələri → Jolt addımı → qaydalar və sıralama → döyüş → məhv/bərpa.

Döyüşdə məhv olan maşın son təhlükəsiz nöqtəyə qaytarılır və 2 s qoruma alır.

İki xəta real 20 maşınlıq döyüş yarışında tapılıb düzəldilib:
1. **Geri sayımda grid geri sürüşürdü.** "Əyləcdə saxlamaq" `brake=1` idi; DriveAssist dayanmış maşında bunu geri vitesə çevirir. İndi maşınlar əl əyləci ilə saxlanılır.
2. **Bərpa dövrəsi.** Anchor maşın dayanıbsa da yazılırdı, ilişən maşın ilişdiyi yerə təkrar-təkrar qaytarılırdı (bir maşın 14 dəfə, yarışı 111 s-dən 265 s-ə uzadırdı). İndi anchor yalnız >10 km/saatda yazılır. Reqressiya testi var.

20 AI, 2 dövrə, 8 qabiliyyət, real fizika: **20/20 finiş 126 s-də**, 77 pickup, 74 istifadə, 31 zərbə, 0 ilişmə bərpası. Addım (fizika + AI + döyüş + qaydalar) orta 0.38 ms → 60 fps-də 0.76 ms/kadr (büdcə 4 ms). Simulyasiya deterministikdir: eyni qurulum iki dəfə bit-bit eyni nəticə verdi.

## D-024 — Oynanıla bilən yarış (`racer_sandbox`, default rejim)

Oyunçu + 11 AI (plan E01 ölçüsü), müvəqqəti tras. Harbor trasları hazır olana qədər sınaq meydançasında 1348 m-lik marşrut istifadə olunur, 150 m-dən bir 3 pickup ilə. Trasın kənarı dirəklərlə göstərilir: bir ortaq mesh-in instansiyaları, ayrı mesh yox.

Effektlər əvvəlcədən yaradılmış hovuzlardan göstərilir/gizlədilir; yarış ərzində entity yaradılıb silinmir. Qabiliyyətlər rəng **və forma** ilə fərqlənir (plan §2.14 əlçatanlıq). Storm xəbərdarlığı sarı, alçaq və yanıb-sönən; aktiv zona qırmızı və hündürdür.

60 s avtotest (12 maşın, döyüş aktiv): kadr orta 6.07 ms, p99 7.6 ms, 165 FPS; sağlamlıq pozuntusu 0.

İdarəetmə plan §2.14-ə uyğundur (W/S, A/D, Space, Shift/Ctrl, Q, 1–3, R, F saxlamaq, Esc). `--handling` M1 handling sandbox-unu açır.

## D-025 — S01 Dock Loop: tək məlumat mənbəyi, fizika runtime-da qurulur

`tools/generate_environment.py` Harbor kitini və Dock Loop-u yaradır, həmçinin oyun tərifini (`content/tracks/S01_dock_loop.json`) runtime koordinatlarında yazır: marşrut, checkpoint-lər, 20 yerlik grid, 11 sıra × 3 pickup.

Köhnə generatorda üç problem var idi və düzəldildi:
- baryerlər 34 m-dən bir, 3 m uzunluqda idi (boşluqlar) — indi 1468 hissə fasiləsizdir;
- yol ribbon-u ilə yer arasında 17 sm pillə var idi;
- oyun məlumatı Blender koordinatlarında idi.

Plan §3.5-ə uyğun olaraq fizika JSON-dan qurulur (`TrackBuilder`): y=0-da tək düz səth (tikişsiz) və yolun iki tərəfində ofset poliliniya üzrə 512 fasiləsiz divar qutusu. Onların üzü Blender baryerlərinin üzü ilə eyni yerdədir (yol kənarından 2.2 m). Blender səhnəsi yalnız vizualdır.

`TrackDefinition` validatoru (core, 4 test) yoxlayır:
- yol eni ≥8 m; seqmentlər 0.1–30 m; ayrı hissələr divarları ilə birlikdə üst-üstə düşmür;
- grid 20 yerdir, yolun üstündə, xəttin arxasında, marşruta baxır və üst-üstə düşmür;
- pickup-lar yolun üstündədir.

Dekor yol və baryerə minimum məsafə ilə yerləşdirilir (78 yerləşdi, 0 rədd).

Ölçmə: 2200 m, 14 m en. 1 AI maşın dövrəni 57–58 s-də vurur (orta ~137 km/saat), yol mərkəzindən ən çox 3.3 m kənara çıxır. Oyunda (12 maşın) 166 FPS, p99 8.35 ms.

`assets/tracks/dock_loop/road_collision.glb` və `track_geometry.json` generatorun köhnə versiyasının çıxışıdır (19:12), istifadə olunmur.

## D-026 — Real trasda ilişmə: AI geri çıxır, bərpa marşruta əsaslanır

İlk E05 ölçməsində 20 maşın 3 dövrədə 88–525 "ilişmə" bərpası verdi. Mexanizmi log göstərdi: zərbədən sonra burnu divara dirənmiş AI maşın heç vaxt geri vitesə keçmirdi. Bərpa onu toqquşmadan 1 s əvvəl yazılmış və artıq divara baxan anchor-a qaytarırdı; maşın yenidən divara dəyirdi və dövr təkrarlanırdı. Həmin nöqtədə tək maşın normal sürülürdü, divar həndəsəsi düzgün idi (ölçüldü).

Düzəlişlər:
- `AIDriver` qaz basılı halda 1 s tərpənməsə, 1.2 s güzgü sükanla geri gedir.
- `RaceSession`-da bərpa marşrut üzrə 12 m geriyə, marşrut istiqamətində qoyur və boş zolaq seçir (plan §2.9).

Nəticə: 4 ayrı E05 işə salmasında 20/20 finiş, 0–1 ilişmə.

## D-027 — Determinizm: engine patch P-002

Eyni 20 maşınlıq yarış 25 s sonra 120 dəyərdən 65-də fərqlənirdi (23 m-ə qədər). Səbəb: Wicked body-ləri paralel job-larda yaradır, Jolt isə BodyID-ləri yaranma sırasına görə verir və həll sırası buna bağlıdır. P-002 yaradılışı komponent sırası ilə tək job-da edir. Nəticə: 0/120 fərq, iki ayrı prosesdə də eyni (plan §2.16 replay).

## D-028 — D sinfi: 4 maşın, ölçülən rollar

`content/vehicles/D01–D04.json` (plan §2.7): grip (FWD I4), drift (RWD I4 turbo), balanslı (AWD V6), ağır (RWD V8 pickup). Hər maşının öz ölçüsü, təkər bazası, COM, asqısı, sükan əyrisi, ötürücüləri, tork əyrisi, mühərrik ətaləti və əl əyləci parametrləri var. Yalnız maksimal sürəti dəyişmək fərqlilik sayılmır, ona görə rollar testlə ölçülür:

| Maşın | 0-100 | Top | 100-0 | Skidpad | Əl əyləci | S01 dövrəsi (AI) |
|---|---|---|---|---|---|---|
| D01 grip | 8.27 s | 173.5 | 42.5 m | **1.25 g** | 50.2° | 62.6 s |
| D02 drift | 8.02 s | 173.0 | 40.4 m | 1.06 g | **53.4°** | 62.1 s |
| D03 balanslı | 8.16 s | 172.4 | 47.9 m | 1.17 g | 43.3° | 62.4 s |
| D04 ağır | 7.92 s | 171.8 | 43.7 m | 1.09 g | 42.3° | 61.7 s |

S01-də dövrə fərqi 1.5%-dir (test həddi 6%). İlk tuning-də D02 və D03 0-100-ü 6.7 s ilə D sinfi üçün çox sürətli idi, tork azaldıldı. Grid bu siyahını növbə ilə istifadə edir; oyunçu `--car=` və ya menyudan seçir.

## D-029 — Karyera, yaddaş, eventlər, lokalizasiya (`racer_core`)

- **Karyera (plan §2.12):**
  - Event ID-ləri `Rnn_Enn` formatındadır; E01–E08 rejim düzülüşü plan cədvəlinə uyğundur (48 event: 24 döyüş, 6 time trial, 6 checkpoint, 6 dağıntı, 6 duel).
  - Medallar: yer üzrə 3/2/1, vaxt üzrə 1.05T / 1.15T / 1.30T, üstəgəl 2 əlavə məqsəd; event başına maksimum 5.
  - Duel 14 medal və 3 ustalıq tapşırığından 2-si ilə açılır; duel qələbəsi növbəti regionu açır.
  - Nəticə `result_id` ilə yalnız bir dəfə tətbiq olunur, yalnız rekord artımı mükafat verir; tərk edilmiş yarış sayılmır.
- **Yaddaş (§2.16):** sxem v1 + miqrasiya dövrəsi; gələcək versiya rədd edilir. FNV-1a checksum bit dəyişikliyini də tutur. Yazma: `.tmp` → diskdən oxuyub yoxlama → sağlam köhnə faylı `.bak`-a → atomik `rename`. Yükləmə zədəli fayldan `.bak`-a düşür. Zədəli fayl və backup olmadıqda oyun təzə profillə açılır, amma **o faylın üstünə yazmır**. UTF-8 yollar (Azərbaycan hərfli qovluq) testlidir. Parametrlər ayrı fayldadır.
- **Eventlər:** `content/events/R01.json`. Hazırda 3/8 event oynanıla biləndir (E01, E03, E05). Qalanları səbəbi göstərilməklə bağlıdır (L01 trası, Destruction rejimi). E01 plana uyğun olaraq yalnız Surge/Ward/Mend verir; pickup-lar yerində qalır, qabiliyyətləri icazəlilərə dəyişdirilir.
- **E03 referens vaxtı T = 113.5 s.** Hard AI-nin ölçülmüş solo vaxtıdır, oynanış ilə doğrulanana qədər müvəqqətidir. Test JSON-u ölçmədən ±3%-lə bağlayır; hazır olmayan event üçün referens yazılmır.
- **Lokalizasiya (§2.14):** `content/text/az.json` və `en.json`. Test hər iki dilin eyni açarlara malik olduğunu və eventlərin istinad etdiyi hər açarın mövcudluğunu yoxlayır; çatışmayan açar ekranda açar adı kimi görünür, çökmə olmur.

## D-030 — Oyun axını: menyu → yarış → nəticə → yaddaş

`racer_sandbox` default olaraq menyunu açır: R01 eventləri, medallar, səbəbli kilidlər, maşın seçimi, AZ/EN. Yarış event qaydaları ilə qurulur (iştirakçı, dövrə, çətinlik, icazəli qabiliyyətlər). Finişdən sonra Enter nəticəni tətbiq edib `%LOCALAPPDATA%\Racer\career.json`-a yazır. Pauzada Q yarışı tərk edir və heç nə yazılmır.

Hər `RacePath` öz `Scene`-inə sahibdir; yarış bitəndə path bir neçə kadr sonra məhv edilir və səhnə onunla birgə gedir. `--flow-cycles=K` bu dövrəni K dəfə avtomatik keçir və menyuda RAM/VRAM ölçür.

## D-031 — Menyu↔yarış RAM artımı: sızma yox, yuxarı hədd (high-water mark)

12 dövrəlik ölçü ~4 MiB/dövrə artım göstərirdi. Bölmə (`RACER_DEBUG_SKIP=t|c|tc` — yalnız diaqnostika, pist/maşın vizualını buraxır, 8 dövrə):

| Rejim | RAM (MiB), dövrə 1→8 | Nəticə |
|---|---|---|
| vizualsız (`tc`) | 930 → 998 → 999 | düz |
| yalnız pist (`c`) | 924 → 1058 → 1051 | düz |
| yalnız maşın (`t`) | 935 → 1001 → 1041 | 5-ci dövrədən düz |
| tam, 16 dövrə | 948 → 1008 (7) → 1015 (16) | 10–16: 1013–1016 |

Avtotest hər dövrədə növbəti maşını seçir (D01..D04), ona görə hər maşın ilk dəfə yüklənəndə ayırıcının yuxarı həddi qalxır, sonra dayanır. VRAM 408 MiB-də sabitdir. **Qapı:** `--flow-cycles` hesabatı `ram_growth_last_quarter_mib` və `leak_gate` yazır. Ən azı 4×maşın sayı (16) dövrə lazımdır; 3-cü və 4-cü rübün RAM medianları arasındakı fərq ≤ 4 MiB olmalı (2 MiB/dövrəlik real sızma ~8 MiB fərq verir, tək sıçrayışlar medianı tərpətmir), **Yenilənmiş qapı (2026-09-29, 25 yerli maşınla 32 dövrə ölçüsündən):** menyuda private bytes GPU hovuzu ilə birlikdə 64 MiB pillələrlə tərpənir (VRAM 408/472/536/600, maksimum 600, 24-cü dövrədə yenə 408-ə düşür), `RAM − VRAM` isə 32 dövrə boyu 965–1008 MiB-də düzdür. Ona görə: CPU yaddaşı (`RAM − VRAM`) rüb medianları fərqi ≤ 4 MiB; VRAM medianı ən çox bir blok (≤ 64 MiB) dəyişə bilər — sızma blokları dayanmadan artırar, hovuz isə məhdud qalır. Qeyd olunmuş üç qaçışda: CPU +0 / +3 / +2.5, VRAM +32 / +32 / 0 → PASS. Əks halda çıxış kodu 3. İlk variant (8 dövrə, orta nöqtə ilə son dövrə) yüksəlmə hissəsinə düşürdü və yalançı FAIL verirdi.

## D-032 — AudioRuntime (`racer_audio`, miniaudio)

- Ayrı statik kitabxana `game/audio` (Wicked-dən asılı deyil), offline rejimdə (cihazsız) test olunur. Cihaz və ya asset yoxdursa `Init` false qaytarır, oyun səssiz davam edir.
- **Mühərrik:** hər maşın `audio_family`-yə görə 10 loop (5 RPM ankeri × on/off yük), `EngineSoundModel` qarışdırır, qaz `ParameterSmoother` ilə yumşaldılır. Gain dəyişiklikləri 15 ms fade ilə (zipper səsi yox), layer sıfıra enəndən sonra dayanır (klik yox). Rəqiblər 3D (inverse zəifləmə, Doppler 0.5).
- **Oyun testi (2026-09-28, istifadəçi):** "səs çoxdur, qarışır, musiqi qulağı ağrıdır". Ölçü: tam miksin piki 1.88 (clip), limiter daim sıxırdı. Düzəliş: bus 0.5 + master peak limiter (−0.45 dBFS, `PeakLimiter`, racer_core); yalnız **ən yaxın 4 rəqibin** mühərriki (≤70 m) eşidilir; oyunçuya aid olmayan döyüş səsləri 0.5; musiqi yenidən yazıldı — sinus bas, 4–8 kHz sakit hi-hat, metal perkussiya yox, −12 dBFS pik, runtime-da əlavə 0.6 trim. Nəticə: pik 0.67 (limiter boşda), RMS 0.31 → 0.12.
- **Səslər:** 24 dünya one-shot (ən köhnəsi oğurlanır) + 8 kritik 2D (geri sayım, xəbərdarlıq, UI — dünya səsləri onları əvəz edə bilməz) + 4 musiqi stem-i (bir start kadrında, sample-dəqiq sinxron). Statik limitlər cəmi ≤ 96 (VoiceBudget::kTotal).
- **Yaddaş:** dekodlanmış buferlər 16-bit (f32-nin yarısı). Musiqi stem-ləri yarışlar arasında yaddaşda qalır (hər yarışda ~23 MB yüklə/boşalt heap-i parçalayırdı).
- **Generator:** loop seam xətaları düzəldi — mühərrik tezlikləri loop uzunluğuna tam dövrlə kvantlanır (`loop_freq`), musiqi notlarının quyruğu loop başına bükülür. 63/63 fayl yoxlamadan keçir.
- **Testlər (`racer_audio_tests`, 9):** 12 maşın + SFX + musiqi 20 s: pik ≤ 1, səslər ≤ 96, render Release-də ~1% real vaxt; uzaq mühərrik susur; pool oğurlama; stem-lər loop nöqtəsindən sonra sinxron; səs qrupları; 8× Init/Shutdown RAM artımı < 1 MiB.

## D-033 — Qrafika: Balanced render profili və Harbor materialları

İstifadəçi (2026-09-28): "qrafikaları düzəlt", "hər şeyi daha realistik et, Forza 6 kimi, açıq mənbə axtar". Başlanğıc: bütün səthlər düz rəng, tək rəngli göy, tonemap/AO/bloom yox, ekranda engine debug yazısı.

- **Render (plan "Balanced render"):** gradient göy + məsafə dumanı (realistic sky təsadüfi qaralırdı, ENGINE_BASELINE), bir kölgəli günəş (35°, 3 kaskad 12/60/300 m, 2048), MSAO, ölçülü bloom (threshold 1.4 — yalnız lampa, pickup halqası, stop işıqları), ACES tonemap, exposure 0.85, FXAA + yüngül sharpen; SSR, RT, dinamik GI, eye adaptation söndürülüb. Debug overlay yalnız `--debug-overlay` / `--handling` ilə.
- **Materiallar (plan 3.3 müqaviləsi):** albedo sRGB, normal OpenGL, ORM (R=AO, G=roughness, B=metallic). İki mənbə:
  - `tools/generate_harbor_materials.py` — prosedural, tileable (FFT səs-küyü), boya/beton/sinklənmiş polad/su; tikiş yoxlaması (kənar sıçrayışı ≤ 1.5× daxili maksimum).
  - Poly Haven CC0 skanları (istifadəçi endirməyə icazə verdi): asphalt_01, concrete_floor_02, container_side, rusty_metal_02 (2K), industrial_sunset_02_puresky HDRI. `assets/materials/polyhaven/LICENSE.md`. Konteyner skanı 5 boya rənginə tint edilir (`tools/prepare_scanned_materials.py`).
  - HDRI-də günəş 2.2° (gün batımı): gündüz yarışında yol baryer kölgəsində qalardı — gələcək "gün batımı" variantı üçün saxlanılır, indi realistic sky.
- **Pist:** bütün obyektlər dünya ölçüsündə UV (texel sıxlığı sabit), yol lenti uzunluq boyunca UV (tikişsiz), qırıq orta xətt (tək mesh), 925 konteynerlik yard (5 rəng, 1–3 hündürlük, eyni boşluq qaydası). Konteynerin həndəsi qofresi silindi (normal xəritə verir; ~0.55M üçbucaq qənaət). Sıxlıq ilk variantda 2695 konteyner (~2.1M üçbucaq) idi — plan xəbərdarlıq həddinə görə ~1/5-ə endirildi.
- **Maşın boyası:** metallic 0.7 → dielektrik baza (metallic 0.15) + clearcoat 1.0 (KHR_materials_clearcoat); şüşə dielektrik. Əvvəlki boya əks etdirəcək mühit olmadığından qaranlıq görünürdü.
- **Cook:** `import_check --cook` indi teksturaları `.wiscene`-ə gömür (ENGINE_BASELINE).
- **Ölçü (60 s yarış, 12 maşın):** kadr orta 6.06 ms, p99 7.6 ms (165 Hz vsync); VRAM 672 MB (limit 3.2 GiB); RAM 1.36 GB sabit (limit 6 GiB).
- **Forza səviyyəsi haqqında dürüst qeyd:** keyfiyyətli CC0 maşın modelləri demək olar yoxdur (əksəriyyəti real marka — plan 233/691 qadağan edir — və ya hesab tələb edir). Maşın realizmi prosedural modellərin detallaşdırılması ilə artırılır.

## D-034 — Maşın gövdəsi: loft edilmiş tək səth (class-A yaxınlaşması)

Köhnə gövdə 6 nöqtəli kəsiklərdən qurulmuş "qutu" idi, şüşə və dirəklər ayrı düz panellər idi. İndi `tools/generate_vehicles.py` gövdəni 34 kəsikli tək səth kimi qurur (LOD0; 24/16/10 aşağı LOD-larda):

- Plan görünüşü superellipsdir (yumru burun/arxa); kəsik şablonu sill → ən geniş nöqtə → çiyin → pəncərə lövhəsi → dam kimi gedir, yuxarıya doğru daralır (tumblehome). Salon (greenhouse) eyni səthdən `cabin(y)` əyrisi ilə böyüyür, ona görə ön/arxa şüşələr gövdənin öz yamaclarıdır.
- Materiallar üz səviyyəsində: yan şüşə, ön və arxa şüşə, qara B-sütunu, qara alt hissə. Subdivision LOD0-da 2, LOD1-də 1 səviyyə.
- Təkər yuvaları EXACT boolean-la kəsilir; kəsicinin qara materialı yuvanın içini örtür, gövdədən işıq görünmür.
- Faralar, stop işıqları, radiator barmaqlığı, splitter və egzoz burun/arxa konturunun üstündədir (`nose_y`). Güzgülər ellipsoiddir, qapı tikişləri gövdə səthini izləyir.
- Üçbucaq sayı LOD0 ~43–45k (limit 60k), LOD1 ~16k (limit 30k), LOD2 ~9k (limit 12k), LOD3 ~3k (limit 4k). Təkər pivotları və ölçülər dəyişmir, fizika toxunulmur.

## D-035 — Real markalı maşınlar yalnız lokal şəxsi oyun üçün

İstifadəçi (2026-09-28): "real marka modelləri tap, keyfiyyətli olsun; yalnız lokalda özüm oynayacağam". Plan 233/691 (real marka yox) paylanan oyun üçün qüvvədədir; şəxsi lokal quraşdırma üçün istisna edilir.
- Modellər `assets/local_only/cars/`-da saxlanılır (`.gitignore`-da), heç vaxt commit/paylama olunmur; orijinal D01–D04 əsas oyun maşınları olaraq qalır.
- Mənbə: Sketchfab, yalnız müəllif tərəfindən hazırlanmış CC BY / CC BY-NC modellər (şəxsi, qeyri-kommersiya istifadə); oyundan çıxarılmış ("ripped") və NoDerivs lisenziyalılar seçilmir (təkərləri ayırmaq, LOD etmək törəmə işdir). Hər model üçün müəllif və lisenziya `assets/local_only/cars/CREDITS.md`-də yazılır.
- Sketchfab endirmə hesab tələb edir — hesab yaratmaq/giriş mənim edə bilmədiyim addımdır; faylları istifadəçi endirir, inteqrasiyanı (miqyas, təkər pivotları, LOD, collision, material) mən edirəm.
- İstifadəçi sonra oyundan çıxarılmış modellərə də icazə verdi ("eksperimentdir"); 26 model endirdi, 25-i inteqrasiya olundu (L24 RX-7: vitrin pozası, yararsız).
- **Pipeline:** `tools/local_cars_probe.py` → yan/üst render + təkər halqaları (burun istiqaməti gözlə təsdiqlənir, `cars.json` `yaw_deg`); `tools/local_cars_import.py` → studiya zibili (sfera, müstəvi, yerdə yazı) atılır, real uzunluğa miqyas, yerə toxunan böyük obyektlər loose hissələrə bölünür, təkər = yerə toxunan yuvarlaq obyekt (en ≥ 10 sm), yoxdursa vətər + şaquli kəsik; model ox ortasına mərkəzləşdirilir (fizika təkərləri ±wheelbase/2, ±track/2 qoyur); gövdə ≤90k, hər təkər ≤6k üçbucaq, tekstura ≤2048; tərif baza D maşınından (`base`), ölçülər ölçülmüş. Avtomatik ölçü çaşanda `cars.json`-da `track_m` / `wheel_radius_m` (L10, L16, L21, L03).
- **Yoxlama:** ölçülən wheelbase realla ±1–2% (Supra 2.536/2.55, 930 2.271/2.272, E30 2.584/2.562). Oyun: tərifi etibarsız yerli maşın siyahıdan düşür (yarışı sındırmır); rəqiblər yerli maşınlardan hər yarış fərqli dilimlə seçilir; livery saxlanılır (boya əvəzlənmir). 45 s yarış: 0 bərpa, 0 insane step, kadr 6.06 ms / p99 8.6 ms; VRAM ~3.0 GB (limit 3.2 GiB — ehtiyat azdır), RAM ~3.7 GB.

## D-036 — Yerli maşınların vizual keyfiyyəti: decimate yox, real vaxt əks olunma

İstifadəçi (2026-09-28): "bəzi maşınların görünüşü pisdir, Forza kimi et". `tools/local_cars_review.py` (HDRI işığında 3/4 render) göstərdi ki, səbəb bizim 90k-ya decimate idi: Blender decimate müəllifin hamar normallarını silir — boyada əzik, sınıq səth (SLS, GT3-lər, R34, Niva, 930, Impreza).
- Decimate yalnız 400k-dan ağır gövdəyə (2 model); qalanları orijinal həndəsə (100–350k).
- Material düzəlişləri `cars.json` `material_overrides` ilə (L07 Lada 2106: .mtl şinə nişan teksturası bağlayırdı → qara rezin; şüşə yarımşəffaf). OBJ teksturaları `maps\` qovluğuna kopyalandı.
- **Real vaxt əks olunma:** oyunçu maşınını izləyən environment probe (128 px, 10 Hz, 150 m, 60×20×60 m qutu) — boya və şüşədə göy, yol, konteynerlər, digər maşınlar görünür. 256 px / 250 m p99 13 ms verirdi → 128/150: kadr orta 6.19, p95 8.7, p99 10.7 ms.
- Tam poly sahə ilə VRAM ~2.5 GB, RAM ~3.6 GB (40 s yarış, 0 bərpa).
- Flow autotest yaddaşı yarış silindikdən 30 kadr sonra ölçür (GPU silinmələri gecikir; əvvəlki FAIL əvvəlki yarışı ölçürdü). Nəticə: menyuda RAM 1.25–1.43 GB, VRAM 376–536 MB (yarışda 3.6 / 2.5 GB) — hər dəfə boşalır. Qalan "FAIL" təsadüfi rəqib dəstlərinin küyü idi (±60 MB) → flow autotest-də rəqib dilimi dövrəyə bağlıdır (`field_offset`, 4 dövrədən bir təkrarlanır).
- **İkinci oyun yoxlaması (hər maşın oyunçu kimi, oyun şəkli):** (1) Countach siyahıdan düşürdü — ölçülən en güzgülərlə 2.4 m-i keçirdi → en ≤ 2.35 m (və ya `width_m`); (2) 911, Impreza, E30, Evo xrom güzgü kimi — Sketchfab boyası metallic=1 (bəzən teksturada), əks olunma probe-u altında xrom olur → `fix_materials`: ən böyük səthli 3 opaque materialdan boya olanlar (adında paint/body/coloured/exterior/waike… və ya sahəsi ≥15%, metallic sabit və ya tekstura ortalaması ≥0.5) → metallic 0.1 + clearcoat 1.0 (0.03); roughness-ə toxunulmur (qaldıranda Charger tutqun boz olurdu); (3) KHR transmission şüşə (Wicked render etmir, südlü ağ) → alfa 0.35 tünd şüşə.

## D-037 — Yerli maşınlar: yan görünüş yoxlaması, gövdə oturuşu, rəngsiz modellər

Arxa kamera təkər/gövdə problemlərini göstərmirdi; `RACER_DEBUG_CAM=side` (yalnız diaqnostika) ilə hər maşın yandan çəkildi.
- **Gövdə təkərlərin üstünə çökürdü** (Lada 2106/2103, 930, E30, 2109, Impreza): fizika şassini asqıda nominal `origin_height`-dan aşağı oturdur, model isə öz təkərləri üzərində dayanan vəziyyətdə çəkilib. Grid oturandan sonra (1 s) hər yerli maşının modeli fizika təkər mərkəzlərinə görə yerləşdirilir: `root_y = WheelCentreHeight() − wheel_radius` (`VehicleRuntime::WheelCentreHeight`). D maşınlarına toxunulmur.
- **Xrom qalıqları** (Countach gövdəsi, 911 qapıları, E30 yanları — ayrı materiallar): boya qaydası bütün ≥4% sahəli metal materiallara, adında door/hood/fender/bumper/roof/panel olanlara və lak qatı (coat > 0.5) olan hər şeyə şamil edildi; adında chrome/metal/rim/exhaust/brake/grill/mirror olanlar metal qalır.
- **Rəngsiz yarış maşınları** (GT-R GT3, 992 GT3 R — teksturası itmiş çıxarılmış modellər, bütün materiallar Blender default boz 0.8): `cars.json` `paint_rgb` ilə ən böyük default-boz material boya (lak ilə), qalan default-boz hissələr tünd qrafit; teksturalı decal-lar saxlanılır. Seçim: GT-R GT3 tünd qırmızı, 992 GT3 R Riviera mavisi.
- Nəticə: 25 maşının hamısı yandan və arxadan yoxlanıldı — rəngli, xromsuz, təkərlər yuvalarda.

## D-038 — Parametrlər ekranı və menyu səsləri; "hazır kodu götür" qaydası

- İstifadəçi təklifi (2026-09-29): mümkün olan yerdə hazır kodu (GitHub) götürüb uyğunlaşdırmaq. Qayda: hər yeni iş üçün əvvəl Wicked-in öz imkanları, sonra aktiv saxlanılan, icazəli lisenziyalı (MIT/BSD/zlib; GPL yox) kitabxanalar; yalnız uyğun olan yoxdursa özümüz yazırıq. Hər xarici kitabxana endirilməzdən əvvəl istifadəçidən icazə (ad, mənbə, lisenziya) alınır və burada qeyd olunur. Mövcud nümunələr: Jolt, miniaudio, nlohmann/json, Poly Haven CC0.
- Parametrlər ekranı üçün Wicked `wiGUI` (maus yönümlü) yox, mövcud klaviatura/pult mətn menyusu: menyuda `O` / pult Y → Ümumi səs, Musiqi, Effektlər (10% addımla, canlı eşidilir), Dil, Geri. Dəyərlər `Settings`-ə yazılır, oyun çıxışda `settings.json`-a saxlayır. AZ/EN mətnləri `content/text`.
- Menyu səsləri (`ui_move`, `ui_confirm`, `ui_back`): hərəkət, seçim, bağlı eventə "yox" səsi. Menyuda `AudioRuntime::Update` çağırılır ki, one-shot pool yarış olmadan da işləsin.

## D-039 — L01 Container Run və menyu autotesti

- **L01 (3.6 km, 16 m yol):** `tools/generate_environment.py` indi hər iki pisti bir `build_track(spec)` ilə qurur (S01 json-u bayt-bayt eyni qaldı). Mərkəz xətti 21 nəzarət nöqtəli qapalı Catmull-Rom → 400 bərabər nöqtə → 3600 m. Sektorlar: rıhtım düzü, sürətli döngə, **konteyner dəhlizi** (S-döngələr, baryerlərin arxasında iki hündürlüklü konteyner divarları — planın "fərqli sektor" tələbi), şimal hairpini, uzun şimal düzü. 57 pickup (19 sıra × 3), 20 yerli grid, 925+ konteyner.
- **Ölçü:** tək AI maşın 1 dövrə 132.9 s (ort. 98 km/h), yoldan heç vaxt çıxmadı (|lateral| ≤ 4.0 m, 16 m yolda), 0 bərpa. E07 (20 maşın, 3 dövrə, tam döyüş): 20/20 finiş, 0 bərpa, fizika 0.81 ms/kadr (büdcə 4.0). Oyunda 16 maşınla E02: kadr orta 7.3 ms.
- **Eventlər:** E02, E04, E07 açıldı. E04 istinad vaxtı ölçülmüş Hard-AI 138.9 s (test ±3% saxlayır). E08 artıq pistə görə yox, rejimə görə bağlıdır (`unavailable.mode_duel`).
- **Oyun:** trek eventin `track` sahəsindən `content/tracks/<id>_*.json` ilə tapılır; `--track=` autotest üçün; L01 cook addımı CMake-də.
- **Menyu autotesti:** istifadəçi "menyunu da test et". `MenuPath` girişi `Actions` qatından oxuyur; `--menu-script=ODRR…` (U/D/L/R/E/B/O/G) hər kadr bir hərəkət verir, sonra ekran şəkli və JSON hesabat (parametrlər, dil, seçim, başlayan/uğursuz yarışlar). Test rejimində `settings.json` yazılmır. Yoxlanıldı: parametrlər (səslər 10% addım, dil), əsas menyu (maşın seçimi yerli maşın adı ilə), E02 seçib start → L01 yüklənir, 0 uğursuz. Tapılan xəta: 8 event + məqsədlər olduqda maşın sətri açar köməkçisi ilə üst-üstə düşürdü → sətirlər sıxlaşdırıldı, maşın sətri sabit yerdə.

## D-040 — Destruction (E06) və Rival Duel (E08) rejimləri

Plan yalnız "oyunçu + 7 hədəf, 90 s" və "2 maşın, 2 dövrə" deyir; qalan qaydalar bizim qərarımızdır.
- **Destruction:** `RaceSetup.mode` + `time_limit_s` (90 s yaşıl işıqdan). Hədəflər döyüşmür (planner yoxdur), pickup götürmür, Easy döngə tutuşu və maşın sürətinin 70%-i ilə mərkəz xəttində sürür. Oyunçu grid-in sonundan başlayır. Pickup bərpası 3 s (yarışda 8), radius 4 m. Nəticə: yer = vurulan zərbələrə görə (bərabərlikdə az wreck), `finished` = saata qədər qalmaq. Wreck hadisəsi indi son zərbəni vuranı göstərir (`Combatant::last_attacker`), `WrecksCaused` hesablanır. HUD: `HITS n  TIME m:ss`, sonda "TIME UP" və xülasə.
- Ölçü/düzəliş zənciri (hər biri testlə ölçüldü): ovçu 0 zərbə (test planner-siz idi) → 3 (hücum qabiliyyəti süzgəci + aqressiya 1.0) → 2 (hədəflər yavaşladı, fərq yox) → **10 zərbə, 1 wreck** — əsl səbəb: ovçu pole-dan başlayıb hamını qabaqlayırdı, qarşısında hədəf yox idi. E06 məqsədi (7 zərbə) AI ovçu ilə əlçatandır.
- **Rival Duel:** rəqib Hard + Aggressor xarakteri. L01-də 2 dövrə: 2/2 finiş, 28 istifadə, 4 zərbə, 0 bərpa. Açılma plan qaydası ilə (E01–E07-dən 14 medal + 3 ustalıqdan 2): menyu bağlı duel üçün irəliləyişi göstərir ("Duel bağlıdır: 3/14 medal, 0/2 ustalıq tapşırığı").
- Ustalıq tapşırıqlarının (R01_M1..M3) özü hələ yoxdur — duel praktikada açıla bilmir; növbəti iş.

## D-041 — Claude Code Game Studios (CCGS) quraşdırıldı

İstifadəçi (2026-09-29): "github.com/Donchitos/Claude-Code-Game-Studios yüklə, quraşdır, işlət". MIT, 25.5k ulduz, commit 7ed2c3e; klon `.tools/claude-code-game-studios` (git-ignored).
- **Yoxlama əvvəl:** 12 hook-un hamısı oxundu — yerli bash skriptləri (commit/push/asset yoxlaması, sessiya jurnalı, Windows bildirişi), şəbəkə yox. `settings.json` `rm -rf`, `git push --force`, `reset --hard` və s. qadağan edir. Hook-lar Git Bash-də sınaqdan keçirildi (jq yoxdur — hook-lar onsuz işləyir).
- **Köçürüldü:** `.claude/` (34 agent, skill-lər, hook-lar, qaydalar, sənədlər), `design/`, `production/`, `docs/architecture`, `docs/registry` və CCGS bələdçiləri. Bizim `docs/DECISIONS*.md`-ə toxunulmadı.
- **Uyğunlaşdırma:** şablon yalnız Godot/Unity/Unreal tanıyır — 15 mühərrik-agent `.tools/ccgs-unused-agents/`-ə; `engine.name` boş, Wicked məlumatı `project.yaml` `gamee:` blokunda (sxem yoxlaması təmiz keçdi). Şablonun `CLAUDE.md`-si "hər yazıdan əvvəl icazə soruş" tələb edir — istifadəçinin daimi avtonomluq qaydasına ziddir; öz `CLAUDE.md`-miz yazıldı: CCGS strukturu + koordinasiya qaydaları, `modes.automation: autonomous`, geri qaytarıla bilməyən addımlar üçün hələ də soruşulur. Kod kökü `game/` (şablonda `src/`).
- Agentlər/skill-lər/hook-lar Claude Code sessiyası yenidən başlayanda yüklənir.

## D-042 — Ustalıq tapşırıqları (duel açarı)

Plan: "regionun üç ustalıq tapşırığından ikisi + 14 medal duel-i açır" — tapşırıqların məzmunu bizim. Blur-un bacarıq tapşırıqları kimi event-lərarası yığılır:
- **R01_M1 Döyüş ustası:** regionda cəmi 25 zərbə. **R01_M2 Sağ qal:** 3 döyüş yarışını sınmadan bitir (Destruction/zamanlı eventlər sayılmır). **R01_M3 Qızıl vaxt:** E03 və ya E04-də qızıl (1.05T).
- Məlumat `content/events/R01.json` `mastery` blokunda (növlər: `total_hits`, `clean_combat_finishes`, `gold_in_events`); parser `gold_in_events`-in yalnız regionun zamanlı eventlərini göstərməsini tələb edir. İrəliləmə `SaveProfile.mastery_progress`-də (köhnə save-lər onsuz oxunur); tamamlanan tapşırığın sayğacı dayanır; nəticə dublikatı və ya tərk edilmiş yarış saymır. Tapşırıq həmin nəticədə duel-i aça bilir (yoxlama tapşırıqlardan sonra).
- Oyun: `RaceResult` zərbə/sınma daşıyır; nəticə mesajında "Ustalıq tamamlandı: …"; menyu başlığında `Ustalıq: M1 12/25  M2 1/3  M3 OK`, bağlı duel üçün `0/14 medal, 0/2 ustalıq`.
- TDD: 6 yeni test (5 karyera, 1 məzmun) əvvəl yazıldı və qırmızı idi; core 105/105. Bir test gözləntisi (tamamlanmadan sonra sayğacın artması) yanlış idi, düzəldildi.

## D-043 — Döyüş parametrləri JSON-da; ölçülmüş balans turu

- **Data:** `content/combat/tuning.json` (52 sahə = `CombatTuning`-un hamısı; `ParseCombatTuning`, `game/core/src/CombatTuning.cpp`). Sərt: naməlum açar (yazı səhvi), ≤0, tam ədəd tələb olunan yerdə kəsr, `storm_cover_fraction ≥ 1`, `heavy_chain_scale > 1` → xəta, oyun yarışı açmır. Oyun və handling testləri eyni faylı oxuyur; Destruction override-ları onun üstündən. Test: faylın bütün sahələri doldurması yoxlanır.
- **Balans (CCGS `systems-designer` agenti təklif etdi, `design/balance/combat-tuning-proposal.md`; mən 3 mərhələdə tətbiq edib ölçdüm).** Handling testləri qabiliyyət üzrə istifadə bölgüsünü çap edir. Qəbul edilən dəyişikliklər: `pickup_radius` 3.0→1.9 (sırada iki pad götürməyin qarşısı), `lance_damage` 28→34, `lance_speed` 70→60, `pulse_radius` 6→7, `pulse_damage` 18→15, `lance_target_range` 120→150, `lance_route_window` 150→180, `storm_warning_s` 1.2→1.5, `storm_active_s` 3→4, `storm_spacing_m` 80→60, `needle_damage` 8→10 (agentin cədvəlində yox — Needle istifadələrin 37%-i, zərbə/wreck-i endirmək üçün mənim əlavəm). `pickup_respawn_s` 8-də qaldı (10 ilə E07 hədəfdən aşağı düşdü).
- **Balans turu iki gizli xəta tapdı** (hər biri əvvəl qırmızı testlə, sonra düzəliş): (1) Storm öz sahibini vururdu — lider Storm istifadə edəndə öz zonasına girirdi; indi sahib toxunulmazdır. (2) AI Surge qərarı yalnız "indi" ilə "60 m irəli" istiqamətini müqayisə edirdi — S-döngənin girişi və çıxışı eyni yönə baxır, AI dəhlizə boost edib 9.2 m kənara çıxırdı (L01 tək maşın testi `pickup_radius` dəyişəndə qırıldı, qrup-qrup geri qaytarma ilə tapıldı). İndi Surge-ün qət edəcəyi bütün məsafə (reaksiya + boost müddəti, ≥60 m) hər 10 m yoxlanır.
- **Ölçü (baza → son, düzəlişlərlə):** E07 istifadə 555→331, vurma nisbəti 0.31→0.40, wreck 14→12, zərbə/wreck 12.4→10.9, 20/20 finiş (1 "stuck" bərpa, əvvəl 0); E05 wreck 4→8, istifadə 216→242, 20/20; E06 ovçu 10→9 zərbə (≥7); L01 tək maşın |lateral| 4.0 m. ctest 6/6, core 109/109. Aralıq mərhələlər `build/sandbox_reports/balance/`.
- **Açıq:** E08 duel-də 15 istifadə, 2 zərbə — tuning bunu dəyişmədi (kilid məsafəsi artsa da). İki maşın yarışın çoxunda bir-birindən uzaqdır; bu, duel AI-sinin davranış məsələsidir (rəqib oyunçuya yaxın qalmalı/ötməyə çalışmalı), rəqəm məsələsi deyil — ayrıca iş.

## D-044 — Rival Duel: güzgü matç və döyüş məsafəsini saxlayan rəqib

Problem (D-043-dən açıq): E08-də 2 zərbə / 257 s. Ölçü əlavə edildi (maşınlar arası məsafənin paylanması, kim öndədir, hər maşının finiş vaxtı) və iki səbəb tapıldı:
1. **Maşın, AI deyil:** rəqib D02 (drift maşını) Hard AI ilə real fizikada 2 dövrədə D01-dən 17 s yavaş idi. Qərar: **duel güzgü matçdır** — rəqib oyunçunun maşınını sürür, nəticəni bacarıq həll edir.
2. **Rəqib yalnız yarışırdı:** öndə olan maşın sadəcə uzaqlaşırdı (median 68–408 m). `AIDriver::SetPaceScale` (0.75–1.0, TDD ilə) və duel qaydası: rəqib oyunçudan 30 m-dən çox öndədirsə tempini 100 m-də 85%-ə qədər azaldır, geridə tam sürətlə sürür. Oyunçu onu ötüb qazana bilər.
- `RaceSetup.car_difficulty` (maşın başına çətinlik; qarışıq sahələr üçün də): testdə oyunçunun yerini Normal AI tutur (insan nadirən Hard AI qədər sürətlidir), rəqib Hard.
- **Ölçü (əvvəl → sonra):** median məsafə 68 m → 42 m, 50 m daxilində vaxt 24% → 66%, zərbə 2 → 5, finiş fərqi 17 s → 1.3 s; rəqib vaxtın 90%-i öndə. Test qapıları: ≥50% vaxt 50 m daxilində, ≥4 zərbə, finiş fərqi <10 s. ctest 6/6, core 110/110.

## D-045 — AZ font örtüyü testi və klaviatura remap

- **Font:** oyun Wicked-in gömülü Liberation Sans-ını işlədir. `FontTests.cpp` fontu açıb (`stb_truetype`) `az.json` + `en.json`-dakı hər simvolun və Azərbaycan hərflərinin (ə Ə ğ Ğ ı İ ö Ö ş Ş ç Ç ü Ü) qlifinin olduğunu yoxlayır: 76 simvol, 0 çatışmayan. Testin özü yoxlanır: fontda olmayan U+4E2D 0 qaytarmalıdır.
- **Remap (plan 2.14):** `Settings.bindings` (10 hərəkət → düymə adı), `Rebind` — istifadədə olan düyməyə bağlama iki hərəkəti dəyişdirir, heç bir hərəkət düyməsiz qalmır; `settings.json`-da toqquşan/naməlum xəritə bütövlükdə default-a qayıdır (oyunu bloklamır). Yarış klaviatura girişini bu xəritədən oxuyur; ox düymələri başqa hərəkətə bağlanmayıbsa ikinci sürüş dəsti qalır; pult sabitdir. HUD köməkçisi bağlı düymələri göstərir; stick deadzone indi parametrdən gəlir.
- **Menyu:** Parametrlər → İdarəetmə: hərəkət seç, Enter, yeni düyməni bas (Esc ləğv edir). Autotest skriptində `k<düymə>` basışı simulyasiya edir; hesabat `bindings` xəritəsini yazır. Yoxlandı: qaz→I, əyləc→D (D sağa idi → sağa S aldı). Ekran şəkli ilə tərtibat düzəldildi (son sətir köməkçi ilə toqquşurdu).
- core 113/113, ctest 6/6.

## D-046 — Əlçatanlıq seçimləri

`Settings`-də sahələr vardı, amma oyuna heç biri təsir etmirdi. İndi Parametrlər ekranında və oyunda:
- **HUD yazı ölçüsü** 80–150% (10% addım): yarış HUD-unun bütün yazıları və aralıqları; düymə köməkçisi sığmayanda iki sətrə bölünür (150%-də ekrandan çıxırdı — ekran şəkli ilə tapıldı). Menyular sabit tərtibatdadır (144 dpi-də 600 məntiqi hündürlük artıq doludur) — miqyas yalnız HUD-a.
- **Yüksək kontrast:** HUD yazıları SDF ilə qalın qara kənar xətt (bolden 0.7, softness 0.05).
- **Hərəkət bulanıqlığı** aç/bağla (Wicked `setMotionBlurEnabled`, güc 0.6). Ölçü: 30 s yarış, kadr orta 6.08 vs 6.09 ms — xərci ölçü küyü daxilində.
- **Pult ölü zonası** 5–40% (5% addım) — sükan stiki.
- **Kamera silkələnməsinin azaldılması** göstərilmir: oyunda kamera silkələnməsi yoxdur, seçim heç nə etməzdi. Silkələnmə əlavə olunanda seçim də qoşulacaq (sahə saxlanılır).
- Test: menyu skripti (`ODDDDRRDRDRDR`) → hesabatda `ui_scale 1.2, high_contrast true, motion_blur false, stick_deadzone 0.20`; `--ui-scale`, `--high-contrast`, `--no-motion-blur` autotest bayraqları ilə HUD şəkilləri. ctest 6/6.

## D-047 — Qaraj müqayisə paneli

- Ayrı qaraj ekranı yox, menyunun sağında seçilmiş maşının paneli (maşın dəyişdikcə yenilənir): ad, sinif, ötürücü; fırlanma momenti, maksimum sürət, möhkəmlik, çəki — hər biri eyni qrupdakı ən yüksək dəyərə nisbətən 10 bölməli çubuqla. Qrup: D sinfi öz arasında, lokal real maşınlar öz arasında (D maşınını 700 Nm-lik superkarla müqayisə mənasızdır).
- Menyu skripti ilə D02 və lokal maşın (BMW M3 E30) seçilib ekran şəkli ilə yoxlandı; yeni mətnlər font testindən keçdi (77 simvol).
- **Aşkarlandı:** lokal maşınların fizikası D maşınlarının kopyasıdır (`cars.json` `base`), ona görə "BMW M3 E30: AWD, 205 Nm" (D03-ün dəyərləri) görünür. Növbəti iş: hər lokal maşına real-yaxın göstəricilər (ötürücü, çəki, moment, maksimum sürət) — `cars.json`-da məlumat, importer yazır.

## D-048 — Lokal real maşınlara öz fizikası; performansa görə rəqib seçimi

- `tools/local_cars_specs.py` (Blender lazım deyil, importdan sonra): 25 maşına ictimai göstəricilərə yaxın dəyərlər — ötürücü, çəki, moment, maksimum sürət, rpm limiti, mühərrik səs ailəsi (V12/flat-six ən yaxın ailəyə), şin tutuşu (klassiklər 0.85–0.95, superkarlar 1.1–1.15, GT3/DTM 1.3–1.35). Hesablanan: son ötürmə (top gear-də max rpm-in 97%-i = hədəf sürət), əyləc (çəki × 1.6, yarış maşınları × 2.2), möhkəmlik (60 + çəki × 0.035, 80–150). Şəxsi oyun üçün yaxınlaşmadır, sertifikatlı məlumat deyil.
- **Yoxlama (hər maşın oyunçu kimi, L01, 45 s):** 25/25 yükləndi, 0 bərpa, 0 insane step; ölçülən max sürət 129 (Lada 2103) … 253 km/h (Countach) — sıra real sıraya uyğundur; L01 düzləri tam sürətə imkan vermir.
- **Rəqib seçimi:** D maşını → D sinfi; lokal real maşın → maksimum sürəti ən yaxın lokal maşınlar (ən azı 4, sahənin yarısı qədər). Nümunə: Lada 2103 → Lada 2106/2109, Niva, Mustang 65, 190E; Agera → Aventador, SLS, GT-R, C8, Countach. Hesabatda `field` və `player_max_kmh`.
- Flow autotest (D maşınları) indi D sahəsi ilə yarışır — yerli maşınların yaddaş yükü artıq həmin testdə deyil (D-036 ölçüləri qüvvədədir).

## D-049 — Toon (komiks) üslubu: prototip T01

İstifadəçi (2026-09-29) istinad şəkli göstərdi: qalın konturlu, düz rəngli, mübaliğəli ölçülü stilizə maşın ("bu stildə özün düzəlt"). Fotorealizmdən (Forza) fərqli olaraq bu üslub tam bizim əlimizdədir — orijinal model, lisenziya məsələsi yox.
- **Wicked-in hazır imkanları** (yeni shader yazılmadı): material `SHADERTYPE_CARTOON` (pilləli işıq) + material `SetOutlineEnabled` və RenderPath `setOutlineEnabled` (qalınlıq 1.6, eşik 0.1, tünd kontur). Kontur yalnız istəyən materiallara çəkilir — digər maşınlar və mühit toxunulmur.
- **T01** (`tools/generate_toon_cars.py`, orijinal dizayn): hündür qutuvari gövdə, iri protektorlu şinlər (18 blok), sarı disklər, qırmızı zolaq/kapot/qanad, boz qanad genişləndiriciləri, dam barmaqlığı + yük qutusu, qabaq bufer, faset (düz) səthlər; 5.5k üz; D01 fizikası (vizual dəyişiklik). Menyuda seçilir; T oyunçusu D sahəsi ilə yarışır. Yan və arxa kamera ilə oyunda yoxlandı.
- Blender-də tapılan iki xəta düzəldildi: `transform_apply(scale=True)` mövqeyi də bişirirdi (protektor blokları dünya mərkəzi ətrafında fırlanırdı); təkər hissələri birləşdirilməzdən əvvəl yerləşmə bişirilməli idi.
- **Növbəti addım (tövsiyə):** üslub bütöv olmalıdır — toon maşın realistik liman fonunda yad görünür. Mühit (asfalt, beton, konteynerlər) üçün də cartoon shader + düz/az detallı materiallar və D02–D04-ün toon qarşılıqları (T02–T04, eyni rollar).

## D-050 — Bütün oyun toon üslubunda; gövdə animasiyası

İstifadəçi (2026-09-29): "hər şeyi belə animasiyada elə, animasiyalara çox fikir ver; maşını özəlləşdirmək də olacaq".
- **Üslub bütövdür:** `ToonifyMaterials()` (RacePath) yüklənən hər maşın və tras materialına `SHADERTYPE_CARTOON` verir; kontur hamısında açıqdır, **yer səthləri istisna** (adında Road/Quay/Water/Paint White/Charcoal) — böyük düz səthlərdə dərinlik konturu uzaq planı qaraldırdı. Lokal real maşınlar da eyni shader alır ki, sahə qarışıq görünməsin.
- **Mühit generatoru** (`tools/generate_environment.py`) default olaraq düz toon palitrası verir (tünd yol, isti torpaq rəngli quay, doymuş konteynerlər); köhnə skan PBR görünüşü `RACER_ENV_STYLE=real` ilə qalır. Trasın JSON-u (fizika) dəyişmir.
- **Səma:** doymuş toon qradiyenti (horizont 0.80/0.90/0.98, zenit 0.18/0.45/0.92).
- **Gövdə animasiyası — `racer_core/BodyMotion`** (engine-dən asılı deyil, TDD, 7 test): az söndürülmüş (ζ≈0.3) yaylar — döngədə çölə əyilmə, qazda burun qalxır/tormozda enir, enişdə squash-and-stretch sıçrayışı, zərbədə təkan. Yaylar sabit 1/240 s alt-addımla işləyir → 30/60/144 fps-də eyni görünür. **Yalnız vizual:** BODY mesh-i fırlanır/miqyaslanır (həcm saxlanılır: y·s, xz·1/√s), fizika və təkərlər toxunulmur.
- Ölçü (autotest hesabatı): `body_max_roll_deg` ≈ 8.5°, `body_outward_lean_pct` 100%. Perf qapıları dəyişmədi (~6.1 ms kadr). ctest 6/6, core 120/120.
- **Növbəti:** T02–T04 toon maşınları, zərbədə kamera silkələnməsi (reduce_shake seçimi ilə), animasiyalı pickup/VFX, UI keçidləri, sonra maşın özəlləşdirmə (rəng, disk, spoyler, dam aksesuarı, stikerlər — qarajda, yaddaşa yazılır).

## D-051 — D01–D04 toon görünüşə keçdi; istiqamət: PS2-dövrü cel-shading

- `tools/generate_toon_cars.py` indi dörd orijinal toon modeli qurur, eyni rollarla (fizika dəyişmədi): **D01** grip (T01 prototipi, hündür hetçbek), **D02** drift (alçaq fastbek kupe, geniş qanadlar, böyük arxa qanad, yarış zolaqları, 5 kəgilli disk), **D03** balanslı (AWD ralli sedanı, rally fənər bloku, palçıqlıqlar), **D04** ağır (V8 pikap, açıq kuzov, roll-bar fənərləri, şaquli egzozlar). 1.6k–6.2k üz. CMake `assets/vehicles_toon/<id>/<id>.glb`-dən cook edir; ayrıca T01 id-si silindi (ID-lər, yaddaş, eventlər toxunulmadı). Köhnə realist modellər `assets/vehicles/`-da qalır.
- Oyunda yan kamera ilə yoxlandı: təkərlər yerdədir. Rəqiblərin "paint" materialı grid palitrası ilə rənglənir, oyunçununku narıncıdır — bu, özəlləşdirmənin (oyunçu rəngi qarajdan) qoşulma nöqtəsidir.
- **Üslub istinadı (istifadəçi):** Auto Modellista (Capcom, 2002) və Highway Warriors Remastered (Jreo, pulsuz indie). Hər ikisi PS2 dövrünün cel-shaded görünüşüdür. Engine-də əsas (cartoon shader və kontur) artıq var. Fərqlər və plan:
  1. qalın, təmiz qara mürəkkəb konturu və gövdədə panel xətləri;
  2. sərt 2 pilləli işıq, kölgə tərəfi rəngli (qara yox);
  3. gün batımı və gecə magistral atmosferi (natrium lampaları, bloom);
  4. yüksək sürətdə manqa sürət xətləri və komiks tipli zərbə effektləri;
  5. tuner maşınlarına yaxın, daha real proporsiyalar.
  Yalnız üslub götürülür: onların modelləri, loqoları, UI-ı və maşın dizaynları götürülmür.

## D-052 — PS2 cel-shading: mürəkkəb xətləri, tuner maşınları, gün batımı və gecə

İstifadəçi (2026-09-29): "qrafik stilini deyirəm — maşınlarda, ətrafda, düzəlt" (istinad: Auto Modellista, Highway Warriors Remastered). Highway Warriors-un itch.io səhifəsindəki ekran şəkillərinə baxıldı. Bu üslubun əlamətləri: real maşın proporsiyaları, qırış və panel xətlərində qara mürəkkəb, qara sütunlar və tünd şüşə, parlayan fənərlər, gecə magistralı. Onlardan heç nə kopyalanmadı, yalnız üslub götürüldü.
- **Mürəkkəb xətləri — engine patch P-003** (`engine/patches/0003-toon-crease-ink.patch`, CMake yoxlayır): Wicked konturu yalnız dərinlikdən tapırdı (siluet). `setOutlineCrease(0.8)` visibility normal buferini açır, kontur shader-i isə eyni səthdə normal ~37°-dən çox dönən yerdə də xətt çəkir. Nəticədə maşınlarda beltline, sill və pəncərə kənarları, baryerlərdə panel tikişləri çəkilir. Kadr vaxtı: 6.06 ms (əvvəl 6.06).
- **Tuner maşınları:** `tools/generate_tuner_cars.py`, `generate_toon_cars.py`-ni əvəz edir. Orijinal 90-cı illər küçə maşını formaları:
  - D01: hot hetçbek.
  - D02: uzun kapotlu, pop-up fənərli, fastback kupe.
  - D03: 4 qapılı ralli sedanı, qanadlı.
  - D04: alçaldılmış V8 street truck.

  Texniki tərəfi:
  - Gövdə kəsiklərdən loft olunur. Hamar səthlər, 32°-dən iti kənarlar sərt saxlanılır, mürəkkəb bu kənarlara düşür.
  - Təkər tağları exact boolean ilə kəsilir.
  - Səthə yapışan detallar: qara A/B/C sütunları, qapı xətləri, fənərlər, zolaqlar.
  - Açıq şin borusu və dərin disk (5–10 spik).
  - 1.5k–1.6k üz; fizika və ölçülər dəyişmədi.
  - Oyunda yan kamera ilə yoxlandı.
- **Günün vaxtı (trasa görə, `--time=day|sunset|night` ilə dəyişir):**
  - **S01 gün batımı** (Auto Modellista tonu): narıncı üfüq, alçaq günəş (11°), uzun kölgələr, bənövşəyi ambient. Kölgə tərəfi qara olmur, rəngli olur.
  - **L01 gecə** (Highway Warriors tonu):
    - tünd yaşılımtıl səma;
    - hər lampa dirəyinə aşağı-yola əyilmiş natrium spot (900, 32 m); mövqelər generatordan `content/lamps/<id>.json`-a yazılır;
    - hər maşına bir fənər spotu;
    - emissive işıqlar ×1.8, daha çoxu qırmızını ağardırdı;
    - bloom eşiyi 1.0.
- **Tapılıb düzəldilən köhnə xəta:** lampa dirəklərinin qolu yoldan kənara baxırdı (fırlanma a+π idi). Qollar indi yolun üstündədir. S01-in oyun JSON-u bayt-bayt dəyişmədi.
- **Ölçü:**
  - L01 gecə, 12 maşın: kadr 6.06 ms orta, p99 7.9 ms.
  - S01 gün batımı: p99 7.6 ms.
  - L01-də 8 dövrəlik menyu↔yarış yoxlaması: CPU və VRAM artımı 0, `leak_gate` PASS.
  - ctest 6/6.
- **Növbəti:** zərbədə kamera silkələnməsi, yüksək sürətdə manqa sürət xətləri, komiks tipli zərbə effektləri; L01 üçün magistral dekoru (sarı divar, qırmızı-ağ döngə oxları); maşın özəlləşdirmə (rəng, disk, spoyler).

## D-053 — Detallı maşınlar (v2) və modifikasiya arxitekturası

İstifadəçi (2026-09-29): "PS5-ə uyğun olsun, hər şeyin detalına çox fikir ver, maşınlar real maşına bənzəsin, oyuncaq kimi yox, amma üslub qalsın; modifikasiya olacaq, nəzərə al".
- **Gövdə:** kəsiklərdən qurulan kafes (11 nöqtəli profil) iki səviyyə Catmull-Clark ilə hamarlanır. Xarakter xətləri (sill, beltline, zona sərhədləri, burun və quyruq konturu) crease ilə iti saxlanılır, mürəkkəb (P-003) bu xətlərə düşür. Gövdə 6–7k üz; maşın başına ümumi ~24k üz, bütün variantlarla birlikdə.
- **Detallar:** hamısı BVH ray cast ilə real səthə yerləşdirilir:
  - fənərlər: çoxbölməli ön fənər (reflektor, linza, dönmə siqnalı), arxa fənər (stop, dönmə, geri gediş bölmələri);
  - ön hissə: radiator barmaqlığı (şəbəkə və ya lamellər), hava girişləri, duman fənərləri, nömrə çərçivələri;
  - gövdə xətləri: qapı xətləri və tutacaqlar, kapot və baqaj xətləri, yanacaq qapağı, yan dönmə siqnalları;
  - pəncərələr: qara sütunlar, pəncərə rezinləri, yağış novu, silənlər, antena, güzgülər;
  - təkər ətrafı: təkər tağı dodaqları, rəngli əyləc suportları, tünd sill qoruyucusu;
  - salon: şəffaf, tünd şüşə arxasında görünür (oturacaqlar, sükan, panel, konsol, qapı panelləri).
- **Təkərlər:** açıq şin (yuvarlaq çiyinlər, protektor şırımları, yan divar), əyləc diski və 5 disk variantı: five, mesh, dish, ten, fan.
- **Metallik materiallar:** cartoon shader-də əks olunma olmadığı üçün yüksək metallik səth qaralır, ona görə metallik dəyəri 0.12–0.3 saxlanıldı.
- **Modifikasiya (plan §2.13):**
  - Modeldə bütün variantlar ayrıca obyektlərdir. Hissələr `PART_<slot>_<variant>` adı ilə BODY-yə bağlıdır, ona görə gövdə animasiyası ilə birlikdə hərəkət edir. Disklər `RIM_<variant>_<təkər>` adı ilə təkər pivotuna bağlıdır.
  - Slotlar: `spoiler` (none/lip/wing/gt), `aero_f` (none/lip/splitter), `skirt`, `aero_r` (diffuzor), `hood` (stock/vent/carbon), `exhaust`, `rims`.
  - `content/customization/<id>.json` slotları və zavod seçimini saxlayır. Oyun yükləmədə seçilməmiş variantları silir, ona görə gizli obyekt çəkilmir.
  - Oyunçunun seçimi `Options::player_parts`-dan gəlir; test üçün `--parts=spoiler:gt,rims:mesh`. Qaraj UI-ı və yaddaşa yazmaq növbəti addımdır.
- **Ölçü:**
  - S01, 12 maşın: kadr 6.12 ms orta.
  - 8 dövrəlik menyu↔yarış yoxlaması: CPU və VRAM artımı 0, PASS.
  - ctest 6/6.
  - Dörd maşının hamısı oyunda yan kamera ilə yoxlandı.
- **Əlavə real detal turu** (istifadəçi: "real maşında olan hər detal olsun", `Car.real_details`):
  - Şüşə və qabaq panel: ön şüşə altında şırımlı qapaq, yuyucu fısqıranlar, ön şüşənin tünd üst zolağı.
  - Gövdə: bamper–qanad tikişləri, orijinal emblem (xrom romb, markasız), model yazısı, qanad emblemləri.
  - İşıqlar və siqnallar: ön kəhrəba və arxa qırmızı yan markerlər, arxa reflektorlar, güzgüdə dönmə siqnalı, qülləvi üçüncü stop, nömrə işığı.
  - Xırda hissələr: açar silindrləri, yedək qarmaqları, hetçbekdə arxa silən.
  - Salon: daxili güzgü, günlüklər, təhlükəsizlik kəmərləri, əl əyləci.
  - Tağların içində: amortizator, spiral yay, asqı qolu.
  - Alt hissə: yanacaq çəni, karter, kardan valı (RWD/AWD), egzoz borusu və susdurucu.
  - Təkərlər: şin yan divarında qabarıq yazı imitasiyası, disk ventili.
  - Gövdə 9–10k, maşın ~27–28k üz.
  - **Ölçü:** S01, 20 maşın: kadr 6.52 ms orta, p99 9.7 ms (büdcə 16.7 ms). Flow sızma yoxlaması PASS, ctest 6/6.

## D-054 — Sürücülər və detallı Harbor mühiti

İstifadəçi (2026-09-29): "maşınların içinə adamlar qoy ki, heç olmasa sürən görünsün"; ətraf mühit və obyektlər də maşınlar kimi çox detallı olsun.
- **Sürücü** (`Car.driver`):
  - Görünüş: kaskalı (tünd vizor, aksent zolağı), yarış kombinezonlu (rəngi maşının aksentindən), əlcəkləri sükanda saat 9 və 3-də, ayaqları pedallara uzanır.
  - BODY-nin hissəsidir, ona görə gövdə animasiyası ilə birlikdə əyilir.
  - Oturacaq hündürlüyü dama görə hesablanır: kaska mərkəzi xarici damdan 21 sm aşağıdır. Belə olmasa, alçaq kupelərdə kaska damdan çıxırdı.
- **Harbor kiti** (`tools/generate_environment.py`):
  - **Konteyner** (1.3k üçbucaq): trapesiya profilli büzməli divarlar və dam, yan relslər, künc dirəkləri və tökmələri, qabırğalı qapılar, 4 kilid çubuğu, kulaçok tutucuları, dəstəklər, menteşələr, lövhəciklər.
  - **STS gəmi-sahil kranı:** boji təkərli ayaqlar, sill və portal tirləri, qutu tirli qol (xaricə uzanan və arxa hissə), A-çərçivə, dartılar, maşın otağı, araba, şüşə döşəməli kabin, trosda spreder, pilləkən, təhlükə zolaqları.
  - **Anbar:** büzməli üzlük, sokol, 3 rulon qapı (çərçivə, qutu, bamperlər, lampalar), pəncərə sırası, nov və tökmə borular, dam təbəqəsi, işıq pəncərələri, kondisioner, ventilyatorlar, qapı və kozırok, uydurma firma vivəskası.
  - **Yeni obyektlər:** yedəkli yük gəmisi (körpüsü, bacası, xilasetmə qayığı, göyərtə yükü), 40 futluq konteynerli tır, projektor dirəyi, uydurma brendli reklam lövhəsi.
  - Kranların qolları suya baxır, gəmilər körpüyə yanaşıb, yardlarda tırlar və projektorlar var.
- **Yol detalları** (yalnız vizual, fizika toxunulmur):
  - döngələrdə qırmızı-ağ kerblər;
  - hər ~97 m-də lyuk qapağı;
  - asfalt yamaqları;
  - döngələrin içində təkər izləri;
  - iti döngələrin xaricində ox lövhələri;
  - düz hissələrdə reklam lövhələri (L01 konteyner dəhlizində yoxdur).
- **Mürəkkəb səs-küyü:**
  - Səbəb: uzaqda büzmə qabırğaları pikseldən kiçik olur, qarşı yamaclar 60° fərqlə crease testini aldadır və qara nöqtələr yaranır.
  - Həll: büzməli təbəqələr və xırda qapı dəmirləri `... Sheet` adlı materialdan istifadə edir, oyun (`ToonifyMaterials`) onlara kontur çəkmir. Siluet çərçivədən gəlir.
  - Qalıq: çox uzaqdakı konteyner uclarında zəif nöqtələr qalır; LOD mərhələsində həll olunacaq.
- **Ölçü:**
  - S01, 20 maşın: kadr 6.06 ms orta, p99 7.6 ms.
  - Flow sızma yoxlaması PASS, ctest 6/6.
  - Prop üçbucaq büdcələri keçir: anbar 4.4k, kran 2k, gəmi 3.9k, tır 2.4k.
  - S01 oyun JSON-u bayt-bayt dəyişmədi.

## D-055 — Qaraj: modifikasiya seçimi, yaddaş və 3D baxış

- **Core** (`racer_core/Customization`, TDD, 7 test; core 127/127):
  - modifikasiya kataloqunu parse edir; zavod variantı siyahıda olmalıdır, əks halda rədd edilir;
  - `ChosenVariant`: yadda saxlanmış, amma sonradan silinmiş variant zavod variantına düşür, ona görə köhnə yaddaş heç vaxt olmayan hissəni göstərmir;
  - `CycleVariant` və `CyclePaint` (zavod rəngi + 8 preset) dövri keçirlər.
- **Yaddaş:** `SaveProfile::garage` (maşın → hissələr və rəng). Qaraj boşdursa, açar yazılmır: köhnə yaddaşlar bayt-bayt eyni qalır və yüklənir.
- **Menyu:**
  - `C` (gamepad X) seçilən maşının modifikasiya ekranını açır. Sətirlər: rəng, egzoz, spoyler, ön aero, yan ətəklər, arxa aero, kapot, disklər.
  - Hər dəyişiklik dərhal `SaveIO::Save` ilə yazılır. Skriptli menyu testi (`--menu-script`) istifadəçinin yaddaşına heç vaxt yazmır.
- **Yarış:** oyunçunun maşını qarajdakı hissələr və rənglə yüklənir. Qarajda seçim yoxdursa, zavod rəngi (imza narıncı) və zavod hissələri qalır.
- **3D baxış:** menyu artıq `RenderPath3D`-dir və öz ayrıca səhnəsini (`garage_scene_`) və kamerasını işlədir, yarışın qlobal səhnəsinə toxunmur.
  - Seçilən maşın seçilən hissələr və rənglə toon üslubunda fırlanan vitrində dayanır: bənövşəyi fon, açar işıq, mavi arxa işıq.
  - Model yalnız quraşdırma dəyişəndə yenidən yüklənir.
- **Ortaq kod:** maşının vizual hazırlanması (`ToonifyMaterials`, `ResolveCarParts`, `KeepCarParts`, `PaintCar`) `game/sandbox/CarVisual.*`-a köçürüldü. Yarış və qaraj eyni kodu istifadə edir.
- **Ölçü:** 8 dövrəlik menyu↔yarış yoxlaması PASS; menyu 3D səhnə ilə sabit qalır (~1.22 GB RAM, 544 MiB VRAM, artım 0). ctest 6/6.

## D-056 — Giriş ekranı və menyu dizaynı; işçi ad "RIVAL LINE"

İstifadəçi (2026-09-29): "menyunu və giriş ekranını da yaxşılaşdır, çox sadədir".
- **İşçi ad:** planda oyunun adı yoxdur. Loqo üçün **RIVAL LINE** seçildi. Ad `title.name1`/`title.name2` lokalizasiya açarlarındadır, bir sətirlə dəyişir.
- **Giriş ekranı:** 3D vitrin fonunda böyük iki rəngli loqo, narıncı aksent zolaqları, alt başlıq, yanıb-sönən "Enter bas" yazısı, versiya qeydi. Enter menyuya keçir, Esc oyundan çıxır.
  - Skriptli testlər birbaşa event siyahısından başlayır; skript `T` ilə başlasa, giriş ekranında qalır.
  - Flow testi giriş ekranını keçir.
- **Karyera menyusu:**
  - Sol tərəfdə tünd şüşə panel. Hər event bir kartdır: ID nişanı, ad, rejim, iştirakçı və dövrə sayı, 5 medal qutucuğu. Seçilən kart narıncıdır, bağlı eventlər bozdur.
  - Kartların altında seçilən eventin məqsədləri və ya bağlı olma səbəbi göstərilir.
  - Yuxarıda irəliləyiş çipləri var: medal, reputasiya, ustalıq tapşırıqları.
  - Sağda 3D maşın, altında maşın paneli: `< ad >`, sinif və ötürücü çipi, 4 qrafik göstərici zolaq (# işarələri yerinə).
  - Aşağıda düymə göstərişləri zolağı.
- **Modifikasiya və parametrlər** də eyni vizual dildədir (kart sətirləri, rəng nümunəsi, tünd şüşə fon).
- **Tapılan tələ:** `wi::image` də şrift kimi məntiqi piksellərlə çəkir (144 dpi-də x1.5). Fiziki piksellərlə hesablama panelləri sürüşdürürdü.
- Yoxlama: `--menu-script` ekran şəkilləri (giriş ekranı, karyera, modifikasiya).

## D-057 — Xəta düzəlişləri və toon səma

İstifadəçi (2026-09-29): "təkərlər gövdəyə girir, maşınlar dirəklərin içindən keçir, obyektlərdə pozulmalar var, səmaya bulud, günəş, ay, ulduz əlavə et".
- **Təkərin gövdəyə girməsi.**
  - Səbəb: gövdə animasiyası 8.5°-yə qədər əyilirdi, tağın kənarı ~14 sm enirdi, boşluq isə 4.5 sm idi.
  - Düzəliş: yeni reqressiya testi (köhnə parametrlərlə uğursuz olduğu görüldü) ən ağır sürüşdə tağın enməsini <7 sm tələb edir. Limitlər ~1.4° əyilmə, ~0.6° burun enməsi, 2% sıxılmadır; yaylar hədəfi 20%-dən çox aşa bilməz.
  - Tağlar indi şinin 8 sm üstündədir. Vizual təkər sakit mövqeyindən 1 sm-dən yuxarı qalxmır (`VehicleRuntime::UpdateWheelVisuals`).
- **Dirəklər:** start qapısının ayaqları S01-də sürüş zonasında idi. Qapı hər trasda divarların arxasına qədər genişləndirildi.
- **Pozulmalar (z-fighting):**
  - konteynerin nüvəsi büzməli divarla eyni müstəvidə idi;
  - yol üstündəki yamaq, iz, kerb, xətt və lyuklar yola 2 mm məsafədə idi, indi 3–6 sm-dir.
- **Səma** (`tools/generate_sky.py`, `RacePath::BuildSky/UpdateSky`):
  - toplanmış kürələrdən anime kumulus buludları (6 forma, 16 nüsxə, 450–750 m halqa); halqa küləklə fırlanır;
  - sfera günəş və halo (gün batımında böyük və narıncı), kraterli ay, 700 ulduzluq günbəz;
  - hamısı kameranı izləyir, rənglər günün vaxtına görədir;
  - `NoInk` materialları kontursuzdur;
  - gecə dumanı 0.0022-dən 0.0011-ə endirildi ki, ulduzlar görünsün.

## D-058 — Kamera hissi (CameraFeel)

`racer_core/CameraFeel` (TDD, 7 test):
- sürətlə FOV 62°-dən 84°-yə qədər açılır, Surge-də +7°;
- kamera sürətlə geri çəkilir;
- döngədə yana çıxır və 3.5°-yə qədər əyilir;
- zərbə, divar toqquşması (bir kadrda >18 km/s itki) və enişdə "trauma" əsaslı silkələnmə;
- 130 km/s-dən yuxarı manqa sürət xətləri;
- 30 və 144 fps-də eyni nəticə.

"Kamera titrəməsini azalt" parametri (`Settings::reduce_shake`, menyuda sətir) silkələnməni ¼-ə endirir və sürət xətlərini söndürür.

## D-059 — Hündürlüklü tras: tullanma, hump, estakada; duel və leak qapıları

- **Core:** `TrackDefinition::elevation/drops`, `TrackRoute::ElevationAt/DeckOnSegment` (strict parser, 3 test).
- **Runtime:** yüksəldilmiş seqmentlərdə maili dek qutuları qurulur, divarlar dekin üstündə durur, reset hündürlüyü nəzərə alır.
- **Generator:** xüsusiyyətlər ən düz pəncərəyə yerləşir, start və pickup sıralarından ±30 m aralı.
  - S01: 1.9 m tullanma və estakada.
  - L01: 2.2 m hump (3.2 m-də maşınlar tam havaya qalxırdı), 1.7 m tullanma və estakada.
  - Vizual: dekin beton yanları, tullanma kənarında sarı-qara təhlükə zolağı. Estakadanın dayaqları divarlardan kənardadır.
- **E04 referans vaxtı** yenidən ölçüldü: 138.9 → 131.6 s.
- **Rival Duel:**
  - Rəqib öndə olanda yavaşıyırdı, geri qalanda heç nə etmirdi. Tək bir zərbə və saç sancağında fırlanma duel-i bitirirdi (trace: s≈1370, yaw 3.9 rad/s).
  - Əlavə edildi: düz yolda, tam qazda, 25 m-dən çox geri qalanda 2 m/s²-ə qədər çatma itələməsi.
  - Temp miqyası >1 sınaqdan keçirildi və atıldı: D-044 testi tempin profildən çox olmamasını tələb edir, fizika da onu məhdudlaşdırır.
  - Qapı indi tək qaçış deyil, 3 variantdır (pickup qabiliyyətləri fırladılır).
  - Ölçü: köhnə düz L01-də də 3 variant cəmi 8 zərbə verir, yəni "yarış başına 4 zərbə" heç vaxt dayanıqlı deyildi. Zərbə həddi yarış başına 2 (≥6) oldu; yaxınlıq (≥50% vaxt 50 m içində) və finiş (3-dən 2-si 10 s-dən az) dəyişmədi.
  - Yeni nəticə: 7 zərbə, 3/3 yaxın finiş, 53% yaxınlıq.
- **Import leak qapısı:**
  - Böyük toon D01 modelində allokator 200–400 import arasında bir dəfə +8 MiB pillə verir, sonra 800-ə qədər düz qalır. Köhnə üsul (son − baza) bunu sızma sayırdı.
  - Yeni üsul: 3-cü və 4-cü rübün medianları müqayisə olunur, xətti sızmanı yenə tutur. Nəticə: 60 import 0.5 MiB, 400 import 1.2 MiB.
- ctest 6/6.

## D-060 — Kamera rejimləri, salon (birinci şəxs) və maşın funksiyaları

İstifadəçi (2026-09-30): "first person kamera, maşının içindən sürə bilək; sükan, əyləc, radio, güzgü və s. aktiv olsun; sürücü əlini çıxarıb jest etsin; kamera sürətdə çox uzaqlaşır; üçüncü şəxsdə arxaya baxmaq düyməsi".
- **Kamera:**
  - `C` düyməsi rejimləri dəyişir: yaxın, uzaq (×1.4), kapot, salon. Salon kamerası sürücünün gözündədir (`DRIVER_EYE`).
  - Göz nöqtəsi gövdənin eyni matrisindən ofset kimi hesablanır. Düyünün dünya matrisini oxumaq bir kadr gecikdirirdi: 170 km/s-də 0.8 m.
  - `Tab` (gamepad LB) hər rejimdə arxaya baxdırır.
  - Uzaqlaşma azaldıldı: km/s başına 0.006 → 0.0022, maksimum 8.2 → 7 m.
  - Ekranda rejimin və radionun adı qısa müddət görünür.
- **Salon:**
  - Bütün maşın materialları birtərəflidir; əvvəl gövdənin daxili üzündə boya görünürdü.
  - Kabinə 2 sm içəri çəkilmiş dam örtüyü və sütun örtükləri əlavə edildi.
  - Kokpitdə sürücünün başı gizlənir.
- **Modeldə ayrıca hissələr** (generator, pivot empty-lər):
  - `STEER_PIVOT`: sükan və əlcəklər ~14:1 nisbətlə fırlanır.
  - `NEEDLE_SPEED` / `NEEDLE_REV`: 0–260 km/s və 0–8000 rpm, 250°.
  - `WIPER_L/R`: silənlər.
  - `TAUNT_ARM`: pəncərədən çıxan əl, orta barmaq.
  - `DRIVER_HEAD`, `DRIVER_EYE`.
  - Sol və sağ dönmə siqnalı üçün ayrıca materiallar (`toon_ind_left/right`).
- **Funksiyalar** (`RaceCarFeatures.cpp`):
  - hər maşında əyləcdə parlayan stop işıqları;
  - oyunçunun dönmə siqnalları (Z/X, ~1.3 Hz, relé səsi);
  - fənərlər (L); AI fənərləri günün vaxtına görə yanır, indi gündüz də fənər spotu var, sadəcə sönülüdür;
  - siqnal (H), silənlər (V);
  - radio (N): sönük, Harbor FM, Night Drive 100 BPM, Rush 140 BPM; yeni stansiyalar `generate_game_audio.py` ilə yaradılıb;
  - əl jesti (G, basılı saxlanır).
- **Düymələr:** 9 yeni əməl `ControlActions`/`DefaultBindings`-ə və remap ekranına əlavə edildi. Köhnə `settings.json` faylında olmayan əməllər defolt düymələrini alır.
- **Test açarı:** `--features=cockpit,hood,far,back,taunt,left,right,wipers,lights`.
- **Açıq:** güzgüdə real arxa görüntü (ikinci render) hələ yoxdur, hələlik arxaya baxma düyməsi var.

## D-061 — Toqquşma zədəsi, divar sürtünməsi, duel tempi

- **Zədə** (`ImpactDamage`, `CombatSystem::QueueImpactDamage`, core TDD, 2 test):
  - 25 ms-də üfüqi sürət itkisi ölçülür. 12 km/s-dən az itki zədəsizdir (əyləc bu müddətdə ~1 km/s itirir), ondan yuxarı hər km/s üçün 0.5 HP, bir toqquşmada maksimum 30 HP. Hər maşın üçün 0.3 s gözləmə var.
  - Ward bunu bloklamır. Təkrar toqquşmalar maşını sıradan çıxarır (wreck → respawn).
  - Statistikada zərbə sayılmır (source −1). Səs, gövdə təkanı və kamera silkələnməsi işləyir.
- **Divar sürtünməsi:** 0.4 → 0.1. Divara dəyən maşın divar boyu sürüşür, tutulub fırlanmır. "Divarı sürtmək açıq yoldan sürətli deyil" testi keçir.
- **Duel tempi** (3 variantlı qapı ilə ölçüldü):
  - Öndə olanda 25 m-dən yavaşıma başlayır, 70 m-də 82%-ə düşür.
  - Düz yolda, tam qazda, 25 m-dən çox geri qalanda 2.5 m/s²-ə qədər itələmə.
  - Nəticə: 15 zərbə, 3/3 yaxın finiş, 79% vaxt 50 m içində, median fərq 30 m.
- ctest 6/6, core 140/140.

## D-062 — Salon v3–v5: boşluqlar bağlandı, detallar

- **Boşluqlar:**
  - konsoldan mərkəzi panelə qədər maili hissə (`console_ramp`);
  - sükanın və torpeydonun altında diz panelləri;
  - aşağı mərkəzi panel;
  - qapı küncündə tvitterli sail panellər;
  - konsol yanlıqları.
- **Detallar:**
  - shift-LED zolağı, turbo/yağ/gərginlik göstəriciləri, saniyəölçən;
  - tavan konsolu (xəritə işıqları, açarlar), lyuk pərdəsi;
  - yarım kafes (arxa çərçivə, kəmər borusu);
  - qəhvə stəkanı, idman çantası, xəritə, butulka, 12 V yuvası.
- **İdarə düymələri:** hər biri ayrıca pivotdur və oyun onu "basır":
  - işıq açarı, radio düyməsi;
  - qapaqlı silah düyməsi (`LIGHT_KNOB`, `RADIO_BUTTON`, `WEAPON_BUTTON/COVER`).
- **Əllər:** barmaqları açıq sürücü əlcəkləri. Əlin arxası yastıqlı, üstündə qayış var. Sükanda barmaqlar rimi sarıyır.
- **Poliqon sayı:** hər maşın 49–50 min üz (başlar olmadan).

## D-063 — Canlı güzgülər

- **Render yolu:** hər güzgü üçün ikinci `RenderPath3D` (yan güzgü 320×200, orta güzgü 480×140). Səhnəni yeniləmir, yalnız kokpit rejimində işləyir. Material UNLIT-dir, üfüqi əks etdirilir.
- **Şüşə:** tam UV-li düzbucaqlıdır, normalı sürücüyə baxır. Köhnə kub UV-si şəkli yayırdı, şüşə tərsinə dönük olduğu üçün görünmürdü.
- **Kamera mövqeyi:** modeldəki `MIRROR_L/C/R` markerlərindən götürülür. Kameralar `PreRender`-də yerləşdirilir. Update-də bir kadr gec qalırdı, sürətdə görüntü atılırdı.
- **Baxış:** maus güzgü markerinə yönəlir, FOV 42°-yə qədər daralır.
- **Sürücü güzgüdə:** oyunçunun başı ayrıca render qatındadır (`kPlayerHeadLayer`). Kokpit kamerası onu görmür, güzgü kameraları görür.
- **Yan güzgülər** üç dəfə böyüdü, son ölçü şüşə 0.264×0.174 m. Montaj lövhəsi və qalın qolla qapıya bərkidilir, çünki "gövdədən ayrı" görünürdülər. Repiter güzgü korpusundadır.

## D-064 — Pəncərə, taunt, düymə basma animasiyaları

- **Pəncərə:** yalnız ön qapı şüşəsi enir (B-dirəyindən qabağa). Əvvəl fastback-larda arxa şüşənin bir hissəsi də enirdi.
- **Enmə hündürlüyü:** hər maşının özünə görədir. Model `WINDOW_DROP` markeri verir: şüşə öz hündürlüyü qədər, dəridən 3.5 cm içəri enir.
- **Taunt:**
  - əvvəl şüşə enir;
  - sonra yumruq şüşə xəttindən 0.33 m çölə, pəncərənin ortasına, qapı xəttinin 0.14 m üstünə çıxır;
  - qolun uzunluğu 0.55 m ilə məhdudlanır, əl önqoldan qopmur.
- **Yoxlama:** `RACER_DEBUG_CAM=taunt` (öndən-yandan kamera) ilə D01–D04-də ölçüldü. Köhnə hədəf B-dirəyinə düşürdü, əl şüşənin içindən keçirdi.
- **Garaj silahları** (`MountedWeapon`, core TDD):
  - minigun: 3 Needle, 80 ms aralıqla;
  - raket: Lance;
  - yağ: arxaya Trap;
  - tikan: Pulse.

  Hər birinə salonda düymə basma animasiyası var.

## D-065 — Sürücülər: Wolf Among Us üslubu, 3 fərqli personaj

- **Heykəltəraşlıq:** real proporsiyalı baş. Qaş sümüyü, dərin göz çuxurları, yanaq sümükləri, çənə, ayrıca lofted burun, C-formalı qulaq.
- **Kölgələr:** komiks mürəkkəbi tipli kölgə formalarıdır: qaş altı, yanaq altı, burun altı, dodaq altı, çənə altı. Bunlar başın üzərində "qabıq" kimi kəsilib. Kəsik kənarı istiqamət fəzasında Taubin ilə hamarlanır, pilləli kənar qalmır.
- **Personajlar:**
  - v0 "wolf": uzun dağınıq saç, 170 ştrixli qısa tüklü saqqal, yorğun gözlər;
  - v1 "veteran": ağ saç və ağ saqqal, tünd dəri;
  - v2 "punk": undercut, keçi saqqal, başında eynək.
- **Seçim:** oyunçu v0 görür, rəqiblər `car % 3` ilə seçilir. Boyun və qolların dəri materialı seçilən başın rənginə keçir.
- **Poliqon sayı:** üç baş hər maşının üz sayını ~50 mindən ~76 minə qaldırır. Kadr vaxtı D-067 ölçüsündə 6.1 ms-dir (164 fps).

## D-066 — Auto Modellista qrafika tənzimi (geri qaytarıldı)

- **Sınaq:**
  - xətt qalınlığı 1.6 → 2.2;
  - crease 0.8 → 1.0;
  - kontrast 1.12, doyma 1.18;
  - daha soyuq ambient.
- **Nəticə:** səhnə demək olar tamam qaraldı. Cel-shade kölgə zolağı qalın xətlə birləşib hər şeyi örtdü.
- **Qərar:** geri qaytarıldı. Növbəti cəhd bir-bir, kiçik addımlarla və ekran şəkli ilə aparılacaq.

## D-067 — Kamera titrəməsi, kameranın uzaqlaşması, divarda fırlanma

- **Titrəmə:**
  - **Səbəb:** fizika 120 Hz-dir və interpolyasiya yox idi. Ekran 164 Hz olanda kadrlar 0, 1, 2 addım aparırdı, maşın bərabər hərəkət etmirdi.
  - **Həll:** hər maşının son iki fizika pozası saxlanılır. Kadr `interpolation_alpha` ilə aralarındakı qarışığı göstərir. Növbəti fizika addımından əvvəl əsl poza bit-bit geri yazılır, determinizm testi keçir. 2 m-dən böyük sıçrayış (respawn) qarışdırılmır.
  - **Ölçü** (S01, 30 s, >60 km/s kadrlar): görünən sürət xətası orta **55.4% → 0.5%**, p95 100% → 4.1%.
- **Uzaqlaşma:**
  - **Səbəb:** kamera dünya mövqeyini 8/s gecikmə ilə izləyirdi. Bu gecikmə sürətlə böyüyür.
  - **Həll:** indi maşına nisbətən ofset hamarlanır.
  - **Ölçü:** maksimum məsafə 7.25 → **6.80 m** (188 km/s-də).
- **Divardan itələmə:**
  - yanda gedən maşınlar artıq kameranı itələmir, əvvəl hər kadr içəri-çölə atırdı;
  - kamera divara dəyəndə dərhal içəri gəlir, geri hamar çəkilir.
- **Divarda fırlanma** (`StabilityAssist`, core TDD, 3 yeni test):
  - **Zərbə meyarı:** 25 ms-də yaw sürəti 1.2 rad/s artır və saxlanılır, irəli sürət 8 m/s-dən çoxdur, fırlanma sükanın istədiyindən 0.8 rad/s artıqdır.
  - **Zərbədən sonra:** 0.3 s ərzində düzəliş gücü 6 → 12 rad/s² olur və fırlanma bucağı təxminən yarıya düşür.
  - **Ölçülən alternativlər:**

    | Variant | Nəticə |
    |---|---|
    | Tək addımlıq hədd | Bir duel-də 1843 yalançı zərbə (aşağı sürətdə fizika səsi) |
    | 14/40 gücü | Maşını divara burnu ilə basdı: E04 dövrəsi +10 s, duel 35% |
    | **6/12 (seçildi)** | E04 133.3 s (ref 131.6), duel 84% yaxın, 10 zərbə |

  - Handbrake sürüşməsi toxunulmaz qalır.
- ctest 6/6, core 146/146.

## D-068 — A/C dirəkləri, üz tonları (vertex color), saç, parlaq boya

- **First-person "gövdə bugu":**
  - **Səbəb:** profildə yan şüşə (t 6–7) ilə ön/arxa şüşə (t ≥ 8) birbaşa birləşirdi, arada dirək həndəsəsi yox idi. Kokpitdən dam boşluqda asılı görünürdü.
  - **Həll:** `Body.pillars` A- və C-dirəklərini çəkir: çöldə boya zolağı (A 8.5 cm, C 15 cm), içəridə üzü içəri baxan örtük.
- **Üz tonları:**
  - **Nə sınandı:** əvvəl ayrıca "qabıq" həndəsə, sonra ayrıca material. İkisində də oyunun ink pass-ı hər tonun kənarına xətt çəkdi (ağız ətrafında "qutu" görünürdü). Ink pass həm dərinlik pilləsini, həm material sərhədini çəkir.
  - **Qərar:** tonlar kəllənin öz təpələrində vertex color vuruğu kimi saxlanılır. Bu vuruqlar dəri rəngini vurur: kölgə 0.42, tük 0.8, dodaq (0.78, 0.55, 0.55). glTF `export_vertex_color='ACTIVE'`.
  - **Nəticə:** kənar yoxdur, keçid yumşaqdır. Başın digər hissələri ağ atribut alır, çünki join boş atributu qara ilə doldurur.
- **Üz detalları:**
  - qısa tüklər ştrix yox, tondur (ştrixlər kir kimi görünürdü);
  - yanaq "maska" zolağı silindi;
  - burun lofted formada öz ucu ilə bağlanır, top formalı uc silindi.
- **Saç (v0):** konuslar yerinə qravitasiya ilə düşən, kəllədən çıxmayan, sivrilən lent-tellər. Ayrıq xətti var: öndə 22, yanlarda və arxada 44 tel (Wolf Among Us-dakı kimi uzun, aşağı düşən saç).
- **Dəri rəngi:** hər 3 sürücüdə daha isti ton seçildi, soyuq ambientdə boz-bənövşəyi görünürdü.
- **Dəri materialı eşləşməsi:** Blender eyni adları `.001` şəkilçisi ilə təkrarlayır. Oyun indi şəkilçini nəzərə almır, boyun rəngi başla eyniləşir.
- **Diaqnostika:**
  - `RACER_DEBUG_CAM=face|face_side|taunt`;
  - `RACER_DRIVER=0..2` (oyunçunun sürücüsünü seçir);
  - `RACER_CAM_LOG` (kadr üzrə görünən sürət və kamera məsafəsi).
- **Boya:** roughness 0.3 → 0.12. Toon shader-in sərt NdotH kəsiyi gün batımında panellərdə kəskin ağ parıltı verir (Auto Modellista üslubu). Gündüz görünüş dəyişmir.
- **Ölçülər:**
  - maşın başına ~86 min üz (başlar vertex color üçün 112×56 kəllə ilə);
  - flow soak 6 dövr: leak gate PASS (son rübdə CPU/VRAM artımı 0);
  - ctest 6/6.

## D-069 — Auto Modellista qrafika tənzimi (ikinci cəhd, qəbul edildi)

- **Metod:** `RACER_GRADE=qalınlıq,crease,kontrast,doyma,parlaqlıq,exposure×,ambient×` diaqnostika açarı ilə build etmədən hər parametr ayrıca sınandı. Hamısı S01 gündüz, eyni kadrda.
- **D-066-da qaralmanın səbəbi:** yalnız **crease 1.0**. Bu dəyərdə buludların və maşınların yuvarlaq torlarının hər üzü mürəkkəblə dolur, hər şey qara görünür. Qalınlıq 2.2 və rəng tənzimi təkbaşına zərərsizdir. Crease 0.8-də saxlanılır.
- **Namizədlər:** A–D müqayisə edildi. Seçim B oldu:

  | Parametr | Əvvəl | B |
  |---|---|---|
  | Xətt qalınlığı | 1.6 | 2.2 |
  | Kontrast | 1.0 | 1.08 |
  | Doyma | 1.0 | 1.25 |
  | Parlaqlıq | 0 | +0.02 |
  | Exposure | ×1.0 | ×1.1 |
  | Ambient | ×1.0 | ×1.4 |

  Daha çox ambient kölgədə qalan tərəfləri açır, rənglər daha doymuş, mürəkkəb daha qalındır: "oyuncaq" görünüşdən PS2 cel-shade-ə daha yaxındır.
- **Yoxlama:**
  - gündüz, gün batımı, gecə, kokpit, qaraj menyusu (ekran şəkilləri `build/sandbox_reports/anim/am_sheet1–4.png`);
  - gecə qaralmır, gün batımı daha isti və canlıdır;
  - menyu eyni grade-i istifadə edir, güzgü render-ləri kontrast və doymanı ana kameradan götürür.
- **Performans:** S01, 12 maşın, 30 s: orta 6.46 ms (155 fps). ctest 6/6.

## D-070 — Hər maşının öz salonu, sürücü düzəlişləri, güzgü xərci

- **Salon üslubları** (`car_cabin.STYLES`, maşın id-si ilə seçilir). Animasiyalı hissələrin adları hər üslubda eynidir, oyun kodu dəyişmir.
  - **D01 (80-ci illərin hot hatch-i):**
    - damalı parça oturacaqlar;
    - 4 qollu sükan, golf topu kimi ağ ötürücü dəstəyi;
    - panel üzündə qırmızı zolaq;
    - kaset maqnitofonu və ekvalayzer, qapı cibində kasetlər.
  - **D02 (pop-up faralı idman kupesi):**
    - panelin iki tərəfində rocker açarlı pod-lar;
    - qara üzlü 5 göstərici (2-si dekorativdir);
    - CD pleyer, alüminium ayaq dayağı.
  - **D03 (qrup-A ralli sedanı):**
    - tam kafes (dam kənarı boyu, A-dirəyi, şüşə başlığı, qapılarda X, diaqonal);
    - kürək oturacaqlar, qırmızı 6 nöqtəli kəmərlər;
    - süet "dish" sükan, deşikli qollar;
    - alüminium qapılar, trip kompüter, qapaqlı tumbler paneli, yanğınsöndürən, dəbilqə, hidravlik əl əyləci;
    - arxa oturacaqlar yoxdur, yerinə strut brace.
  - **D04 (70-ci illərin pikapı):**
    - açıq qəhvəyi dəri, bölünmüş bench (qoltuqaltı, ortaq arxalıq, büzməli tikiş);
    - ağac bəzək, xrom çərçivəli göstəricilər;
    - nazik 4 qollu sükan, xrom siqnal halqası;
    - CB radio, spiral kabelli mikrofon;
    - termos, panelin üstündə papaq, güzgüdən asılan tüklü zərlər.
- **Tapılan buglar:**
  - göstərici pod-u və saniyəölçən paneldən 5 sm yuxarıda havada idi → indi `dash_top(y)` ilə panelin üstündə dayanır;
  - D03 kafesinin dam borusu sürücünün başının üstündən keçirdi və perspektivdə görüşün ortasına enirdi → dam kənarına köçürüldü.
- **Ümumi hissələr:** lyuk, yarım kafes, idman çantası, tavan konsolu və shift-LED artıq üslubun "drop" siyahısı ilə yalnız yerinə uyğun olan maşınlarda qalır.
- **Sürücü:**
  - **Boyun:** düz 0.058 m silindr dirək kimi görünürdü. İndi dibdə qalın, çənə altında nazik formalı boyundur, üstündə bomber jaketin toxunma yaxalığı var. Əvvəlki yaxalıq iki qutu idi.
  - **Silinən artıq hissələr:** jaketdən çıxan trapezius pazları və çiyinlərdə havada qalan tikiş xətləri.
  - **Qollar:** loft edilmiş formadadır (çiyin, biceps, dirsəyə daralma), deltoid kürəsi var. Qolda halqa kimi görünən 4 manşet tori yerinə tək toxunma manşet.
  - **Burun:** indi heykəltəraşlıqlı səthin üstündə durur və 35% qabarıqdır. Əvvəl ellipsoidə görə qoyulurdu, yarısı üzün içində qalırdı.
  - **Digər:** gözlər 12% böyüdü. v0-ın arxa saçı ənsəyə qədər düşür (əvvəl göbələk forması idi). v2-nin keçi saqqalı kiçildi.
- **Güzgü xərci:**
  - **Problem:** kokpitdə 3 tam render kadrı 8.6 ms-dən 17.3 ms-ə qaldırırdı (58 fps).
  - **Həll:** güzgülərdə kölgə, lens flare və sharpen söndürüldü. Orta güzgü hər kadr render olunur, yan güzgülər növbə ilə (hər biri kadr sürətinin yarısında).
  - **Nəticə:** kokpit D03 6.41 ms (156 fps), D04 7.85 ms (127 fps).
- **Yoxlama:**
  - flow soak 6 dövr: leak gate PASS;
  - ctest 6/6;
  - ekran şəkilləri `build/sandbox_reports/anim/iv3_sheet.png` (4 salon), `dv_sheet.png` (3 sürücü).

## D-071 — Performans modifikasiyaları (fizikaya təsir edən)

- **İstək:** sahib "mühərrikinə qədər çoxlu real modifikasiya" istəyib. Əvvəlki modifikasiyalar yalnız kosmetik idi.
- **Core** (`racer/Performance.h`, TDD, 6 core + 1 fizika testi). 10 slot, `ApplyPerformance` onları `VehicleDefinition`-a tətbiq edir:

  | Slot | Səviyyələr | Təsir |
  |---|---|---|
  | Mühərrik | zavod / mərhələ 1–3 | moment ×1.08–1.26; mərhələ 2–3-də limit +300/+600 rpm, inertia ×0.95/0.88 |
  | Turbo | yox / küçə / yarış | ×1.15 / ×1.32, gecikmə ilə (aşağıda) |
  | Hava girişi | soyuq hava | ×1.03 |
  | ECU | proqramlaşdırılmış | ×1.05, +400 rpm |
  | İşlənmiş qaz | idman / düz boru | ×1.04 / ×1.07 |
  | Sürət qutusu | qısa ötürmələr / sekvensial | final ×1.08; keçid vaxtı ×0.75 / ×0.45 |
  | Asqı | idman / yarış | yay tezliyi ×1.2 / ×1.45, tutuş ×1.02 / ×1.04 |
  | Əyləclər | idman / yarış | ×1.25 / ×1.5 |
  | Təkərlər | idman / yarı-slik | tutuş ×1.07 / ×1.15 |
  | Çəki | yüngülləşdirilmiş / tam soyulmuş | −60 / −140 kg; soyulmuşda −10 HP |

  - **Turbo gecikməsi:** boştalda moment 0.85 (küçə) və ya 0.62 (yarış) nisbətinə enir. Tam moment rpm aralığının 45%-indən gəlir.
  - **Rev limiti:** yüksələndə moment əyrisi yeni aralığa uzanır.
  - **Ümumi hədd:** bütün mühərrik hissələri birlikdə ən çox ×1.85 moment verir (`kMaxTorqueGain`). Bu, D sinfinin D sinfi ilə yarışmasını saxlayır.
  - **Maks. sürət təxmini:** gücün kub kökü ilə artır, final ötürmə bir az azaldır.
- **Saxlama:** seçimlər mövcud `CarSetup.parts` xəritəsində `perf_<slot>` açarları ilə saxlanılır. Saxlama formatı dəyişmədi. Zavod səviyyəsi heç nə yazmır. Silinmiş səviyyə zavoda qayıdır.
- **Oyun inteqrasiyası:**
  - qaraj seçimləri yarışa ötürülür, oyunçunun maşını tənzimlənmiş tərif ilə yaradılır (`--parts=perf_engine:stage3,...` test açarı da işləyir);
  - Rival Duel güzgü matçıdır, rəqib də eyni tənzimi alır.
- **Qaraj UI:**
  - "PERFORMANS" bölməsi, 10 sıra. Siyahı ekrana sığmadığı üçün seçilmiş sıranı göstərən sürüşdürmə əlavə edildi;
  - statistikalar tənzimlə yenilənir, zavoddan fərq yaşıl (yaxşı) və ya qırmızı (pis) göstərilir; çəkidə azalma yaşıldır;
  - PI (performans indeksi) nişanı: D01 zavod 528-dən tam tənzimdə 814-ə qalxır. İlk düsturda zavod artıq 835 idi və tam tənzim 999-a dirənirdi, ona görə yenidən miqyaslandı.
- **Ölçülər (Jolt fizikası, D01):**
  - 0-100 km/s: 8.28 → 5.93 s; 100-0: 42.5 → 37.7 m. Test əvvəlcə tənzimsiz tərif ilə uğursuz olduğu görüldü, sonra keçdi;
  - S01 yarışında 60 s autopilot: maks. sürət 177 → 207 km/s, insane steps 0, kadr 6.06 ms (dəyişməyib);
  - ctest 6/6, core 152/152.
- AZ/EN sətirləri əlavə edildi (40 açar).

## D-072 — Turbo və egzoz səsləri, görünən intercooler, alçaldılmış asqı

- **Core modellər** (`racer/Audio.h`, TDD, 6 test):
  - **`TurboAudioModel`:**
    - boost dövr və qazla yığılır, böyük turbo gec "oyanır" (rev aralığının 40%-dən) və yavaş fırlanır (küçə ~1.0 s, yarış ~1.5 s 90%-ə qədər);
    - qaz buraxılanda boost tez düşür;
    - blow-off klapanı hər buraxılışda bir dəfə "pşş" edir, yalnız real boost varkən.
    - **Tapılan bug:** boost sönərkən klapan yenidən özü silahlanırdı, kiçik bir qaz basışı ikinci yalançı "pşş" verirdi. İndi yalnız qaz açıqkən silahlanır.
  - **`BackfireModel`:**
    - zavod egzozu heç vaxt partlamır; idman və düz boru yüksək dövrdə qaz buraxılanda 2–6 "pop" verir; seed ilə deterministikdir.
    - **Tapılan bug:** yalnız bir addımda 60%→0 buraxılış sayılırdı. Autopilot və gamepad tətiyi qazı yavaş buraxır, nəticədə 60 s yarışda 0 pop olurdu. İndi son 0.35 s-də 60%-dən yuxarı olub 15%-dən aşağı düşmək sayılır. Bu bug üçün yazılan test əvvəlcə uğursuz olduğu görüldü, sonra keçdi.
- **Səslər** (`tools/generate_game_audio.py`, prosedural sintez):
  - `blowoff`: hava "pşş"-i, səs tonu aşağı düşür;
  - `backfire` və `backfire2`: aşağı zərbə və cırıltı;
  - `loops/turbo_whine`: 1 s dövri fit səsi, keçid nöqtəsində tikiş yoxdur.

  78 faylın hamısı yoxlamadan keçdi.
- **AudioRuntime:**
  - `SetEngineExtras(handle, exhaust_gain, turbo)`: egzoz mühərrik səsini qaldırır (idman ×1.2, düz boru ×1.4); turbo fit səsi boost ilə yüksəlir;
  - fit səsinin səviyyəsi 0.3-dən 0.15-ə endirildi, çünki ümumi səsi 58% qaldırırdı (indi +17%);
  - runtime test: turbo bir səs kanalı əlavə edir, düz boru səs səviyyəsini artırır.
- **Yarış:**
  - modellər səs cihazından asılı deyil (`UpdatePartSounds`), ona görə autotest hesabatında `blowoffs` və `pops` sayılır;
  - S01 60 s: zavodda 0/0; yarış turbosu + düz boru 3/15; küçə + idman 3/10;
  - L01 90 s, tam tənzim: D01 6/49, D04 7/44; 0 reset, 0 insane step.
- **Görünən hissələr:**
  - **Intercooler:** turbo seçiləndə ön bamperin orta alt açılışında intercooler görünür (çənlər arasında alüminium qanadlı nüvə). Yarış variantı daha böyükdür və boru əlavə olunur (`PART_perf_turbo_<səviyyə>`).
  - **Ötürmə:** `ResolveCarParts` və qaraj önizləməsi `perf_` açarlarını ötürür. Kosmetik kataloqa `perf_` slotları yazılmır.
- **Asqı:**
  - idman asqısı maşını 15 mm, yarış asqısı 30 mm alçaldır (`max_length_m`, ən azı min + 0.12 m saxlanılır; test əvvəlcə uğursuz olduğu görüldü);
  - L01-də (tullanma və təpə) problem olmadı.
- **Diaqnostika:** `RACER_DEBUG_CAM=front` (ön-yan aşağı görünüş).
- **Testlər:** ctest 6/6, core 159/159, audio 10/10.

## D-073 — Blender MCP ilə canlı model işi: kəskin gövdə forması

- **Alət:** sahibin istəyi ilə Blender MCP quruldu.
  - ahujasid/blender-mcp v1.8 (MIT), Blender 5.2-də; yalnız `localhost:9876`-da dinləyir.
  - Telemetriya və bütün xarici aktiv xidmətləri söndürülüdür.
  - Server Claude-un lokal konfiqurasiyasındadır, repoya heç nə yazılmayıb.
- **Metod:**
  - generatorun funksiyaları canlı Blender-ə yükləndi (yalnız tərif hissəsi, build dövrü yox);
  - gövdə variantları yan-yana quruldu, viewport ekran şəkilləri ilə müqayisə edildi;
  - seçilən dəyişiklik generatora köçürüldü. Canlı Blender-də edilən iş GLB-yə özü düşmür; generator hər dəfə modeli yenidən qurur.
- **İlham:**
  - Auto Modellista maşınları "manqa səhifəsindən çıxmış" kimidir: düz rənglər, kəskin formalar;
  - 80-ci illərin Golf Mk2 tipi "yonulmuş", bucaqlı panellər;
  - oyun modelləşdirməsində qıvrımlarda dəstək kənarları ki, subdivision formanı kürəyə çevirməsin.
- **Tapılan səbəb:** burun və quyruq mərkəzə bir üçbucaq "yelpik" ilə bağlanırdı. Subdivision bunu günbəzə çevirirdi, sahibin "oyuncaq" dediyi "sabun parçası" quyruğu buradan gəlirdi.
- **Dəyişiklik (bütün maşınlar):**
  - burun və quyruq: içəri çəkilmiş halqa ilə düz panel, kənar crease 0.85 → 1.0;
  - dam kənarı 0.7 → 0.9, alt kənar 0.6;
  - yanların burun və quyruğa döndüyü künclər 0.7.
- **D01:** quyruq və burunda daralma azaldıldı (0.82 → 0.865 m), Golf Mk2 tipli qutu forması alındı.
- **Yoxlama:**
  - Blender review şəkilləri (4 maşın, ön və arxa);
  - oyunda üçüncü şəxs və ön görünüş;
  - ctest 6/6 (model idxalı və sızma testi daxil);
  - gövdə ~16.6–18.4 min üz (+620).

## D-074 — Bürünən bamperlər (Blender MCP prototipi)

- **Problem:** burun və quyruq çılpaq boyalı panel idi, 80-ci illər maşınlarındakı qara plastik bamperlər yox idi.
- **Prototip:** canlı Blender-də hazırlandı. Bamper gövdənin konturunu izləyən bir zolaqdır: mərkəzdən 6°–174° şüalarla gövdəyə vurulur, dəridən 3.5 sm çöldə durur, üstü və altı yuvarlaqdır.
- **Ölçülər ekran şəkilləri ilə tapıldı:**
  - **İlk cəhd:** şüa mərkəzi təkər oxuna yaxın idi, bamper yan tərəf boyu sürüşürdü.
  - **İkinci cəhd:** çox hündür idi.
  - **Son variant:** şüa mərkəzi burun/quyruqdan 0.25 m içəridədir, bamper təkər tağına qədər dönür.
- **Generatora köçürülən:**
  - `Car.bumpers(front_band, rear_band, m)`, hər maşının öz hündürlük zolaqları ilə;
  - D04 pikapında xrom, digərlərində qara plastik;
  - üzündə boz sürtünmə zolağı.
- **Bamper üzünə oturan hissələr:** `fy()` bamper zolağındakı hissələri (nömrə, intercooler, egzoz və s.) avtomatik bamperin üzünə köçürür.
- **Ölçülər:**
  - maşın başına +830 üz;
  - ctest 6/6;
  - oyunda 4 maşının ön görünüşü və üçüncü şəxs görünüşü yoxlandı (`build/sandbox_reports/anim/bp_sheet.png`).

## D-075 — Təkər tağı flare-ləri, zolaqların təkər üstündən keçməsi, audio yaddaş testi

- **Flare-lər** (canlı Blender-də iki profil yan-yana sınandı):
  - **Zavod:** boyalı "pres polad" flare. Gövdədən 3 sm çıxan yuvarlaq çiyindir, köhnə 2.2 sm-lik borunu əvəz edir.
  - **Yeni kosmetik slot `flares`:** `rally` variantı qara, 6 sm çıxan, hər birində 5 pərçimli flare-dir.
  - **Tətbiq:** `Car.arch_flare(prof)` qapalı kəsiyi təkər tağı boyu gövdənin yan səthinə yapışdırıb sürüşdürür. AZ/EN sətirləri əlavə edildi.
- **Tapılan bug:** yan zolaqlar, sürtünmə zolağı və qara sill gövdə formasından qurulurdu, təkər yuvası kəsilməmişdən əvvəlki səthə görə. Ona görə təkər yuvasından keçib təkərin üstündə asılı qalırdı (Blender şəkillərində və oyunda görünürdü).
- **Həll:** `Body.patch` yuvanın içinə düşən üzləri silir. Yuvalar `cut_arches`-da saxlanılır. Uzun zolaqlar 3 sm addımla qurulur ki, kəsik təmiz olsun. Düzəliş bütün zolaqlara tətbiq olunur.
- **Audio yaddaş testi:**
  - "son − başlanğıc" ölçüsü bir dəfəlik allocator addımları üzündən 0.04–1.02 MiB arasında sıçrayırdı və bir dəfə 1.02 ilə uğursuz oldu;
  - dövr-dövr RAM isə ±0.05 MiB-də sabit idi;
  - test D-061-dəki idxal testi kimi "son yarının medianı − ilk yarının medianı"-na keçirildi.
  - **Yoxlama:** süni 300 KB/dövr sızma ilə 3/3 uğursuz (1.27–1.59), təmiz kodda 6/6 keçir (0.00–0.05).
- **Ölçülər:**
  - maşın başına +1.2 min üz (zolaq addımları və flare-lər);
  - ctest 6/6.

## D-076 — Sürücü ayrı personaj modelidir, özəlləşdirilir, ayaqlar pedallarda

- **İstək:** sahib "sürücünü kənarda düzəlt, maşınla bir olmasın; personaj dizaynı özəlləşdirilsin" istəyib.
- **Model:**
  - `tools/generate_driver.py` bütün maşınlar üçün tək personaj faylı qurur: `assets/characters/driver/driver.glb` (48 min üz, 22 pivot, model vərəqi şəkilləri);
  - maşın faylında sürücü yoxdur, yalnız `DRIVER_ANCHOR` (omba), `DRIVER_EYE` və pedallarda `FOOT_TARGET_L/R` var;
  - maşın faylı ~87 min üzdən ~43 min üzə düşdü.
- **Bir ölçü:**
  - əvvəl oturacağın ombadan gözə hündürlüyü maşından maşına 0.64–0.80 m arasında dəyişirdi, sürücü hər maşın üçün fərqli uzunluqda qurulurdu;
  - indi personajın ölçüsü sabitdir (`HEAD_H` 0.72 m) və oturacaq personaja uyğunlaşır: `zc = max(döşəmə + 0.10, dam boşluğu − 0.72)`, real maşınlardakı kimi.
- **Ayaqlar:**
  - statik ayaqlar əvəzinə bud və baldır sümükləri qolladakı kimi IK ilə pedallara uzanır: sol ayaq debriyajda, sağ ayaq qazda;
  - pedal basılanda ayaq da basır;
  - 4 maşında çatmama 0 m-dir (omba–pedal 0.74–0.84 m, ayaq uzunluğu 0.92 m).
- **Oyunda birləşdirmə (`AttachDriver`, `CarVisual`):**
  - personajın pivotları maşının `BODY`-sinin altına köçürülür, ombalar arasındakı fərq qədər sürüşdürülür;
  - seçilmiş sifət `DRIVER_HEAD` olur, digər ikisi silinir;
  - dəri, saç və gödəkçə material ailələrinə görə rənglənir;
  - yarışda və qaraj önizləməsində eyni funksiya işləyir.
  - **Tapılan bug:** `Component_Attach` əvvəl köhnə valideyni ayırır və lokal transformu köhnəlmiş dünya matrisindən yenidən yazır. Sürüşdürmə itirdi, sürücü görünmürdü. İndi lokal transform attach-dan sonra geri yazılır.
- **Özəlləşdirmə (core TDD, 4 test):**
  - `DriverLook`: 3 sifət, 5 dəri tonu, 6 saç rəngi, 8 gödəkçə rəngi;
  - saxlamada `driver` açarı yalnız seçim standart deyilsə yazılır, köhnə saxlamalar olduğu kimi qalır;
  - AI sürücüləri `AiDriverLook(i)` ilə qarşılıqlı sadə addımlarla seçilir, 12 maşında iki eyni sürücü yoxdur;
  - qarajda 4 sətir və rəng nümunələri.
- **Ölçülər:**
  - S01 yarışında 6.06 ms (p99 7.97);
  - flow soak 6 dövr: leak gate PASS;
  - ctest 6/6, core 163/163.

## D-077 — Mühit səthlərinə əllə çəkilmiş toon fakturalar

- **İstək:** sahib "ətrafdakı detalları artır; yeni obyekt yox, düz və sadə əvəzinə daha kompleks və gözə xoş gələn stil" istəyib.
- **Qərar:** obyektlər eyni qalır. Böyük səthlər `tools/generate_toon_materials.py`-nin yaratdığı komiks fakturalarını alır.
- **Fakturalar:**
  - periodik FFT küyü və bükülmüş Voronoi ilə qurulur, təkrar olunan UV-lərdə tikiş yoxdur;
  - rənglər 3–5 tona posterizasiya olunur, cel-shade onları "boyanmış" kimi oxuyur;
  - mürəkkəb xətləri tünd, açıq hissələri boyalı kimi görünür.

  | Faktura | Ölçü | Məzmun |
  |---|---|---|
  | `toon_asphalt` | 6 m | sakit ton ləkələri, aqreqat dənələri, yağ ləkələri, çat şəbəkəsi zonaları, yol boyu iki qatran tikişi |
  | `toon_barrier` | 2 m | panel tikişi, divarın üst faskası və işıq xətti, aşağıdan qalxan çirk, şaquli yağış izləri, qırıqlar |
  | `toon_quay` | 4 m | 2×2 beton plitə, mürəkkəb tikişlər, hər plitənin öz tonu, pas və yağ ləkələri, çatlar |
  | `toon_water` | 24 m | iki tonlu dalğa zolaqları və ağ parıltılar |

- **Ölçüləri tapmaq:**
  - **Asfalt:** ilk versiyada əsas rəng 0.075 idi, cel-shade və duman altında detal tam itdi. 0.15-də uzaqda göründü, yaxında qara qaldı. 0.25-də hər yerdə oxunur, Auto Modellista-nın orta boz yolu kimi. Ləkələrin kontrastı 0.2-dən 0.11-ə endirildi, kamuflyaj kimi görünməsin.
  - **Liman:** ləkələr kamuflyaj kimi görünürdü, kontrast azaldıldı.
  - **Yağış izləri:** ştrix-kod kimi sıx idi, enləndi və seyrəldi.
  - **Su:** dalğalar çox xırda idi, böyüdüldü.
- **Ad qaydası:** materiallar adlarını saxlayır ("Toon Road", "Toon Quay", "Toon Water"). Oyun yer materiallarına kontur çəkməmə qaydasını (D-050) adla tətbiq edir, bu qayda işləməyə davam edir. Yol UV-si 6 m-ə uyğunlaşdırıldı (`TILE_TOON`).
- **Ölçülər:**
  - S01 6.06 ms (p99 7.34), dəyişməyib;
  - track GLB-ləri 2.6–3.1 MB;
  - flow soak PASS;
  - ctest 6/6;
  - S01 gündüz, L01 gün batımı və gecə, kokpit ekran şəkilləri ilə yoxlandı (`build/sandbox_reports/anim/env3_sheet.png`, `env4_s01.png`).

## D-078 — Kamera, əl əyləci animasiyası, Wolf Among Us üslublu sürücülər, hava planı

- **Kamera:**
  - **Problem:** sürətdə görüş bucağı 62°-dən 84°-yə qədər genişlənirdi. Maşın ekranda 200 km/s-da dayanmış vəziyyətin 72%-nə qədər kiçilirdi; sahibin "kamera uzaqlaşır" hissi buradan idi. Bunu ölçən test yazıldı və əvvəlcə uğursuz olduğu görüldü.
  - **Yeni dəyərlər:** FOV 62–72°, sürətlə 0.042°/km/s; məsafə sabit 5.0 m (6.0 idi, sürətlə artmır); hündürlük 1.75 m (2.3 idi).
  - **Nəticə:** maşın 200 km/s-da da ölçüsünün 85%-ni saxlayır. Sürət hissini sürət xətləri və döngədə yellənmə verir.
- **Əl əyləci:**
  - boşluq basılanda sağ əl sükandan əl əyləcinin qulpuna gedir, sonra qolu çəkir;
  - qol əl çatandan sonra qalxır (`hb_k`), əl qolla birlikdə hərəkət edir;
  - qulpun yeri maşın modelindəki `HANDBRAKE_GRIP` nöqtəsindən gəlir;
  - test açarı: `--features=handbrake`.
- **Sürücülər (Wolf Among Us üslubu, Blender MCP ilə canlı yoxlanaraq):**
  - **Forma:** uzun və dar kəllə, kvadrat çənə küncləri və enli irəli çənə, ağır qaş sümüyü, dərin göz yuvaları, yüksək yanaq sümükləri.
  - **Gözlər:** kiçik, badam formalı, ağır qapaqla yarı örtülü, qapaq qırışı; qaşlar aşağı və qaşqabaqlı.
  - **Mürəkkəb:** burun–ağız qırışı, göz kənarında qırışlar, yanaq ştrixləri, iki qaşarası qırış; birinci sürücüdə Bigby-dəki kimi alında çapıq.
  - **Kölgə tonları:** yanaq altı paz, burun yanı, gicgah, daha tünd qısa tük.
  - **Saç:** başa yapışan, ucları yuvarlaq enli tellər; yalnız iki tel önə düşür, yan tellər yanağın önünə keçmir.
  - **Tapılan bug:** üz xətləri sadə ellipsoidə görə yerləşdirilirdi, profildə üzdən çıxıb çubuq kimi görünürdü. İndi `on_face` heykəltəraşlıq səthinin normalı boyu oturdur.
  - **Dəri:** standart ton isti orta tondur (0.86, 0.56, 0.38). Ən açıq ton D-069 tənzimi altında ağa çevrilirdi.
- **Hava şəraiti:** plan `design/weather_plan.md`-də yazıldı. Yağış, fırtına, duman; görüntü, fizika (tutuş), səs, ölçülən qapılar və ardıcıllıq.
- **Testlər:** ctest 6/6, core 164/164.

## D-079 — Salonda işləyən funksiyalar: lampalar, shift-LED, qəza işıqları, salon işığı, günlük

Sahib (2026-10-03): "maşının iç interyerini daha detallı və yaxşı et. Daha çox funksiyalar olsun".

- **Panel lampaları** (`car_cabin.py`, `WARN_*` pivotları):
  - **Lampalar:** sol və sağ göstərici oxları (göstəricilər və qəza işıqları ilə yanıb-sönür), əl əyləci (qırmızı, ling qalxanda yanır), mühərrik (sarı, HP 30%-dən aşağı olanda yanıb-sönür), uzaq işıq (göy), qəza (qırmızı).
  - **Yer:** binnacle-in maili ön dodağı. İlk variantda lampalar panelin aşağısındaydı və sükan qovşağı onları gözdən tam gizlədirdi (ekran şəkli ilə yoxlandı). Göz hündürlüyündən panelin yalnız G.z+0.078-dən yuxarısı görünür; dodaq isə sürücüyə və yuxarıya ~34° baxır.
- **Shift-LED:** 10 LED eyni dodaqda. Dövrlər `shift_up_rpm`-in 55%-dən 100%-ə qədər yaşıl → sarı → qırmızı dolur, 97%-dən sonra "indi ötür" deyə yanıb-sönür. Stil `shift_led`-i atırsa (D01, D04), yalnız sönük yuvalar qalır.
- **Qəza işıqları (J):**
  - Düymə `HAZARD_BUTTON` pivotudur; sağ əl uzanıb basır (`kHazard`), düymə içəri gedir.
  - Hər iki göstərici, kokpit oxları və tıqqıltı səsi birlikdə işləyir; ekranda toast göstərilir.
- **Salon işığı (K):**
  - Sağ əl tavandakı açara uzanır (`kDome`); `DOME_LIT` görünür və BODY-yə bağlı point light yanır.
  - **Ölçmə:** intensivlik 6 ilə gözdən 0.3 m məsafədə bütün ekran ağarırdı. 0.5-ə (radius 1.6 m) endirildi; gecə şəkli təmizdir.
- **Günlük (U):**
  - Sol əl uzanır (`kVisor`), `SUN_VISOR_L` ön menteşəsi üzərində enir.
  - **Bucaq:** 77°-də yolun dörddə birini örtürdü. 0.6 rad yarım enmədə parıltını kəsir, yol görünür (3 bucaq ekran şəkli ilə yoxlandı).
  - Günlükdə güzgü və qayış var; sağ günlük statikdir.
- **Bindinqlər:** `hazard` J, `dome` K, `visor` U (`DefaultBindings`, remap siyahısı), AZ/EN sətirlər. Autotest flag-ları: `hazard`, `dome`, `visor`, `presshazard`, `pressdome`, `pressvisor`.
- **Ölçmələr (S01 gecə, D02, 12 maşın, 20 s):**
  - Kadr vaxtı funksiyalarsız 6.23 ms, hazard + dome ilə 6.42 ms (+0.19 ms, point light-dan). p99 8.39 ms.
  - Flow soak (6 dövr) leak gate PASS: CPU və VRAM artımı 0 MiB.
  - ctest 6/6.

## D-080 — Müasir maşınlardan ilham: yeni modifikasiyalar

Sahib: "modern maşınların bəzilərindən də ilham al və maşınları və onların modifikasiya imkanlarını genişlət". Bu qərar modifikasiyaları genişləndirir; yeni müasir maşın ayrıca qərardır (D-081).

- **Yeni slotlar** (`Car.modern_mods`, hər maşında):
  - **Dam:** yoxdur / hava qəbuledicisi (GR Yaris, WRX tipli; boyalı gövdə və tünd ağız) / ralli bagajı (relslər, 4 tirə, ehtiyat təkər, 4 lampa podu; podların linzası `lamp` materialıdır, faralarla yanır) / akula üzgəci antenası.
  - **İşıq imzası:** zavod / LED DRL (hər faranın altında bıçaq və xarici kənarda qarmaq, müasir L forması) / LED zolaq (burun boyu bütün eni tutan zolaq və bıçaqlar). Material `toon_drl_NoInk` həmişə yanır.
- **Mövcud slotlara əlavələr:**
  - **Təkər tağları:** "Geniş gövdə". Boyalı, enli bolt-on tağlar, hər tağda 9 açıq bolt; boltlar tağın öz xarici səthinə oturur.
  - **Disklər:** Y spik (6 çəngəlli forged), Aero (EV tipli düz örtük, 5 tünd ləçək), Qoşa spik (7 cüt spik, cilalı pilləli lip, 14 montaj boltu).
- **Tapılan və düzəldilən xətalar:**
  - **DRL oyunda görünmürdü.** Blender render-də görünürdü; part oyunda saxlanılırdı (`RACER_PARTS_DEBUG` logu) və AABB düzgün idi (z 2.027–2.044, burnun önündə).
    - **Səbəb:** 1.4–1.8 sm hündürlüyündəki zolaq yarış məsafəsində 3–4 px edir, gövdənin 2.2 px outline mürəkkəbi onu tam örtürdü.
    - **Düzəliş:** zolaqlar 3 sm-ə qaldırıldı (real DRL bıçaqları 2–3 sm-dir); indi görünür. Mürəkkəb çərçivəsi LED korpusu kimi oxunur.
  - **Widebody boltları:** ilk variantda bəzi boltlar kapotun kənarına düşürdü (tağdan fərqli radiusdan hesablanırdı); düzəldildi.
- **Təhlükəsizlik:** skriptli menyu testi bitmiş yarışın nəticəsini artıq `career.json`-a yazmır (`!scripted`). Əvvəl yalnız qaraj dəyişiklikləri qorunurdu.
- **Ölçmələr:**
  - **Model:** hər maşın ~55k üz (əvvəl ~43k). Seçilməyən variantlar yükləmədə silinir. Cooked `D01.wiscene` 9.2 → 12.0 MB.
  - **Kadr vaxtı:** S01 gecə, 12 maşın: 6.33 ms (əvvəl 6.23 ms, səs-küy həddində); p99 8.23 ms.
  - **Flow soak:** leak gate PASS.
  - **Testlər:** ctest 6/6; core 164/164. Yeni yoxlama: hər maşının kataloqunda yeni slot və variantlar var. Kataloqdan `fin` silinəndə test qırmızı oldu, bərpadan sonra yaşıl.
  - **Menyu skripti:** yeni sətirlər "Dam" və "İşıq imzası" dəyişir, qaraj önizləməsi dərhal yenilənir.

## D-081 — Yeni müasir maşın D05 "Sprint"

İlham: indiki homologasiya hetçbekləri (GR Yaris, Civic Type R, i30 N tipli). Real marka yoxdur; orijinal dizayndır.

- **Dizayn:**
  - **Gövdə:** AWD turbo hetçbek-kupe; alçaq iti burun, nazik "əsəbi" faralar, böyük alçaq hoteyk barmaqlıq, maili kupe damı, enli arxa omba.
  - **Rəng həlli:** qara iki tonlu dam, tam enli arxa işıq zolağı, yan zolaq.
  - **Zavod modları:** dam hava qəbuledicisi, LED DRL və Y spik disklər. Yeni `mod_stock` ilə hər maşın öz zavod kitini seçə bilir.
- **Salon (`STYLES['D05']`):**
  - Qara suede, qırmızı tikiş.
  - **Rəqəmsal panel (`cluster='digital'`):** qara ekran, mavi parlayan halqalar, cyan bölgülər.
  - Dashboard üstündə sürücüyə əyilmiş üzən sensor ekran: gecə rejimli xəritə, kəhrəba marşrut, media plitələri.
  - Shift-LED-lər var; köhnə dövrün əşyaları (kaset və s.) atılıb.
  - İlk variantda panel ekran rəngində açıq görünürdü, gecə xəritəsi də ağarırdı; ikisi də tündləşdirildi.
- **Rol (ölçülən):**
  - Ən sürətli start: 0–100 7.85 s (digərləri 7.92–8.27 s).
  - Qarşılığında ən az möhkəmlik (85 HP) və aşağı yan tutuş (skidpad 1.08 g, D01-də 1.25 g).
  - Testə yeni yoxlama əlavə olundu: "sprint ən tez 100-ə çıxır".
- **Tənzimləmə ölçmələri (S01 AI dövrəsi, start daxil):**
  - İlk variant (205 Nm) dominant idi: 0–100 6.76 s (sinif həddi 7 s), 182.8 km/s, dövrə 59.2 s; test qırmızı.
  - Fırlanma momenti 205 → 178–188 Nm arası, ötürmələr qısaldıldı. Dövrə vaxtı mühərrikə həssas deyil: dəyişikliklər 60.1 s-də qaldı.
  - Yan tutuş 0.92–0.94 aralığında da 60.1 s verdi; 0.90-da 64.3 s-ə sıçradı (həddi effekt, AI döngü itirir).
  - **Seçim:** 185 Nm, 1330 kq, yan tutuş 0.94. Dövrə 60.1 s (digərləri 61.8–62.7 s), yayılma 4.3%, 6% qapısının içində.
  - **Qəbul olunan güzəşt:** təmiz dövrədə ~2 s üstünlük var, amma döyüşdə ən kövrək maşındır.
- **Oyuna qoşulma:** CMake cook, `own_cars`, RacePath roster (AI sahəsində D01–D05 növbə ilə), test roster-i oyunla eyni. Seçilmə statistikası: S01 sahəsi "D04 D05 D01 D02 D03 …".
- **Yan təsir — E06 testi:**
  - **Səbəb:** 5 maşınlıq roster-də ovçu yeri (8-ci) D04-dən D03-ə keçdi və tək qaçış 6 zərbə verdi (hədd 7).
  - **Ölçmə (eyni 3 pickup düzümü):** köhnə roster D04 11/6/5, yeni roster D03 9/3/5. Köhnə "ölçülüb 10" ən yaxşı halın nəticəsi idi, stabil orta deyildi.
  - **Yeni qapı (D-059 nümunəsi ilə):** 5 qaçış. "7 zərbə" bonus məqsəddir, ona görə ən yaxşı qaçış ≥ 7 olmalı, orta ≥ 5. Ölçülən: 9/3/5/3/8, cəmi 28 (qapı 25).
- **Yan təsir — import leak testi (D01.glb, 60 import) flaky idi:**
  - **Ölçmə:** 16.3, 13.9, −0.3, 2.2 MiB. ~55k üzlü modeldə allocator-un birdəfəlik high-water addımı 3-cü və 4-cü rüb arasına düşürdü.
  - **Yeni qayda:** artım = son iki rüb addımının kiçiyi (sızma hər rübdə böyüyür, birdəfəlik addım isə bir rübdə olur).
  - **Qapının özü yoxlandı (`RACER_INJECT_LEAK_KIB`):** təmiz 4/4 PASS (0.17, 0.16, −0.29, 3.10). 512 KiB/import 2/2 FAIL (6.8, 8.5). 1 MiB/import 2/2 FAIL (15.7, 14.5).
  - **Həssaslıq həddi (açıq yazılır):** 300 KiB/import 60 importda tutulmur (3.7, 2.1 MiB; azad edilmiş səhifələr yenidən istifadə olunur). Köhnə qayda da bunu tutmurdu.
- **Ölçmələr:**
  - Kadr vaxtı: S01 gecə, D05 kokpit, 12 maşın, 6.12 ms; p99 8.18 ms.
  - Flow soak leak gate PASS.
  - Testlər: ctest 6/6, core 164/164, handling 37/37.

## D-082 — Konteyner, anbar və kranlara əllə çəkilmiş toon boya

Sahib: "digər stilləri də daha da zənginləşdir" (yeni obyekt yox, mövcud səthlərə daha zəngin stil). D-077-nin yolu ilə davam edir.

- **Yeni fakturalar** (`generate_toon_materials.py`; hamısı kəsiksiz təkrarlanır, rüsum seam-ləri `np.roll` ilə bükülür):
  - `toon_box_<5 rəng>`: konteyner boyası. Posterize solğunluq, yağış zolaqları, tünd mürəkkəb kənarlı boya qopuqları və altından axan pas. 512 px; ~1000 konteyner instansı eyni xəritələri paylaşır.
  - `toon_clad_teal`, `toon_clad_grey`: anbar örtüyü və damı, daha az pas.
  - `toon_crane`: 2 m-lik polad lövhələr, qaynaq tikişi, kənarlarda pərçim sırası, pərçimlərin altından pas axını, aşağı üçdə birdə komiks ştrixi.
  - `toon_emblems`: 2×2 atlas. Ağ lövhə üzərində abstrakt gəmiçilik emblemləri (dalğa, ulduz, şevron, romb). Ad və real şirkət nişanı yoxdur.
- **Tənzimləmə:** ilk variant kamuflyaj kimi çox qarışıq idi. Ləkə amplitudu yarıya endirildi, qopuqlar 6% → 2.5%, pas axını 9% → 5%, zolaqlar daha seyrək.
- **Generator:**
  - Eyni material adları saxlanıldı, ona görə oyunun mürəkkəb qaydaları dəyişmədi.
  - Büzməli lövhələrin UV-si yox idi; artıq `world_uv` alırlar.
  - Hər konteynerin iki uzun tərəfinə 2.4×1.0 m emblem lövhəsi qoyulur (rəngə görə emblem).
- **Ölçmələr:**
  - Yaxın planlar Blender-də render edildi. Oyunda S01 və L01 ekran şəkilləri: emblemlər uzaqdan rəng vurğusu kimi oxunur, faktura yaxında görünür.
  - **Kadr vaxtı:** S01 gecə kokpit 6.41 ms (D-079 bazası 6.23 ms), p99 9.17 ms. L01 gündüz 6.06 ms.
  - **RAM:** S01 yarışında 1495 MiB. D-079 zamanı 1400 MiB idi; artım D-080-in böyük modelləri, D05 və yeni fakturaların cəmidir, ayrıca bölünmədi.
  - **Testlər:** flow soak leak gate PASS, ctest 6/6.

## D-083 — Hava şəraiti: yağış, fırtına, duman (plan mərhələ 1–2)

`design/weather_plan.md`-in ardıcıllığı ilə. Sahibin ilk baxışdan rəyi: "bu yağış effekti çox çox pisdi, daha yaxşı və realist elə". Aşağıdakı görüntü hissəsi həmin rəydən sonra yenidən quruldu.

- **Core (`racer/Weather.h`, TDD):**
  - `WeatherKind` növləri: clear, overcast, rain, storm, fog. `WeatherState` növ və 0..1 intensivlik saxlayır.
  - `EffectsOf` tutuş, nəmlik, görmə məsafəsi və küləyi verir: yağış tutuşu ×0.85, fırtına ×0.8 + külək 9 m/s, duman 140 m.
  - `BlendEffects` smoothstep keçidi edir. `ApplyWeather` yalnız şin tutuşunu dəyişir, əyləc yolu ondan uzanır.
  - **Event JSON:** isteğe bağlı `"weather"`/`"weather_intensity"`; köhnə fayllar aydın qalır, səhv dəyər rədd olunur.
  - **Testlər:** 5 yeni core testi və 1 event testi. Testin özündəki xəta tapıldı: parser nəticəni üstünə yazmır, əlavə edir; testdə yeni obyekt istifadə olundu.
- **Fizika:**
  - `RaceSetup.weather`: `RaceSession` bütün maşınlara eyni `ApplyWeather` tətbiq edir. AI-nın döngə və əyləc büdcəsi eyni tutuşla vurulur, yoxsa quru yola görə əyləc edib yaş yolda sürüşərdi. Autotest avtopilotu da eyni qaydadadır.
  - **Ölçmələr (handling testi):**
    - 100–0 əyləc yolu: D02 quru 40.4 m, yağış 47.4 m (+17%), fırtına 50.2 m (+24%); D05 +14% / +19%. Plan +15% istəyirdi.
    - 12 maşın S01-də: aydın 12/12, yağış 11/12, fırtına 12/12 bitirdi; insane step 0.
    - Solo Hard AI dövrəsi: 62.4 / 62.6 / 63.1 s. S01 sürətin həll etdiyi geniş döngələrdən ibarətdir, ona görə tutuş fərqi azdır.
    - İlk variantda maşın 0-ın sahədəki dövrəsi müqayisə edilirdi. Bu yanlış ölçü idi: döyüş zərbələri nəticəni həll edir, fırtına aydından "tez" çıxdı (60.7 vs 70.6 s).
- **Görüntü — ilk variant pis idi (ekran şəkilləri ilə ölçüldü):**
  - Wicked-in daxili yağışı yuxarıdan baxan dərinlik xəritəsi ilə toqquşur. Ona görə buludlarda və qapıda ləkə-ləkə sıçrayırdı, mürəkkəb pass-ı da hər damcını qara çəkirdi.
  - Duman (2.5/görmə) gündüz bütün kadrı ağardırdı. Parlaq yol fara işığını ağ ləkəyə çevirirdi.
- **Görüntü — yenidən qurulan variant:**
  - **Öz yağışımız:**
    - Kameraya bağlı 32×32 m müstəvidən 18 000 damcı/san (fırtına) düşür, 13 m/s sürətlə; yumşaq nöqtə faktura sürətlə 0.6 m-lik zolağa uzanır.
    - İşıqsızdır: işıqlı variantda hər lampa minlərlə damcıda üst-üstə düşüb ağ ləkə əmələ gətirirdi. Saata görə rəng alır.
    - Kameranın üstündə külək tərəfə sürüşdürülmüş 5×5 m deşik var. Bunsuz damcılar linzanın yanında ağ lövhə kimi görünür, kokpitdə salonun içinə düşürdü. Sürüşmə emitter-in lokal sürətindən hesablanır (mühərrik sürəti emitter matrisi ilə çevirir).
    - Sıxlıq ölçüldü: 7 500/san və 48 m-lik sahə çiskin kimi görünürdü.
  - **Təkər sıçrantısı:** hər maşının arxasında sürətlə artan duman (25 km/s-dan yuxarı), yumşaq "puff" fakturası.
  - **Səma və işıq:** səma və buludlar boz-tünd olur, günəş zəifləyir, ağır havada günəş/ay/ulduzlar gizlənir. Duman daha yumşaqdır (1.1/görmə).
  - **Yaş yol:** yol və rıhtım materialları tündləşir, bir az parıldayır (pürüzlülük ≥ 0.65).
  - **İşıqlar:** ağır havada faralar və küçə lampaları yanır; gündüz faralar gecənin üçdə biri gücündədir. Gecə "lampa parıltısı ×1.8" gündüz yağışında artıq tətbiq olunmur; salon ekranlarını ağ dumana çevirirdi.
  - Faranın gücü bütün hallarda 45 → 30-a endi: dəstədə qabaqdakı maşının damı ağ diskə dönürdü.
  - **Fırtına:** hər 5–12 s-dən bir ikiqat ildırım çaxması (ekspozisiya və ambient). Bu yalnız təqdimatdır, simulyasiyaya aid deyil.
  - **SSR yoxlandı:** fırtınada +0.3 ms, əksetmə toon asfaltda demək olar görünmür; söndürüldü.
- **Kontent:** R01_E07 (L01 gecə, "Container brawl") yağışlı oldu. Test qarmağı: `--weather=rain`, `--weather=storm:0.5`.
- **Ölçmələr:**
  - Kadr vaxtı, S01 gecə kokpit: aydın 6.38 ms, fırtına 6.70 ms (SSR-siz), plan qapısı +1 ms. L01 gecə yağış 6.06 ms.
  - Son yoxlama: ctest 6/6, flow soak leak gate PASS. Fırtına S01 gecə 20 s: 6.66 ms, p99 8.76 ms, insane step 0.
  - **Qalan mərhələlər (plan):** yağış, külək və ildırım səsləri; menyuda hava seçimi; fırtınada yan külək itələməsi (fizikada hələ yoxdur).

## D-084 — Yağış düzəlişləri, gölməçələr və yaş yolun sürüşkənliyi

Sahib: "oyun açılandan sonra yağış yağmasın, açılanda yağsın; yağış yerdən keçir; asfaltda gölməçələr olsun və yol yağışın şiddətinə görə sürüşkən olsun, amma oynanış məntiqini nəzərə al".

- **Yağış yenidən quruldu:**
  - Damcılar kameraya bağlı 36×14×36 m həcm qutusunda, kameradan 2 m irəlidən başlayaraq doğulur.
  - Yarış başlayanda qutu bir dəfə doldurulur (`Burst`), yəni ilk kadrdan yağır.
  - Mühərrikin rain-blocker xəritəsi damcını yolda, damda və maşında dayandırıb 3 sm-lik sıçrantıya çevirir; bundan əvvəl damcılar yerdən keçirdi. Damın altında damcı doğulmur, salonun içi də quru qalır. Bu xəritə yalnız `rain_amount > 0` olanda çəkilir, ona görə 1e-6 verilir; mühərrikin öz emitter-i saniyədə ~1 damcıya enir.
  - **Tapılan iki xəta (ölçüldü):**
    - Mühərrik emitter sürətini qutu miqyası daxil bütün dünya matrisi ilə çevirir; damcılar 230 m/s ilə ekran boyu xətlər kimi uçurdu. Sürət indi miqyasa bölünərək verilir.
    - `SIMPLE` shader debug shader-dir: qeyri-şəffaf boz rəng çəkir, faktura istifadə etmir; ağ kvadratlar bundan idi. `SOFT` istifadə olunur.
- **Gölməçələr (core, TDD):**
  - `MakePuddles`: hər km-də 48 gölməçə (tam yağışda), seed ilə deterministik, start düzündə yoxdur.
  - Yolun xarici yarısındadır (yanal 45–85%), radiusu kənarı orta xəttə çatmayacaq qədərdir. Yarış xətti quru qalır, ötmək üçün kənara çıxmaq riskdir. Ölçmə: orta xəttin 766 nöqtəsindən 0-ı gölməçəyə toxunur.
  - `PuddleGrip`: təkər gölməçənin mərkəzinə yaxınlaşdıqca tutuş ×0.8-ə enir (yüngül akvaplan).
  - `RaceSession` hər təkərin kontakt nöqtəsinə görə hesablayır (`SetWheelGrip`) və bu bütün maşınlara eyni tətbiq olunur.
- **Gölməçələrin görüntüsü:** səmanı əks etdirən açıq mavi-boz su, əks zolağı, dalğa halqaları, mürəkkəb kənar, ətrafda tünd yaş asfalt.
  - **Ölçülən dalanlar:** koddan qurulan mesh materialı tətbiq olunmadı; decal toon yolda çəkilmir; gizli şablon kubu paylaşan instanslar heç nə çəkmədi; 1.5 sm hündürlükdəki gölməçə vizual yolun altında qalırdı (6 sm-ə qaldırıldı); tünd su yolla qarışıb itirdi.
  - **İşləyən həll:** hər gölməçə öz kubudur (döyüş effektləri kimi).
- **Sürüşkənlik:** yağış tutuşu ×0.85, fırtına ×0.8 (D-083); gölməçə üstəlik yerli olaraq ×0.8-ə qədər.
  - **Ölçmə (skidpad, D02):** aydın 1.06 g, yağış 0.90 g, fırtına 0.84 g.
- **Test dəyişikliyi:** "yağışda dövrə daha yavaş" yoxlaması S01-də səs-küy həddində idi (−0.5%…+1.4%); trassın dövrəsini düzlüklər həll edir. Əvəzinə tutuş birbaşa skidpad ilə yoxlanır. Dövrə üçün yalnız "fırtına aydından yavaşdır" saxlanıldı, dövrə vaxtı isə üç maşının medianıdır.
- **Ölçmələr:** fırtına S01 gecə kokpit 6.65 ms (aydın 6.38 ms); flow soak PASS; ctest 6/6, core 177/177, handling 38/38.

## D-085 — Qəza zədələri: əzilmələr, tüstü; maşınların bir-birinə keçməsi

Sahib: "maşın divara dəyəndə qabağı əzilsin, əzilmələrin dərəcəsinə nəzarət et; canı azalanda bir az tüstülənsin, amma divara dəyən kimi yox, canı 10–20%-ə enəndə; maşınlar bəzən bir-birinin içinə keçir".

- **Core (`racer/Damage.h`, TDD):**
  - 4 zona var: ön, arxa, sol, sağ. Zonanı maşının öz çərçivəsindəki sürət dəyişikliyi Δv seçir; yan sürtünmə sürəti azaltmasa da Δv-də görünür.
  - **Dərəcə nəzarəti:**
    - 15 km/s-dan zəif toxunuş iz qoymur.
    - 90 km/s-lıq zərbə təzə paneli birdən həddə çatdırır.
    - Hər yeni zərbə daha az əlavə edir (əzilmiş metal müqavimət göstərir).
    - Ən dərin əzilmə 28 sm-dir; 20 sm uzaqdan az görünürdü.
  - `DeformPoint` nöqtəni əzilən tərəfdən içəri itələyir:
    - Uclarda zona 0.8 m, yanlarda 0.45 m-dir.
    - 0.22 m-lik dəyər-küyü ilə kələ-kötür əzilmə naxışı verilir.
    - Kapot və baqaj bir az yuxarı əyilir.
  - Bu yalnız görüntüdür; can və idarə döyüş qaydalarından gəlir.
  - **Tüstü:** canın 15%-indən aşağı başlayır, 4%-də ən çoxdur (yenə də nazik duman).
- **Oyunda:**
  - `RaceSession` hər maşının əzilməsini izləyir. Recovery teleportu zərbə sayılmır (impact tarixçəsi sıfırlanır).
  - `RacePath` BODY və taxılmış hissələrin orijinal təpələrini öz çərçivələrində saxlayır. Əzilmə 0.02-dən çox artanda mesh yenidən deformasiya edilib GPU-ya yüklənir; bu yarış boyu bir neçə dəfədir, hər kadr deyil.
  - Tüstü emitter-i kapotun altındadır.
  - Test qarmaqları: `RACER_DENT=front,right`, `RACER_LOW_HP`. Autotest hesabatında `dents_all_cars` sahəsi var.
  - **Ölçmə:** 40 s-lik S01 yarışında 6 maşında real əzilmə yarandı (ön 0.23–0.51, bir sağ 0.32). Kadr vaxtı dəyişmədi (6.07 ms).
- **Bir-birinə keçmə — tapılan səbəb:** toqquşma qutusu maşın uzunluğunun 86%-idir. Burnun və quyruğun son ~30 sm-də toqquşma yoxdur, ona görə burun-quyruq vəziyyətində iki maşın 60 sm-ə qədər bir-birinə keçir.
  - **Qutunu uzatmaq ölçülüb rədd edildi** (15 solo 2 dövrəlik qaçış, 5 maşın × 3 çətinlik):
    - 86%: orta 117.8 s, yavaş qaçış 1;
    - 90%: 117.3 s, 1;
    - 92%: 119.5 s, 2.
    - 90%-də isə E06 ovçusu 5 qaçışın hamısında az zərbə vurdu (28 → 22) və D05 S01-də hadisə yaşadı.
  - Qutu dəyişdirilmədi. **Düzgün həll** yalnız maşın–maşın arasında işləyən burun/quyruq "bamper" toqquşdurucusudur (divarlara təsir etməz). Bu açıq iş kimi qalır.

## D-086 — Yol kənarındakı torpağın zənginləşdirilməsi

Sahib: "ətrafdakı kənar yollar düz bej rəngli bir şeydir, oraları da zənginləşdir".

- **`toon_quay` yenidən çəkildi, təkrarlanma 4 m → 16 m (2048 px):**
  - 2 m-lik 8×8 beton plitə, mürəkkəb tikişlərlə.
  - İki tünd yenilənmiş asfalt yaması.
  - Sarı yard xətləri, ştrixlənmiş qadağan qutusu, ağ yer işarələri.
  - Drenaj barmaqlıqları, təkər izləri, pas və yağ ləkələri.
  - Böyük miqyaslı ton dəyişməsi.
- **`toon_apron`:** bariyerin arxasında 6 m-lik köhnə asfalt zolağı. Bariyer tərəfdə alaq ləkələri, xarici kənarda çınqıl var. Materialın adında "Road" olduğu üçün mürəkkəb çəkilmir.
- **Yoxlama:** Blender yuxarı plan renderi; S01 və L01 ekran şəkilləri. Kadr vaxtı L01 gündüz 6.06 ms.

## D-087 — Sürücü üzləri: araşdırmanın ilk tövsiyələri (R1–R4, R7)

Sahib: "karakter dizaynları çox pisdir, düzəltmək üçün araşdırma et". Hesabat: `docs/research/character-design.md` (primary mənbələrlə).

- **R1 — gözlər:** göz ağı, bəbək və parıltı kəllənin 1–4 mm içində idi, gözlər yumulmuş `> <` kimi görünürdü. Göz indi dəridən 4 mm qabarıqdır; kirpik xətti qalınlaşdırıldı (2.7 → 3.6 mm).
- **R2 — müstəvi üz normalları:** Guilty Gear Xrd-in (GDC 2015) əllə tənzimlənmiş normalları kimi.
  - Hər kəllə təpəsi eyni şəkildə heykəlləşdirilmiş 14×8-lik kobud başın ən yaxın üzünün normalını alır (85/15 qarışıq, `normals_split_custom_set_from_vertices`).
  - Toon shader-in işıq/kölgə pilləsi indi alın, yanaq və çənə müstəviləri boyunca düşür.
  - Qonşu müstəvilər arasındakı bucaq 37°-lik ink həddindən kiçikdir, ona görə əlavə xətt çəkilmir.
- **R3 — rəngli kölgələr:** boz tonlar (0.38) əzik kimi oxunurdu. Dəri kölgələri indi isti qırmızı-qəhvəyidir (0.6, 0.38, 0.36), Xrd-in kölgə rəngləri və Valve-in TF2 məqaləsinə uyğun.
  - Üz domenində (hər poliqona bir ton) kölgələr pilləvari "piksel" göründü (ekran şəkli ilə). Ona görə təpə domeni saxlanıldı: kənar ~5 mm-lik təmiz keçiddir.
- **R4 — xətlər:** alt göz qapağı, göz altı torbaları, qaz ayağı qırışları, yanaq ştrixləri, burun yanı xətti və göz qapağı qırışı silindi. Bunlar yarışda 0.3–0.9 px idi, yaxın planda isə üzün ən ağır izlərinə çevrilirdi.
  - Qalanlar: qalın kirpik xətti, qaşlar, nazolabial qırış (2.4 mm), ağız, dodaq qırışı, yalnız v0-da bir alın qırışı.
- **R7 — baş +8%:** boyun pivotu ətrafında; ANSUR II ölçülərinə görə baş ~4% kiçik idi. Kokpitdə oyunçunun öz başı gizli qalır, pəncərə görünüşündə dama dəymir.
- **Ölçmələr:** oyundaxili yaxın plan, pəncərə və kokpit ekran şəkilləri (`review/d087_faces.png`). Kadr vaxtı 6.13 ms. Flow soak PASS. ctest 6/6.
- **Qalanlar (hesabata görə):** R5 (model vərəqlərinin oyundakı kimi göstərilməsi)

## D-088 — Maşınlararası bamperlər (bir-birinin içinə keçmənin həlli)

- **Ölçülən problem:**
  - 20 maşınlıq S01 dövrəsində maşınlar 0.87 m-ə qədər bir-birinə keçirdi; 0.3 m-dən dərin batma 17 781 cüt-addım idi.
  - Fizika qutuları da ~0.5 m üst-üstə düşürdü.
  - Fizika qutusunu uzatmaq AI-nın divarlara yanaşmasını pozur (D-085), ona görə o yol rədd edilmişdi.
- **Həll (`racer/Contact.h`, TDD):** iki fırlanmış düzbucaqlı üçün SAT testi. `RaceSession::ApplyBumpers` hər addım maşın cütlərinin tam vizual izlərini (uzunluq × 0.94 en) yoxlayır.
  - Üst-üstə düşmə varsa, normal boyunca yaxınlaşma sürəti sıfırlanır: qeyri-elastik zərbə, impuls saxlanılır.
  - Ayrılma sürəti `min(dərinlik × 6, 1.5) m/s`-ə qaldırılır. Bu hədəf sürətdir, hər addım üstünə əlavə edilmir. İlk variant hər addım əlavə edirdi və 120 Hz-də bu böyüyürdü.
  - Yalnız maşın cütlərinə təsir edir: divarlar və AI xətləri dəyişmir. Bir maşın digərinin üstündədirsə (atlama, >1.2 m), toqquşma yoxlanmır.
- **Tapılan xəta:** ilk addımda telemetriya hələ dolmamışdı, bütün maşınlar (0,0)-da görünür və hamısı itələnirdi. İlk 2 addım atlanır.
- **Ölçmə (eyni 20 maşınlıq qaçış, açıq və bağlı):**
  - Dərin batma 17 781 → 165 cüt-addım (−99%); 20/20 bitirdi, insane step 0.
  - 0.6 m/s-lik yumşaq itələmə 627 cüt-addım qoydu, E06/E08 nəticələri eyni idi; ona görə 1.5 seçildi.
- **E06 məqsədi 7 → 5 zərbə:**
  - Bamperlər açıq olanda AI ovçusu 5 qaçışda 21 zərbə vurdu (5/3/5/3/5), bağlı olanda 28.
  - Fərq iki qaçışdadır: bamperlər bağlı olanda yalnız bu qaçışlarda hədəf məhv edildi və zərbələr məhz orada azaldı (9 → 5, 8 → 5).
  - **Ehtimal olunan səbəb (ayrıca ölçülmədi):** hədəflərin yavaş gedən dəstəsi bir-birinə batırdı, bir Pulse bir neçəsini birdən vururdu. Yəni köhnə "7" bu xətanın sayəsində əlçatan idi.
  - Pickup radiusu 4 → 6 m heç nəyi dəyişmədi (eyni 21 zərbə); geri qaytarıldı.
  - R01.json-da məqsəd 5-dir, AZ/EN mətnləri yeniləndi. Test qapısı: ən yaxşı qaçış ≥ 5, cəmi ≥ 20.
- **Ölçmələr:** ctest 6/6 (handling 39/39, core 180/180). Flow soak PASS. Kadr vaxtı 6.24 ms. Diaqnostika: `RACER_NO_BUMPERS`, `RACER_BUMPER_LOG`., R6 (saç: 5–9 böyük topa, əyri saç xətti), boyun və çiyin proporsiyaları, hər sürücüyə fərqli siluet.

## M2-də qalanlar

- **Harbor:** L01 hazırdır (D-039); əks istiqamət yoxlaması (plan 2.11) və L01 üçün minimap HUD-da yoxdur.
- **Rejimlər:** Destruction, Duel (D-040) və ustalıq tapşırıqları (D-042) hazırdır.
- **Audio:** real səslərlə dinləmə turu (menyu səsləri və səs parametrləri D-038-də hazırdır).
- **UI:** hazırdır — parametrlər, remap, AZ font testi, əlçatanlıq, qaraj paneli (D-038, D-045–D-047). Lokal real maşınların fizikası D-048-də.
- **Döyüş hissiyyatı:** S01 E05-də 3 dövrədə ~240 istifadə — oynanışla tənzimlənməlidir; döyüş parametrlərinin JSON-a köçürülməsi.
- **Hazır:** S01 Dock Loop, D01–D04, menyu/event/nəticə axını, yaddaş.

## D-090 — Yol kənarında "chunk kimi" itən yerlər (açıq məsələ)

Sahib: "maşın gedəndə yol ətrafındakı yerləri örtür və itirir"; sonra: "tam kölgə deyildi, chunk deyirlər, onun kimi bir şey, yoxa çıxır".

- **Birinci fərziyyə — alçaq günəşin süpürülən kölgəsi (11°):** 0.25 s-lik kadr ardıcıllığında quay torpağı tündləşirdi.
  - Günəş 24°-yə qaldırıldı. Sahib bunun kölgə olmadığını dedi, günəş 11°-yə qaytarıldı.
- **İkinci fərziyyə — occlusion culling:** Wicked-in GPU sorğuları bir kadr gec cavab verir, kamera hərəkət edəndə gizlənmiş obyektlər gec "pop" edir.
  - Söndürüldü, çünki pulsuzdur: S01 6.08 → 6.09 ms, L01 6.06 → 6.07 ms (12 maşın, 20 s).
  - **Sübut deyil:** kamera hərəkətsiz olanda (geri sayım, 15 statik kadr cütü) açıq və bağlı arasında fon dəyişikliyində fərq yoxdur (0.33% vs 0.22%, bulud və işıq səs-küyü).
- **Sahibin ekran şəkilləri (2 ədəd):**
  - Start grid-də ağ zolaq xətləri görünür, altında isə asfalt yerinə quay fakturası var: yalnız yol lenti çəkilməyib.
  - Start qapısının və maşının üstündə şüşə kimi "qırıq" üçbucaqlar, maşınlardan qalxan qara iynələr var.
- **Təkrarlama cəhdləri (cari build-də heç biri təkrarlamadı):**
  - sahibin dəqiq garage quraşdırması (D02, GT qanad, splitter, diffuzor, karbon kapot, stage 3 mühərrik, turbo);
  - modlar + məcburi əzilmələr;
  - menyudan karyera yarışı;
  - 30 s-lik ardıcıllıqlar, occlusion culling açıq və bağlı.
- **Ən ehtimal olunan səbəb:** oyun mən mühiti yenidən cook edərkən (D-086) başladılıb və yarımçıq yazılmış `.wiscene` arxivini oxuyub. Bu, həm yol lentinin olmamasını, həm də zibil üçbucaqları izah edir.
- **Qoruma:** `import_check --cook` indi əvvəl `<out>.tmp`-yə yazır, sonra `MoveFileEx(REPLACE_EXISTING)` ilə bir addımda əvəzləyir. İşləyən oyun artıq yarımçıq fayl görə bilməz.
- **Status:** yenidən görünərsə sahib bildirəcək. Occlusion culling söndürülmüş qalır (pulsuzdur və bir pop-in növünü aradan qaldırır).
- **Alətlər:** `--capture-every=<s>` (kadr ardıcıllığı), `RACER_OCCLUSION=1` (köhnə davranış), `RACER_SUN_ELEV`, `RACER_CASCADES`.

## D-091 — Köhnə maşın modellərinin təmizlənməsi

Sahib: "köhnə yüklədiyimiz maşın modellərini seçəndə buglar yaranır" → "köhnə maşın modellərini sil, elə təmiz".

- **Silinməyib, layihədən kənara köçürülüb** (2.1 GB-lıq qovluq zibil qutusuna sığmaya bilərdi və Windows onu həmişəlik silərdi). Yer: `C:\Users\Sanan\Documents\Gamee_local_cars_backup_2026-10-04`. Köçürülənlər:
  - 25 real-brend lokal maşın (L01–L26, `assets/local_only`) və onların cooked faylları;
  - istifadə olunmayan köhnə model qovluqları: `modern_coupes`, `modern_germans`, `vehicles_realistic` (real brend adları ilə), toon-dan əvvəlki `assets/vehicles`;
  - onların generatorları və `local_cars_import.py`;
  - köhnə `T01.wiscene`.
- **Koddan çıxarılanlar:**
  - `FindLocalCars`;
  - `RACER_LOCAL_CARS_DIR`;
  - `RacePath::Options::local_cars`;
  - lokal maşın sahəsinin seçilmə məntiqi.
- Köhnə save-də L maşını seçilmiş idisə, menyu D01-ə qayıdır.
- **Yoxlama:** build təmizdir; menyu skripti ilə yarış başladı (0 uğursuz); flow soak PASS; ctest 6/6.
- **Qeyd:** import leak testi bir qaçışda 4.31 MiB göstərdi (hədd 4.0); növbəti 4 qaçışda −0.5…0.5 MiB oldu. Bu flaky qapıdır, sonra həll ediləcək.

## D-092 — Avtomatik performans qapısı və yağışın 600 MB-lıq sızıntısı

Sahib: "birdə optimizasiyaya da nəzarət edərsən". Bundan sonra performans əllə arabir ölçülmür, hər ctest qaçışında yoxlanır.

- **`--perf-budget=orta_ms,p99_ms,ram_mib`:** yarış autotest-i bitəndə kadr vaxtının ortası, 99-cu persentili və RAM büdcəni keçərsə, qaçış uğursuz olur və səbəb hesabatın `error` sahəsinə yazılır.
- **ctest-də 3 ən ağır səhnə** (büdcə 8.5 ms / 14 ms / 2400 MiB):
  - `perf_S01_storm_cockpit` (gecə fırtınası, kokpit);
  - `perf_L01_20cars_rain` (20 maşın, gecə yağışı);
  - `perf_S01_day_chase`.
- **Qapı ilk qaçışda real problem tapdı:** L01-də 20 maşın yağışda 2501 MiB.
  - **Səbəb:** öz yağışımız üçün mühərrikin rain-blocker xəritəsi lazımdır, o isə yalnız `rain_amount > 0` olanda çəkilir. Biz 1e-6 veririk. Mühərrik isə bu miqdardan asılı olmayaraq daxili yağışı üçün 1 000 000 hissəciklik bufer ayırırdı.
  - **Engine patch P-004** (`engine/patches/0004-small-rain-pool.patch`): `rain_amount < 0.001` olanda pool 1024 hissəcikdir.
  - **Nəticə:** S01 fırtına 2183 → 1896 MiB, L01 20 maşın yağış 2501 → 2234 MiB. Yağış dayanır və yerdə sıçrayır (ekran şəkli ilə yoxlandı).
- **Ölçmələr (qapı keçdi):**
  - S01 fırtına: orta 7.67 ms, p99 10.1 ms;
  - L01 20 maşın yağış: 7.17 / 10.4 ms;
  - S01 gündüz: 6.06 / 8.5 ms;
  - ctest 9/9.

## D-093 — Modifikasiya yoxlaması və eyni yerə taxılan hissələrin konflikti

Sahib: "modifikasiya edəndə buglar yaranır".

- **Sistemli yoxlama:**
  - Menyu skripti ilə qarajda D02-nin 11 slotunun bütün 29 variantı seçildi və hər birinin önizləmə şəkli çəkildi.
  - D01–D05 hər biri ekstremal mod dəsti ilə yarışda yan və ön kameradan çəkildi (widebody, rack, light bar, GT qanad, splitter, ətəklər, diffuzor, minigun, vent kapot, split disklər, race turbo).
- **Tapılan bug:** dam bagajı (rack) və dam silahları (minigun, raket podu) damın ortasında eyni yerə taxılır. İkisi birlikdə seçiləndə silah bagajın içində itirdi.
- **Həll (`Customization.cpp`, oyunlardakı "uyğun gəlmir" qaydası):**
  - Konflikt cütlərinin cədvəli var.
  - Yeni seçim qalib gəlir: bagaj seçilərsə dam silahı çıxarılır; dam silahı seçilərsə bagaj çıxarılır.
  - Köhnə save-də hər ikisi varsa, silah saxlanılır, bagaj çıxarılır.
  - Arxa silahlar (yağ bakı, tikanlar) bagajla birlikdə qala bilər.
  - Test əvvəl qırmızı oldu, düzəlişdən sonra yaşıl.
- **Qeyd:** test zamanı skriptlərdə iki alət səhvi tapıldı:
  - `--screenshot` yalnız `--report` ilə birlikdə yazılır;
  - `while read` döngüsündə oyun stdin-i oxuyurdu (`</dev/null` lazımdır).
- **Ölçmələr:** ctest 9/9 (core 181/181).
- **Açıq:** sahibin gördüyü başqa bir modifikasiya bugu varsa, ekran şəkli ilə dəqiqləşdiriləcək.
- **Performans qeydi (D-093-dən sonra):** üç perf səhnəsinin hamısı bərabər ~25% yavaşladı:
  - S01 gündüz: orta 6.06 → 7.60 ms, p99 8.5 → 11.2 ms;
  - L01 yağış: p99 10.4 → 13.7–15.4 ms. 14 ms büdcəsi 3 qaçışdan 2-sində keçildi.
  - D-092 ilə bu ölçmə arasında render koduna dəyən dəyişiklik yoxdur (yalnız core-da konflikt qaydası, lokal maşın kodunun silinməsi, cook alətinin yazma qaydası).
  - Fərq: bu dəfə sahibin Blender 5.2-si açıq idi (531 MB, viewport GPU-nu paylaşır).
  - **Nəticə:** fon yükü ehtimal olunur, amma sübut deyil. Blender bağlı olanda yenidən ölçüləcək. Gerçək reqressiya çıxarsa, büdcəni artırmaq yox, səbəbi düzəltmək lazımdır.

## D-094 — ProofPlay (hakaton qatı): oyundan yazma, yoxlama, müqayisə, RAM

Plan: `docs/hackathon/PROOFPLAY_PLAN.md`. Kod `proofplay/`-dadır, oyuna yeganə toxunuş `RacePath`-dəki yazıcıdır.

- **Oyundan yazma (P5):** `RacePath` hər fizika addımının girişini, reset düyməsini və qurulumu yazır: grid sırası, oyunçunun yeri (`total/2`), qaraj hissələri, silah, hava, qaydalar. Fayl `proofplay/sessions/<ad>/<vaxt>.json`-a yazılır, ad `PROOFPLAY_PLAYER`-dən gəlir, `PROOFPLAY_OFF=1` yazmanı söndürür.
  - Ölçmə: oyunda `--autotest` ilə 3 dövrəlik S01 yarışı headless replay ilə bit-bit eyni çıxdı (`MATCH`, 22 627 addım).
- **Yoxlama (P6):** nəticə iddiasının bütün sahələri (tick, vaxt, yer, finiş, vəziyyət hash-i) replay ilə müqayisə olunur.
  - Saxtalaşdırma testi (`proofplay/tests/tamper_test.py`): 10 saxta versiyanın 10-u rədd edildi, həqiqi sessiya qəbul edildi.
  - İlk qaçışda 2 "keçən" saxta versiya testin öz səhvi idi: dəyişiklik yarışı dəyişmirdi (pəncərə onsuz da tam qaz idi, hissə açarı `perf_engine` əvəzinə `engine` yazılmışdı).
  - **Açıq:** botla yazılmış girişlər yoxlamadan keçir. Davranış metrikası (sükan dönmələri/s) avtopilot (0.07) ilə offline bot (0.10) arasında fərq qoymur. Hədd üçün insan yarışı lazımdır.
- **Müqayisə (P7, `proofplay/bench/benchmark.py`):** 14 bot yarışı, 12-sində əkilmiş tormoz qüsuru.
  - Qayda məşqçisi: 32 məsləhətin 12-si işləyir (37.5%), 19-u pisləşdirir və ya qəzaya salır.
  - ProofPlay: 31/31 işləyir (konstruksiyaya görə), orta qazanc 1.05 s.
  - Əkilmiş qüsur 1-ci yerdə: hər ikisi 7/12. Amma `late_brake` etiketi yanlışdır: gec tormoz bu botu sürətləndirir (D-094-dən əvvəlki ölçmə: P3 → P1). Ona görə yalnız `early_brake` halları etibarlı test sayılır: ProofPlay 5/6.
  - **Açıq:** bəzi qazanclar şübhəlidir (bir döngədə 5.4–5.5 s). Ehtimal ki, döyüş təsadüfüdür (vuruşdan yayınma). Davamlılıq yoxlaması lazımdır (qonşu məsafələr də qazandırmalıdır).
  - LLM məşqçisi Azure OpenAI açarı gələndə qoşulacaq. Azure for Students-də Claude deploy olunmur (Microsoft sənədi).
- **RAM:** headless alət DX12 cihazı yaratmır, `PROOFPLAY_GPU=1` onu geri qaytarır. İdeal profil keşlənir, what-if qaçışları addım nümunələrini saxlamır.
  - Ölçmə (`PROOFPLAY_MEM=1`): replay 125 → 39 MB pik working set (private 228 → 85 MB), nəticə yenə `MATCH`, bir az da sürətli (23.5x → 26.3x).
  - Tam analiz: 42 MB, nəticələr əvvəlki ilə eynidir.
- **LLM məşqçisi (D-094-ün davamı):** Azure OpenAI gpt-5-mini (Azure for Students, Claude orada deploy olunmur). Eyni 14 yarış üçün 34 məsləhət verdi.
  - 26-sı işləyir (76.5%), 7-si pisləşdirir və ya qəzaya salır (21%), orta qazanc 0.63 s. ProofPlay-də bu rəqəmlər 0% və 1.05 s-dir.
  - 14 çağırış ≈ 26K token ≈ $0.04.
  - LLM çağırışları paralel getdi (6 eyni vaxtda) və nəticələr keşləndi: 14 yarış 30 s-ə hazır oldu (əvvəl ~5 dəq).
  - Benchmark-ın standart paralelliyi 6-ya endirildi: 14 eyni vaxtlı qaçış ~0.5 GB yer tuturdu və CPU-nu tam doldururdu.
- **Dashboard vizualları:** vektor xəritənin yerinə oyunun öz səhnəsinin ortoqrafik yuxarıdan render-i qoyuldu (`tools/render_proofplay_assets.py`, Blender 4.5). Koordinatları dəqiqdir, yarış xətləri asfaltla üst-üstə düşür (ekran şəkli ilə yoxlanıldı).
  - Maşınlar yalnız stok hissələrlə, yuxarıdan və 3/4 bucaqdan render olundu.
  - Xəritə aktiv sübutun döngəsinə yaxınlaşır. "SƏN" və "DÜZƏLİŞ" maşınları eyni anda hərəkət edir.
- **İki mərhələli analiz (sahibin istəyi):** oyun indi sessiya faylına öz nəticə statistikasını da yazır (`StatsTracker`: yer, vaxt, dövrələr, isabət, zərər, maks/orta sürət, dağılmalar, reset). Server əvvəlcə bu statistikadan **dərhal** "ilkin baxış" çıxarır. Bunlar yoxlanılmamış qeydlərdir və ekranda belə də işarələnir. Sonra replay analizi gəlir (sübutlu məsləhətlər) və ilkin baxışın yerini tutur.
  - Ölçmə: sessiya qovluğa düşdükdən 5 s sonra ilkin mərhələ ekranda idi. Statistika ilə yazılmış sessiya replay-də yenə `MATCH` verdi.
- **Dashboard dizaynı:** monoxrom (yalnız ağ, qara və boz çalarlar), bento şəbəkə, tras foto və maşın şəkilləri boz rəngdə. Döngənin itkisi rənglə yox, tündlüklə göstərilir. Dərin analiz gedərkən yükləmə skeleti görünür.
- **İlk real insan yarışı (sahib, klaviatura, karyera yarışı, 12 maşın, qaraj hissələri):** replay bit-bit eyni çıxdı (`MATCH`). Nəticə: P12, dövrələr 79.8 / 56.4 / 54.6 s. Sübutlu məsləhət çıxmadı (0).
  - Səbəb: insan döngələrin 8/9-da tormoza toxunmur (qazı buraxır) və 2–3-cü dövrələrdə botlardan sürətlidir.
  - Həll 1: what-if indi üç hərəkət növünü sınayır: tormoz, qazı buraxmaq və tam qazla keçilən döngədə əlavə tormoz. Yenə də vaxt qazandıran dəyişiklik tapılmadı. Bu dürüst nəticədir.
  - Həll 2: **öz ən yaxşı dövrə ilə müqayisə** (20 m-lik hissələr, səbəb etiketləri ilə). Tapılan itkilər: 1-ci dövrə 0–820 m −23.2 s (dayandı, start), 2-ci dövrə 660–980 m −5.8 s (trasdan çıxdı), 3-cü dövrə 1160–1520 m −5.8 s.
  - Anti-cheat siqnalı: girişin dəyişdiyi addımların payı insanda 2.4%, botlarda 91–95% idi. Sükan insanda 3 fərqli dəyər alırdı, botlarda minlərlə. Bu n=1 ölçmədir.
  - Analiz müddəti 12 maşında 200 s-dir. Sürətləndirmək açıq məsələdir.
- **Statistika uyğunluğu:** oyunun öz nəticə ekranındakı rəqəmlər replay ölçmələri ilə müqayisə edildi. 2 yarışda 12/12 dəqiq uyğun gəldi: yer, vaxt, dövrə vaxtları, silah istifadəsi, isabət, zərər. Bu yoxlama indi hər hesabatda avtomatik aparılır və "Yoxlama" mərhələsində göstərilir.
- **Tras dəyişəndə:** `LoadTrack` trası `content/tracks/<id>_*.json` faylından tapır, oyun da onu belə tapır. Döngələr trasın həndəsəsindən avtomatik çıxarılır. Yeni tras üçün xəritə fotosunu server Blender ilə özü render edir.
  - Ölçmə: L01 Container Run trasında bot yarışı `MATCH` verdi, 14 döngə avtomatik tapıldı, sübutlar alındı, nəticə yoxlanıldı.
- **Məsləhətlər genişləndirildi:** 3 sübut limiti götürüldü (hər döngə), itki hissələri 6-ya qədər. Yeni `proofplay/dashboard/advice.py` sübutları, itkiləri, dövrələr arası qeyri-sabit döngələri və statistikanı vaxt təsirinə görə sıralanmış bir siyahıya çevirir. Hər bəndin mənbəyi, sübutu, ətraflı izahı və "nə etməli" addımı var. Mətn şablondur, ölçülmüş rəqəmlər üzərində qurulur, LLM uydurması yoxdur.
- **Xəritə:** start/finiş xətti, hərəkət istiqaməti oxları və yaxınlaşanda bütün trası göstərən kiçik xəritə əlavə olundu.

## D-095 — ProofPlay: analizin sürəti, interaktiv xəritə, ingilis dili, dizayn skill-ləri

- **Sürət.** Analiz iki keçidə bölündü: `--fast` keçidi (profil, itkilər və məsləhətlər, what-if yoxdur) və tam keçid (sübutlar).
  - Ölçmə (12 maşınlıq insan yarışı): fast keçid 8.6 s çəkdi və tam keçidlə eyni atribut və itkiləri verdi. Tam keçid 200 s-dən 70 s-ə düşdü.
  - Sürətlənmə 12 worker prosesi ilə alındı (`proofplay whatif` alt-əmri).
  - Thread-lərlə paralelləşdirmə sınandı və rədd edildi: bir prosesdəki thread-lər Jolt-un ortaq job pool-unu bölüşür və bir-birini bloklayır. 10 dəqiqədən sonra da bitməmişdi, ardıcıl iş isə 94 s idi.
  - Server mərhələləri: statistika (dərhal) → fast (~9 s) → tam.
- **Dashboard yenidən quruldu** (impeccable + taste-skill qaydaları, Operate rejimi):
  - İsti monoxrom tokenlər; şrift Schibsted Grotesk və IBM Plex Mono. Geist detektorun "overused" xəbərdarlığına görə dəyişdirildi.
  - İnteraktiv xəritə: təkərlə zoom, sürükləyərək hərəkət, hover tooltip, hər tapıntı üçün nömrəli pin, siyahı ilə iki tərəfli sinxronizasiya, klaviatura (↑/↓/Esc).
  - Tras real eni ilə asfalt lent kimi çəkilir: kənar xətləri, kerblər, start zolağı, 500 m nişanları və döngə nişanları. Tras fotosu 260 m kənar boşluqla yenidən render olundu.
  - Bütün mətnlər ingiliscədir (UI, `advice.py`, server qeydləri, atribut adları).
  - impeccable detektoru: əvvəl 3 tapıntı var idi (kontrast 4.1:1, boş `img src`, overused font), düzəlişdən sonra 0.
- **Skill-lər** (sahibin icazəsi ilə): `impeccable` (engine binarı GitHub release-dən sha256 yoxlaması ilə endirildi) və `taste-skill` dəsti (13 skill) `~/.claude/skills`-ə quraşdırıldı.

## D-096 — ProofPlay: PRODUCT.md və dashboard strukturunun yenidən qurulması

- **PRODUCT.md** (layihə kökü, impeccable product-schema 1) yaradıldı: istifadəçilər, məqsəd, mövqe, iş konteksti, imkanlar, brend öhdəlikləri, sübutlar və prinsiplər. Sahib sual raundu olmadan işlədiyi üçün təsdiqlənməmiş bəndlər **[inferred]** ilə işarələndi.
- **Critique** (impeccable, tək kontekstli, sub-agent işə salınmadı) bu tapıntıları verdi:
  - [P1] Səhifə üç bərabər kartla açılırdı, əsas cavab ("növbəti yarışda nəyi dəyiş") aşağıda qalırdı.
  - [P1] Tapıntı kartları çox sıx idi.
  - [P2] "Re-simulyasiya ilə sübut" və reytinqin nə olduğu izah edilmirdi.
  - [P2] Qaranlıq rejim yox idi, ikonlar əllə çəkilmiş SVG idi, aralıqlar en-dash ilə yazılırdı.
- **Yeni struktur:**
  1. Başlıq: sürücü və yarış seçimi, status, açıq/qaranlıq rejim düyməsi.
  2. 4 addımlı analiz zolağı: Results screen → Replay analysis → Proven fixes → Verified.
  3. "Debrief": ən böyük tapıntı başlıq kimi göstərilir, yanında izahın ilk iki cümləsi, əsas rəqəmlər, maşın renderi və reytinq.
  4. Xəritə və tapıntılar yan-yana. Yalnız seçilmiş kart açılıb izahı və sübutu göstərir.
  5. Sürücü profili: radar, meter-li bacarıqlar, dövrələr.
  6. First look, reytinq tarixçəsi, liderlər cədvəli.
  7. "How ProofPlay works": record → replay → prove.
- **Qaranlıq rejim:** `prefers-color-scheme` + `data-theme`. Seçim try/catch içində localStorage-da saxlanır. Canvas rəngləri CSS tokenlərindən oxunur.
- **İkonlar:** Phosphor (unpkg `@phosphor-icons/web@2.1.1`); əllə çəkilmiş SVG ikonlar silindi.
- **Mətn:** UI və `advice.py`-də em-dash və en-dash sayı 0.
- **taste-skill məhdudiyyəti:** onun §13-ü dashboard-ları əhatə etmir, ona görə yalnız tətbiq olunan qaydalar götürüldü (dash yoxdur, kitabxana ikonları, eyebrow yoxdur, reduced-motion, loading/empty/error halları).
- **Ölçülər:**
  - impeccable detektoru 1 advisory (flat type hierarchy) verdi; başlıqlar 16/20 px edildikdən sonra 0.
  - 375 px enində üfüqi scroll yoxdur (scrollWidth = innerWidth).
  - Mobil görünüşdə başlıq sticky deyil.
  - Qaranlıq rejimdə xəritənin asfalt lenti və foto parlaqlığı artırıldı.

## D-097 — ProofPlay: dözümlü sübutlar, ən yaxşı kombinasiya planı, sessiya düyməsi, başqa PC-də işləmə

**Alqoritm** (`proofplay/src/Analyze.cpp`):

- **Dözümlü seçim.** Əvvəl ən böyük qazancı verən sürüşmə seçilirdi. İndi bir düzəliş yalnız qonşu sürüşmələr də təhlükəsizdirsə (qəza yoxdur, real itki yoxdur) "robust" sayılır. Robust düzəliş daha böyük, amma "bıçaq ağzında" olan qazancdan üstün tutulur. Hesabata `robust` və `window_lo_m..window_hi_m` əlavə olunur ("10-30 m arası da işləyir"). Bunun üçün əlavə simulyasiya lazım deyil, artıq hesablanmış 11 sürüşmədən çıxarılır.
  - Ölçü: Bot_Qusurlu-da 3/3 düzəliş robust. L01-də 14-dən 13-ü, AutoTest-də 3-dən 2-si.
  - AutoTest-in 1-ci döngəsi "exact point only" kimi göstərilir.
- **Birgə plan.** Düzəlişlər bir-birinə təsir edir. Ölçüldü:
  - Bot_Normal-da 3 düzəliş birlikdə 0.69 s **yavaşladır**;
  - L01-də 14 düzəlişin hamısı birlikdə qəzaya aparır (ayrı-ayrılıqda cəmi 9.13 s göstərirdi);
  - Bot_Qusurlu-da birlikdə 0.97 s qazandırır (ayrı-ayrılıqda cəmi 1.40 s).

  Buna görə plan qazanca görə ilk 2..5 düzəlişi və "hamısını" bir dövrədə ayrı-ayrı simulyasiya edir və ən yaxşı kombinasiyanı seçir.
  - Bütün variantlar həmin dövrənin bütün düzəlişlərindən çıxan ortaq bazis run ilə müqayisə olunur.
  - Run-lar paralel worker proseslərində gedir.
  - Plan bir dövrə daxilində qurulur. Bütün yarış boyu ölçmədə bazis run 2499-cu metrdə ilişdi: orijinal yarışda orada reset olmuşdu, playback isə reset-i atır.
  - "Line keeper" (sükanla oyunçunun öz xəttinə qayıtmaq, kp 0.08, kd 0.12) yalnız planda istifadə olunur. İşarə dəyişikliyi bazis nəticəsini dəyişmədi, yəni əsas problem reset idi.
- **Paralel plan.** Ardıcıl plan Bot_Qusurlu-nun tam analizini 97 s-ə çıxarmışdı, paralel versiyada 61 s-dir.
- **Worker fayllarının toqquşması.** İki analiz eyni sessiyada `whatifN.json` fayllarını bölüşürdü və JSON parse xətası verirdi. Fayl adına PID əlavə olundu. Server və uploader `.whatif` fayllarına baxmır.

**İzahlar:** `advice.py` sadə ingilis dilinə keçirildi ("you braked too early", "We re-ran your exact race in the game with one change").

**Sayt:**

- Bir accent rəng (zümrüd = "sübut olunub"), tier rəngləri (Elite, Gold, Silver, Bronze), bacarıq zolaqlarının rəngi qiymətə görə dəyişir, xəritədə pinlər mənbəyə görə rənglənir.
- "Your plan for the next race" bölməsi: işarələnən siyahı (localStorage) və Copy düyməsi.
- Əvvəlki yarışla müqayisə: eyni trasda reytinq və vaxt fərqi.
- Fayl yükləmə: düymə ilə və ya sürüşdür-burax.
- Detector 0 tapıntı verdi.

**Sessiya modeli (sahibin tələbi):** sayt oyun açmır. Oyunçu oyunu özü açır, `games.json` reyestri işləyən dəstəklənən oyunu tanıyır, "Start session" isə sessiya müddətində oyunun inbox qovluğuna (`Oyuncu`) düşən yarışları sürücünün adına köçürür.

- Endpoint-lər yalnız local sorğulara cavab verir və `X-ProofPlay` başlığı tələb edir (CSRF qoruması).
- `claim()` testi: köhnə fayl yerində qalır, sessiya zamanı yazılan fayl sürücüyə keçir, sessiya olmayanda fayl yerində qalır.

**Başqa PC:**

- `proofplay/package.py` 17.1 MB-lıq paket yaradır: exe, app-local VC++ runtime, content, dashboard, uploader və `ProofPlay.pyw` launcher.
- `ContentDir` əvvəlcə `PROOFPLAY_CONTENT`, sonra exe-nin yanındakı `content/` qovluğuna baxır. Oyun `PROOFPLAY_SESSIONS` dəyişənini oxuyur.
- Paket ayrı qovluqda test olundu: replay MATCH verdi.
- Uploader ilə göndərilən yarış verified oldu (OVR 75). Saxta fayl və `../../evil` adı rədd edildi.
- Sənəd: `docs/hackathon/PROOFPLAY_DEPLOY.md`.

## D-098 — ProofPlay: sessiya düyməsinin düzəlişi, iki növbəli analiz, metal tier-lər, fon animasiyası

- **"Start session" işləmirdi.** Ölçüldü: oyun açıq olmayanda server 409 qaytarırdı ("Open your game first"), dialoqdakı düymə də deaktiv qalırdı. Amma təbii ardıcıllıq "əvvəl düymə, sonra oyun" da ola bilər. İndi sessiya istənilən vaxt başlayır. Status "waiting for the game" göstərir, oyun açılanda isə onun adını göstərir. Yoxlama: kliklə başladıldı, status "Recording Bot_Qusurlu, waiting for the game, 0 races" oldu, Stop düyməsi göründü və işlədi.
- **İki növbə.** Əvvəl yeni yarış başqa yarışın 5 dəqiqəlik tam analizinin arxasında gözləyirdi (müşahidə: L01 "busy" ikən). İndi `watcher` yeni faylların nəticə ekranı və replay mərhələsini dərhal, ən yenisindən başlayaraq edir (~5–15 s). `prover` isə sübutları ayrıca thread-də, bir-bir edir. Uğursuz fayllar hər saniyə yenidən sınanmır.
- **Tier-lər metal rəngindədir** (sahibin tələbi):
  - Gold qızılı, Silver gümüşü, Bronze bürünc, Elite zümrüd;
  - hər biri yüngül metal parıltısı və daxili işıqlandırma ilə;
  - reytinq kartında tier rəngində haşiyə;
  - liderlər cədvəlində eyni nişanlar.
- **Fon animasiyası.** İki çox zəif işıq ləkəsi (38 s və 46 s, `alternate`) və SVG yarış xətləri yavaşca "çəkilir" (70 s və 95 s). Hamısı `pointer-events: none` və `z-index: -1` ilə qurulub, `prefers-reduced-motion` seçilibsə dayanır. Detector diaqonal zolaqları "repeating-gradient stripes" kimi işarələdi, onlar yarış xətti motivi ilə əvəz olundu və detector 0 tapıntı verdi.
- **Kiçik düzəlişlər:**
  - Panellərdə yumşaq kölgə.
  - Sessiya gedəndə statusda qırmızı REC nöqtəsi.
  - Məsafə aralıqlarında qırılmayan tire (mobil başlıq "860-" yerindən bölünürdü).
  - Köhnə hesabatlarda (`plan.corners` yoxdursa) plan siyahısı sübut olunmuş düzəlişləri göstərir.
- **Yoxlama:** 1440 px enində açıq və qaranlıq rejim, 375 px mobil (scrollWidth = innerWidth), liderlər cədvəli.

## D-099 — ProofPlay: sessiyada yarışın "düşməməsi"

Sahibin şikayəti: "Start session"-da "Omar" yazıb oynadı, Stop etdi, amma yarış sayta düşmədi və analiz olunmadı. Araşdırma:

1. **Oyun fayl yazmamışdı.** 18:41-dən sonra heç bir yarış faylı yaranmamışdı. Recorder yarışı yalnız `RacePhase::Finished` olanda, yəni bütün maşınlar bitirəndə saxlayırdı. Oyunçu finişə çatıb rəqiblər bitməmiş menyuya çıxanda yarış itirdi. 18:41-dəki yarış P12/12 idi, ona görə orada problem çıxmamışdı.
   - **Düzəliş:** recorder oyunçu finişə çatan an saxlayır.
   - **Test:** `racer_sandbox --autotest=260 --opponents=3` P3/4 ilə bitdi, fayl həmin an yazıldı, `proofplay replay` MATCH verdi (hash 9150657677830030411).
2. **Yeni ad ilk yarış gələnə qədər görünmürdü.** Sayt köhnə sürücünün yarışını göstərməyə davam edirdi.
   - `/api/players` indi sessiya sürücüsünü dərhal qaytarır.
   - Sayt "Waiting for X's first race" vəziyyətini göstərir.
   - Səhifə açılanda aktiv sessiyanın sürücüsü seçilir, yeni yarış gələndə sayt ona keçir.
3. **Sessiya yalnız yaddaşda saxlanılırdı.** Server yenidən başlayanda itirdi. İndi `DATA/session.json` faylında saxlanılır. Test: sessiya başladıldı, server yenidən başladıldı, sessiya qaldı.
4. **Kiçik düzəlişlər:**
   - Server log-u line-buffered edildi (preview log boş qalırdı).
   - Worker-lərin müvəqqəti faylları sistem temp qovluğuna köçürüldü (dayandırılmış analizlər sessiya qovluqlarında boş `.whatif` faylları qoyurdu).
   - Sübut olunmuş düzəlişlər 0.05 s həddindən aşağı olsa da siyahıda qalır. Plan "2 düzəliş" deyirdi, amma siyahıda 1 göstərirdi.
   - `.plan[hidden]` CSS düzəlişi: tam analiz bitməmişkən boş plan paneli görünürdü.

**Uçdan-uca test (E2E_Test, sonra silindi):** sessiya açıldı, server yenidən başladıldı, oyunun inbox qovluğuna fayl qoyuldu. Fayl sürücüyə keçdi, ilk nəticə 2 s-də göründü, replay mərhələsi tez gəldi (verified, OVR 75). Tam analiz Bot_Qusurlu-da 46 s çəkdi.
