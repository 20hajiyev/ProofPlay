# M1 — Sürüş: qərarlar və ölçmələr

Bu fayl `DECISIONS.md`-nin davamıdır (M1 mərhələsi). Bütün rəqəmlər bu kompüterdə ölçülüb:
i5-13450HX, RTX 4050 Laptop, 15.7 GB RAM, Release build. Planın referens avadanlığı (GTX 1660 / RX 5500 XT) burada yoxdur.

## D-006 — Avtoritativ fizika addımı yalnız `PhysicsStepper`-dədir

- Wicked-in öz akkumulyatoru neytrallaşdırılıb: `SetAccuracy(1)` + hər çağırışda `dt = 2 × step`. Nəticədə hər `Step()` dəqiq bir Jolt addımıdır, float qalığı toplanmır (test: sərbəst düşmə 120 addımda −10.000 m/s).
- Addımlar arasında fizika **söndürülür** (`SetEnabled(false)`), simulyasiya bayrağı isə yandırılır. Bu cütlük ölçmə ilə seçilib: simulyasiya söndürülü olanda Wicked `Scene::Update` zamanı body-ləri köhnə transformlarına teleport edirdi. Maşın 97 km/saat sürətlə başlanğıc nöqtəsində "mıxlanırdı"; istifadəçinin "WASD işləmir" müşahidəsi də bu idi. Reqressiya testi: `render loop: Scene::Update between steps never pulls the car back`.
- `SimClock::SetPaused` yalnız vəziyyət dəyişəndə gözləyən vaxtı atır. Əvvəl hər kadr çağırılanda 6 ms-lik kadrlar heç vaxt addıma çatmırdı (30 s-də 43 addım). Test əlavə olunub.

## D-007 — Maşın tuning-i JSON-dan Jolt-a, Wicked default-ları əvəz edilir

`content/vehicles/D01.json` → `VehicleRuntime::ApplyTuning()` (P-001 vasitəsilə):
mühərrik torku və əyrisi, min/max RPM, **mühərrik ətaləti**, ötürücülər, dəyişmə vaxtı, diferensial (RWD üçün arxaya köçürülür), əyləclər, səth × şin tutuşu.

| Tapıntı | Ölçmə | Qərar |
|---|---|---|
| Tuning səssizcə tətbiq olunmurdu | pik RPM 6000 (Jolt default) | `Prepare()` `dt > 0` göndərir; `ApplyTuning()` `[[nodiscard]] bool` qaytarır |
| Səthlər əyləcə təsir etmirdi | asfalt/yaş/çınqıl hamısı 19.4 m | Wicked-in 10x şin impulsu Jolt-un fiziki 1x modeli ilə əvəz edildi → 26.5 / 32.1 / 39.8 m |
| 0-100 km/saat 15.2 s | tutuş ×3 və AWD dəyişmir, tork ×2 kömək edir → ətalət | `engine_inertia_kgm2` (D01: 0.12, Jolt default 0.5 = 1-ci ötürücüdə ~925 kq əlavə effektiv kütlə) və `shift_time_s` (0.25) sahələri |
| Divara sıxılan maşın 60° aşırdı | təkərlər qutudan 0.2 m kənarda idi | Kollizion qutusu ən azı təkərlərin xarici kənarı qədər enlidir; indi min up.y 0.997 |

`brake_torque_nm` **hər təkər üçündür** (Jolt semantikası).

## D-008 — D01 başlanğıc handling nəticələri (hədəf deyil, ölçmə)

| Ölçü | Nəticə |
|---|---|
| 0-100 km/saat | 8.9 s |
| Maksimal sürət | 173.5 km/saat (plan: D sinfi 170) |
| 100-0 əyləc | 42.5 m, yan sürüşmə 0.09 m |
| 80-0 asfalt / yaş / çınqıl | 26.5 / 32.1 / 39.8 m |
| Ramp (2 m) | 1.32 s havada, dik enir |
| 8 sm bordür, 110 km/saat | sürət itkisi 2%, dik qalır |
| Divar boyunca sürtünmə | açıq yoldan heç vaxt sürətli deyil (27 vs 158 km/saat) |
| 30/60/144 fps render | 900-cü addımda mövqe bit-bit eynidir |
| 20 maşın fizika | 0.56 ms / 60 fps kadr (büdcə 4 ms) |

Divara sürtünmə cəzası çox sərt ola bilər (27 km/saat). Bu, oynanışla (M2) yoxlanacaq; indiki test yalnız "üstünlük yoxdur" şərtini təmin edir.

## D-009 — Model pipeline: GLB → `.wiscene` cook

`import_check --cook` GLB-ni Wicked-in native səhnə formatına çevirir; CMake `cook_content` hədəfi mənbə GLB dəyişəndə yenidən cook edir. Oyun `.wiscene` yükləyir, staging səhnəsinə yükləyib `Merge` edir (canlı səhnəyə birbaşa serialize sandbox-u dondururdu).

**Açıq optimizasiya:** D01 LOD0 93 ayrı mesh-dir → 20 maşın ≈ 1860 draw call (büdcə 2500). Generator hissələri material üzrə birləşdirməlidir (~8 draw/maşın). Bu, digər maşınlar istehsal olunmazdan əvvəl edilməlidir.

## D-011 — Stabillik assist-i: yalnız istənilməyən fırlanmanı götürür

`racer_core/StabilityAssist` (mühərrikdən asılı deyil, unit test olunur), `VehicleRuntime` onu bucaq sürəti dəyişməsi kimi tətbiq edir. Sürət, tork və ya tutuş əlavə etmir. Oyunçu və AI eyni fizikanı alır (plan §2.10).

- **Yaw:** faktiki yaw sürəti kinematik (bicycle model) sürəti 1.35x aşanda əks təsir göstərir, ən çox 6 rad/s². Əl əyləci basılı olanda söndürülür, buraxılandan sonra `drift_recovery_s` ərzində qayıdır.
- **Havada:** pitch/roll-u yavaşca düzəldir, ən çox 4 rad/s². Yaw oyunçuda qalır. Ölçmə: çevrilərək atılan maşın assist-siz up.y **0.51** ilə, assist ilə **0.99** ilə enir.
- **Ox çevrilməsi:** core "müsbət X = burun yuxarı, müsbət Z = sağ tərəf aşağı" qəbul edir; Jolt-da hər ikisi əksinədir, adapter çevirir.

## D-012 — Əl əyləci drift-i: arxa yan tutuşu maşın başına parametrdir

Jolt-un təkər modelində sürtünmə dairəsi yoxdur: kilidlənmiş arxa təkər yan tutuşunu saxlayır. Ölçmə: əl əyləci dönməni cəmi 23.6°-dən 24.6°-ə artırırdı. Qərar: əl əyləci basılı olanda arxa yan tutuşu `handbrake_rear_grip`-ə endirilir (0.1 s), buraxılanda `drift_recovery_s` ərzində qayıdır. Drift və grip maşınları bu parametrlə fərqlənəcək (plan §2.7).

D01 (0.45 / 0.35 s / 1500 Nm): 0.8 s-lik dönmə 23.6° → 50.2°, buraxılışdan 1 s sonra yaw sıfırdır, arxa slip 0.002 rad.
İlk tuning (3500 Nm, 0.6 s, zəif assist) buraxılışdan sonra **190° spin** verirdi. Test indi buraxılışdan sonrakı fırlanmanı ≤60° ilə məhdudlaşdırır.

**M2 üçün açıq:** 70 km/saatda 90°-lik əl əyləci dönməsi sürəti 23 km/saata endirir. Bu, hairpin üçün məqbuldur, amma uzun drift-lərin hissiyyatı oynanışla yoxlanmalıdır.

## D-013 — Təhlükəsizlik sərhədi və avtomatik bərpa

- Sınaq meydançasının perimetrində 1.2 m statik baryer var. Soak testində açıq-döngəli skript maşını kənara çıxarırdı; maşın ~114 s sərbəst düşdü (-1373 km/saat "sürət").
- `racer_core/RecoveryMonitor` (plan §2.9): təhlükəsiz anchor-u yalnız 4 təkər yerə toxunanda və maşın dik olanda yazır. Bərpa səbəbləri: düşmə (y < −5 m və ya NaN), 2 s tərsinə durma, **gaz/əyləc basılıykən** 5 s irəliləmənin olmaması. Park olunmuş maşın "ilişmiş" sayılmır. F-i 1 s saxlamaq son anchor-a qaytarır.
- 4 dəqiqəlik avtotest: 0 bərpa, 0 sağlamlıq pozuntusu.

## D-014 — Resurslar: uzun sessiyada sızma yoxdur

5 dəqiqəlik soak (1600×900, RTX 4050 Laptop): RAM 940.3 → 945.6 MB-a qədər pilləvari artıb 175-ci saniyədən sonra düz qalır (plato: məhdud keşlər). VRAM 300 s boyu 384 MB-da sabitdir. Kadr vaxtı sabitdir (orta 6.06 ms, p99 6.92 ms). 4 dəqiqəlik sonrakı test: RAM +4.8 MiB (plato), VRAM 0.

## D-015 — Vizual təkərlər oyun kodunda qurulur

Wicked-in `OverrideWehicleWheelTransforms` fizika aktiv olmayanda işləmir (`IsSimulationEnabled() = ENABLED && SIMULATION_ENABLED`). Bizim modeldə isə `Scene::Update` zamanı fizika həmişə söndürülüdür, ona görə təkərlər nə fırlanırdı, nə də dönürdü. `VehicleRuntime::UpdateWheelVisuals()` Jolt-dan body-yə nisbətən lokal pozanı oxuyub pivot entity-lərə yazır. Jolt əsası sağ-əllidir: +Z irəli / +Y yuxarı üçün "sağ" oxu −X-dir. +X ötürəndə təkərlər 180° tərs dururdu (hub-lar içəri baxırdı); test bunu tutdu. İndiki ölçmə: sağ sükanda ön təkərlər +32°, arxalar 0°, 20 km/saatda spin addım başına 0.129 rad (gözlənilən 0.129).

## D-016 — Maşın mesh-ləri runtime üçün birləşdirilir

`generate_vehicles.py` gövdəni bir mesh-də (≤8 material subset), hər təkəri öz pivotu altında ayrıca mesh-də birləşdirir. D01 LOD0: 93 → **5 mesh**; 20 maşın üçün draw call ~1860 → ~100. Üçbucaq sayları dəyişməyib. `fixture_axes` hər maşının təkər yerləşməsini yenə də yoxlayır.

## D-017 — Limiterdə ilişmə: məcburi yuxarı keçid

Jolt-un avtomatik ötürücüsü sürücü təkər slip-i > 0.1 olanda yuxarı keçmir. Güclə döngədən çıxan FWD maşın 1-ci ötürücüdə limiterə ilişirdi (sandbox-da 56 km/saat, 7000 RPM). `LimiterShiftAssist` (core, unit test olunur) qaz basılı halda ≥0.3 s limiterdə qalanda bir ötürücü yuxarı keçir; Jolt-un qalan məntiqi olduğu kimi qalır. Ölçmə (eyni ssenari, 6 s): assist olmadan limiterdə 1.94 s, assist ilə 0.03 s. Əlavə effekt: 0-100 km/saat 8.9 → 8.3 s.

## D-018 — Kamera kollizionu

Kamera maşının damının 1.2 m üstündəki nöqtədən hamarlanmış göz mövqeyinə fizika şüası atır və ilk maneədən 0.3 m əvvəl dayanır (plan §2.14). Kəsişmə hamarlamadan **sonra** yoxlanır ki, gecikmə kameranı divarın içinə apara bilməsin. Avtomatik test yoxdur; yalnız vizual yoxlanıb.

## M1 qapısının vəziyyəti

| Plan tələbi (§5.1, §5.2) | Vəziyyət |
|---|---|
| Bir greybox tras, bir maşın, telemetriya | Hazırdır: tuning meydançası, D01, HUD + JSON hesabat |
| Sürüş sabitliyi | Hazırdır: 21 handling testi, 4 dəqiqəlik avtotest, 0 sağlamlıq pozuntusu |
| 30/60/144 fps eyni simulyasiya | Bit-bit eynidir |
| Alt-tab, pause/resume, 100 ms hitch | SimClock testləri; fokus itəndə Wicked Update-i dayandırır |
| Catch-up addımlarında input iki dəfə işləmir | CommandBuffer testi |
| Bordür, ramp, enmə, yan və arxa təmas | Hazırdır |
| Baryer boyunca ilişməmə | Divar testi keçir; sürtünmə cəzası M2-də oynanışla qiymətləndiriləcək |
| Təkərlərin gövdəyə girməməsi (tam sükan/asqı) | **Açıq**: vizual/asset testi hələ yazılmayıb |
| NaN aşkarlanması | Telemetriya yoxlaması + bərpa monitoru |
| Referens avadanlıq (GTX 1660 / RX 5500 XT) | **Yoxlanmayıb**: burada yoxdur |

## D-010 — Sandbox avtotesti ölçmə alətidir

`racer_sandbox --autotest=N --report=... --screenshot=...`: skriptlə sürür, fokusdan asılı deyil (`alwaysactive` init-dən sonra qoyulur), watchdog `N + 240 s`-də xəta ilə çıxır.

90 s nəticə (1600×900): kadr orta 6.06 ms, p95 6.38, p99 6.73; 10748 addım; sağlamlıq pozuntusu 0; VRAM 384 MB-da sabit.
