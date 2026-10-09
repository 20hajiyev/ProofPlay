# ProofPlay — pitch deck üçün layihə məlumatı

> Bu sənəd pitch deck hazırlamaq üçündür. Bütün rəqəmlər ölçülüb (2026-10-09, son yeniləmə: real insan yarışı, iki mərhələli analiz). Ölçülməmiş olanlar
> **[ÖLÇÜLƏCƏK]** kimi işarələnib — onları slayda rəqəmsiz yazmayın.

Trek: **AI Gaming**. Qiymət kartı: Dəyər 25 · Prototip və AI 30 · Test 20 · Reallaşdırma 15 · Orijinallıq 10.

---

## 1. Bir cümlə

**"Hər AI məşqçi məsləhət verə bilər. ProofPlay yalnız işlədiyini sübut etdiyi məsləhəti verir."**

Qısa izah: ProofPlay oyunçunun yarışını analiz edir, zəif yerlərini tapır və hər məsləhəti vermədən
əvvəl **oyunun öz fizikası ilə yenidən simulyasiya edib yoxlayır**. Oyunçu ehtimal yox, sübut alır.

---

## 2. Problem

**Oyunçu uduzur, amma nəyi dəyişməli olduğunu bilmir.**

- Mövcud alətlər "burada vaxt itirdin" deyir, "bunu etsəydin, nə olardı" sualına sübutla cavab vermir.
- AI məşqçilər (statistikanı LLM-ə verən alətlər) məsləhət verir, amma o məsləhətin işlədiyini heç kim yoxlamır.

**Bazar sübutu (slayd üçün):**
- **Şahmat:** mühərriklə oyun analizi standartdır; Chess.com dərin analizi pullu abunəyə qoyub.
- **Tekken 8 "My Replay & Tips":** Bandai Namco bunu bir oyun üçün əllə qurdu; oyunçular bəyəndi
  (Destructoid onu "what-if machine" adlandırıb).
- **Məşq alətləri bazarı:** Mobalytics (7M+ istifadəçi iddiası, $11.25M), Track Titan ($5M, 2025),
  Trophi.ai (1.5M sessiya iddiası). Rəqəmlər şirkətlərin öz iddialarıdır.

**Öz sorğumuz:** [ÖLÇÜLƏCƏK] — 20–30 oyunçu: "Uduzanda səbəbini bilirsənmi?"

---

## 3. Həll — necə işləyir (sadə dildə)

1. **Oyunçu yarış sürür.** Oyun hər addımın girişlərini (sükan, qaz, tormoz, silah) arxa planda yazır.
2. **ProofPlay yarışı yenidən oynadır.** Oyun deterministikdir: eyni girişlər həmişə eyni nəticəni verir,
   bit-bit.
3. **Zəif yerləri tapır:** hər döngədə ideal sürətə görə itirilən vaxt, 6 bacarıq göstəricisi.
4. **Alternativ gələcəkləri sınayır:** "3-cü döngədə 30 m gec tormozlasaydın?" — bu, real fizika ilə
   yenidən simulyasiya olunur. Yalnız işləyən və insanın edə biləcəyi dəyişikliklər qalır.
5. **Dashboard göstərir:** bacarıq profili, tras xəritəsində vaxtın harada itdiyi, və sənin xəttinlə
   sübut olunmuş düzəlişin yan-yana animasiyası (iki maşın eyni anda sürür).
6. **Leaderboard saxtalaşdırıla bilməz:** hər nəticə serverdə yenidən simulyasiya olunub yoxlanılır.

---

## 3a. İki mərhələli analiz (demoda göstərin)

1. **Dərhal (≈5 s): "İlkin baxış".** Oyunun öz nəticə ekranındakı statistika (yer, vaxt, dövrələr, isabət,
   zərər, sürət) qısa qeydlərə çevrilir. Məsələn: "Silahların çoxu boşa gedir: 3/14 isabət (21%)". Ekranda
   **"yoxlanılmamış"** kimi işarələnir: bu, başqalarının etdiyi səviyyədir.
2. **Sonra (≈90 s): replay analizi.** Yarışın yazısı yenidən simulyasiya olunur. Bacarıq profili hesablanır,
   məsləhətlər sübut olunur, nəticə yoxlanılır.

Pitch üçün mesaj: *"Birinci mərhələ hamının verdiyi cavabdır. İkinci mərhələ yalnız bizdə var: sübut."*

## 4. Canlı demo dövrü (pitch-in ürəyi, 90 saniyə)

1. Kimsə oyunda yarış sürür (S01 Dock Loop, 2 dövrə).
2. Yarış bitən kimi dashboard avtomatik yenilənir (sessiya faylı → analiz → hesabat).
3. **≈5 s sonra — ilkin baxış:** oyunun statistikasından qeydlər ("0/11 isabət", "sonuncu yer") —
   ekranda "yoxlanılmamış" kimi işarəli.
3a. **≈90 s sonra — replay analizi:** bacarıq kartı (məs. 44, BÜRÜNC), tras xəritəsi (oyunun öz
   səhnəsinin yuxarıdan render-i), ən çox vaxt itən döngə tünd.
4. Məsləhət kartı: "Döngə 3: tormozu 30 m gec bas — **+1.22 s**, ✓ 11 simulyasiya ilə sübut olunub".
4a. **"Vaxt harada itdi":** "1-ci dövrə, 0–820 m: −23.2 s — dayandın / ilişdin" — xəritədə həmin
   hissə qalın xətlə göstərilir.
5. Xəritə həmin döngəyə zoom olur: **SƏN** (boz, qırıq xətt) və **DÜZƏLİŞ** (yaşıl) maşınları eyni
   anda sürür — fərq gözlə görünür.
6. Oyunçu yenidən sürür, göstərici yüksəlir (▲).

**Ehtiyat:** demo videosu hazır olsun (internet/kompüter problemi olarsa).

---

## 5. AI nə edir (kart: 30 bal)

| AI komponenti | Nə edir |
|---|---|
| **Kontrafaktik axtarış** | Uğursuzluğu aradan qaldıran ən kiçik, insanın edə biləcəyi dəyişikliyi tapır (tormoz nöqtəsini ±5…30 m sürüşdürür, 10 variant + baza, real fizika ilə) |
| **Zəiflik prioritetləşdirmə** | Ən çox vaxt itən keçidləri sıralayır, ən faydalı 3 məsləhəti seçir |
| **Bacarıq modeli** | 6 atribut (Sürət, Xətt, Döyüş, Müdafiə, Sabitlik, Bərpa) + ümumi bal, botlarla kalibrlənir |
| **Davranış yoxlaması** | Leaderboard-da insandan kənar girişləri axtarır (sükan düzəlişlərinin tezliyi) — hədd [ÖLÇÜLƏCƏK] |

**AI-nin töhfəsi ölçülüb** — bax bölmə 6, "3 məşqçi müqayisəsi".

---

## 6. Ölçülmüş nəticələr (kart: Test 20 bal)

### 6.1 Əsas slayd — 3 məşqçinin müqayisəsi
Eyni 14 yarış (12-sində qəsdən əkilmiş tormoz qüsuru). Hər məşqçinin **hər məsləhəti** oyunun fizikası
ilə yenidən simulyasiya olunub yoxlanılıb:

| Məşqçi | Yoxlanan məsləhət | **İşləyir** | Pisləşdirir / qəza | Orta qazanc |
|---|---|---|---|---|
| Klassik statistika qaydası | 32 | **37.5%** (12) | **59%** (19) | 0.31 s |
| LLM məşqçi (gpt-5-mini, eyni statistika) | 34 | **76.5%** (26) | **21%** (7) | 0.63 s |
| **ProofPlay** | 31 | **100%** (31) | **0%** | **1.05 s** |

**Slayd cümləsi:** *"LLM məşqçinin hər 5 məsləhətindən biri oyunçunu pisləşdirir və ya qəzaya salır.
ProofPlay-in heç biri — və orta qazancı 1.7 dəfə çoxdur."*

**Dürüst qeyd (slaydda yazın):** ProofPlay-in 100%-i konstruksiyaya görədir — o, yalnız yoxladığı
məsləhəti göstərir. Fərqi yaradan məhz budur: başqaları təxmin edir, biz yoxlayırıq.

### 6.2 Determinizm (sübutun əsası)
- Oyunun özündə yazılmış 3 dövrəlik yarış (22 627 addım) headless replay ilə **bit-bit eyni** çıxdı.
- Bot yarışları da eyni: `MATCH`.

### 6.3 Saxtalaşdırma testi (leaderboard)
Oyundan yazılmış həqiqi sessiyadan 10 saxta versiya: 5 s tez vaxt iddiası, 1-ci yer iddiası, saxta hash,
bir sükan girişini dəyişmək, tormozu qazla əvəz etmək, asan rəqiblər, asan çətinlik, son 1000 addımı
kəsmək, bir dövrə az, motor hissəsi əlavə etmək.
- **10/10 saxta versiya rədd edildi, həqiqi sessiya qəbul edildi.**

### 6.4 Əkilmiş qüsurun tapılması
- "Tez tormoz" qüsuru (6 hal): ProofPlay qüsurlu döngəni **5/6** halda 1-ci məsləhət kimi tapdı.
- Nümunə: qüsurlu bot → "Döngə 3: 30 m gec tormozla, +1.22 s"; qüsursuz botda həmin döngə 1-ci deyil.

### 6.5 Real insan yarışı (oyunun özündə, klaviatura ilə)
Karyera yarışı: 12 maşın, Easy rəqiblər, məhdud qabiliyyətlər, oyunçunun qaraj hissələri (splitter,
diffuzor, qanad, vent kapot), grid-də 7-ci yer, 3 dövrə.
- **Replay bit-bit eyni (`MATCH`)** — oyunun qurduğu hər şey (qaraj, qaydalar, grid) dəqiq təkrarlandı.
- **Nəticə:** 12/12 yer, 3:10.79; dövrələr **79.8 / 56.4 / 54.6 s**.
- **Öz ən yaxşı dövrəsi ilə müqayisə yarışın harada uduzulduğunu tapdı:**

| Harada | İtən vaxt | Nə baş verdi |
|---|---|---|
| 1-ci dövrə, 0–820 m | **23.2 s** | start; maşın dayandı (0 km/s), qısa müddət trasdan çıxdı |
| 2-ci dövrə, 660–980 m | 5.8 s | trasdan 0.8 m çıxdı |
| 3-cü dövrə, 1160–1520 m | 5.8 s | səbəb hadisəsi yoxdur (yavaş sürüş) |

- **Dürüst tapıntı:** insan döngələrin çoxunda tormoza toxunmur (qazı buraxır) və 2–3-cü dövrələrdə
  botlardan sürətlidir. Ona görə insan ölçüsündə heç bir tormoz/qaz dəyişikliyi vaxt qazandırmadı və
  sistem **"sübut tapılmadı"** dedi — uydurmadı. Əsas dərs döngədə deyil, ilk 820 metrdə idi.

### 6.6 Bot ilə insanı ayırmaq (anti-cheat siqnalı, ilk ölçmə)
| Göstərici | İnsan (klaviatura) | Botlar |
|---|---|---|
| Girişin dəyişdiyi addımların payı | **2.4%** | 91–95% |
| Sükanın fərqli dəyərləri | **3** (sol / mərkəz / sağ) | minlərlə |

Bir insan nümunəsidir (n=1) — hədd üçün daha çox insan yarışı lazımdır, amma fərq çox böyükdür.

### 6.6a Oyunun statistikası = bizim ölçmə
Oyunun öz nəticə ekranı ilə replay ölçməsi: **12/12 dəqiq uyğun** (2 yarış × yer, vaxt, dövrələr, istifadə,
isabət, zərər). Bu yoxlama hər hesabatda avtomatik aparılır.

### 6.6b Başqa tras
L01 Container Run-da (fərqli tras): replay `MATCH`, **14 döngə avtomatik tapıldı**, sübutlar alındı.
Tras əllə öyrədilmir — döngələr trasın həndəsəsindən çıxarılır.

### 6.7 Performans və xərc
| Göstərici | Dəyər |
|---|---|
| Replay sürəti | real vaxtdan **26 dəfə** tez (2 dövrə ≈ 5 s) |
| Tam analiz (6 atribut + 3 sübut) | ~90 s (4 maşın), ~200 s (12 maşın) |
| İlkin baxış (statistikadan) | **≈5 s** |
| Yaddaş (bir proses) | **39 MB** (optimallaşdırmadan əvvəl 125 MB) |
| Sessiya faylı | 1.1–1.7 MB / yarış |
| LLM xərci (müqayisə üçün) | 14 çağırış ≈ 26K token ≈ **$0.04** |
| ProofPlay-in özünün LLM xərci | **$0** — sübut lokal simulyasiyadır |

---

## 7. Uğursuzluqlar və məhdudiyyətlər (kart açıq tələb edir — slayd edin!)

1. **Botla yazılmış girişlər replay yoxlamasından keçir.** Yoxlama "bu nəticə bu girişlərdən çıxıb" deyir,
   girişləri insanın yazdığını yox. İlk insan yarışı bot ilə insan arasında böyük fərq göstərdi (2.4% vs
   91–95%), amma bu bir nümunədir — hədd üçün daha çox insan yarışı lazımdır [ÖLÇÜLƏCƏK].
1a. **İnsan üçün sübutlu döngə məsləhəti tapılmaya bilər.** Sürətli oyunçuda tormoz/qaz nöqtəsini
   dəyişmək vaxt qazandırmır; sistem bunu açıq deyir və vaxtın harada itdiyini öz ən yaxşı dövrə ilə göstərir.
2. **Şübhəli böyük qazanclar:** bəzi hallarda bir döngədə 5.4–5.5 s — çox güman döyüş təsadüfü
   (vuruşdan yayınma), sürüş dərsi deyil. Həll: davamlılıq yoxlaması (qonşu məsafələr də qazandırmalıdır).
3. **"Gec tormoz" qüsuru yanlış test çıxdı:** bu botu əksinə sürətləndirdi (P3 → P1). Ona görə yalnız
   "tez tormoz" halları etibarlı test sayılır.
4. **Deterministik olmayan oyunlar:** tam replay işləmir; onlar üçün vəziyyət snapşotu lazımdır.
5. **Analiz ~90–200 s çəkir** (maşın sayından asılı) — yarış bitəndən sonra gözləmə var (növbəti addım: snapşotla sürətləndirmək).

---

## 8. Rəqiblər və fərq (kart: Orijinallıq 10)

| | Mobalytics / Leetify | Tekken 8 Tips | LLM məşqçilər | **ProofPlay** |
|---|---|---|---|---|
| Statistika və profil | ✅ | — | ✅ | ✅ |
| "Nə etməliydin" | ümumi məsləhət | ✅ əllə, bir oyun | ✅ yoxlanılmamış | ✅ **sübut olunmuş** |
| Məsləhətin işlədiyinin sübutu | ❌ | qismən (oyunçu özü sınayır) | ❌ | ✅ avtomatik |
| Saxtalaşdırılmaz leaderboard | ❌ | ❌ | ❌ | ✅ |
| Başqa oyuna tətbiq | hər oyuna ayrıca iş | yoxdur | asan | 4 funksiyalı adapter |

Rəqibləri hakimdən **əvvəl** özünüz deyin.

---

## 9. Reallaşdırma (kart: 15 bal)

- **Verilənlər:** xarici verilənlər lazım deyil — oyunun öz telemetriyası.
- **Xərc:** CPU dəqiqələri; LLM tələb olunmur (yalnız izahat üçün opsional).
- **İnteqrasiya:** oyunda dəyişiklik kiçikdir — yarış döngəsində bir yazıcı (girişləri qeyd edir).
- **Başqa oyunlar üçün adapter (növbəti addım):** `SaveState`, `LoadState`, `Step(input)`, nəticə şərti.
  Rollback netcode olan döyüş oyunları və lockstep olan strategiya oyunları artıq deterministikdir.
- **Növbəti addımlar:** (1) davranış əsaslı anti-cheat hədləri insan datası ilə; (2) snapşotla analiz
  sürətini 10× artırmaq; (3) ikinci janr adapteri; (4) onlayn leaderboard.

---

## 9a. Başqa oyunlar — dürüst cavab

- **Bu gün:** yalnız bizim oyunda işləyir.
- **Ümumi olan hissələr:** yazma, replay yoxlaması, saxtalaşdırılmaz leaderboard, iki mərhələli analiz,
  dashboard.
- **Yarışa xas olan hissələr:** döngələr, tormoz/qaz nöqtələri, dövrə müqayisəsi.
- **Növbəti addım:** adapter (`SaveState`, `LoadState`, `Step`, nəticə şərti) və janra uyğun "dəyişiklik
  növləri" — döyüş oyununda bloku kadr-kadr dəyişmək, platformerdə tullanma anını dəyişmək.
- Rollback netcode-u olan döyüş oyunları və lockstep strategiya oyunları artıq deterministikdir.

## 10. Açıqlama (ilk 20 saniyədə deyin)

"Oyun — RIVAL LINE — bizim əvvəlki işimizdir. Hakaton işi ProofPlay qatıdır: yarış yazıcısı, analiz,
kontrafaktik sübut, yoxlanılan leaderboard və dashboard. Yeni kod ayrıca `proofplay/` qovluğundadır."

---

## 11. Slayd planı (5 dəqiqəlik pitch, 10 slayd)

| # | Slayd | Məzmun | Vizual | Vaxt |
|---|---|---|---|---|
| 1 | Başlıq | ProofPlay — "Ehtimal yox, sübut." | dashboard ekran şəkli | 0:00 |
| 2 | Açıqlama | Oyun əvvəlki işimiz, hakaton işi bu qatdır | oyunun ekran şəkli | 0:10 |
| 3 | Problem | Oyunçu niyə uduzduğunu bilmir; AI məşqçilər yoxlamır | 3 bazar faktı | 0:20 |
| 4 | Həll | 6 addımlıq axın (bölmə 3) | sxem | 0:45 |
| 5 | **Canlı demo** | Yarış → 5 s: ilkin baxış → 90 s: sübut + "vaxt harada itdi" → xəritədə iki maşın | canlı | 1:05 |
| 6 | **3 məşqçi** | 37.5% / 76.5% / 100%, pisləşdirmə 59% / 21% / 0% | cədvəl/bar | 2:35 |
| 7 | Etibar | Determinizm bit-bit (real insan yarışı daxil), saxtalaşdırma 10/10, insan vs bot 2.4% / 93% | rəqəmlər | 3:15 |
| 8 | Uğursuzluqlar | 3 dürüst məhdudiyyət | siyahı | 3:35 |
| 9 | Fərq və miqyas | rəqib cədvəli, adapter, xərc ($0 LLM, 39 MB) | cədvəl | 4:05 |
| 10 | Yekun | "Hər AI məşqçi məsləhət verə bilər. ProofPlay yalnız işlədiyini sübut etdiyini verir." | logo | 4:40 |

---

## 12. Hakim sualları üçün hazır cavablar

- **"Bu AI-dirmi, yoxsa sadəcə simulyasiya?"** → Axtarış və planlaşdırma klassik AI-dir; töhfəsi ölçülüb:
  eyni statistikanı alan LLM-in məsləhətlərinin 21%-i pisləşdirir, bizimkilər 0%.
- **"100% saxta görünür."** → Konstruksiyaya görədir və bunu açıq deyirik: yalnız yoxladığımızı göstəririk.
  Ölçülən fərq: orta qazanc 1.05 s vs 0.63 s, və pisləşdirən məsləhət 0.
- **"Tekken 8 bunu edir."** → Bir oyun üçün əllə qurulub, oyunçu özü sınamalıdır. Biz avtomatik
  axtarırıq, sübut edirik və adapterlə başqa oyunlara keçə bilirik.
- **"Deterministik olmayan oyunlarda?"** → Rollback/lockstep oyunları hazırdır; digərlərinə snapşot lazımdır.
- **"İnsan məsləhəti tətbiq edə bilirmi?"** → Dəyişikliklər insan ölçüsündədir (±5–30 m tormoz).
  İnsan pilotu: [ÖLÇÜLƏCƏK].
- **"Oyun hakatonda yazılmayıb."** → Açıqlama + `proofplay/` qovluğu və commit tarixçəsi.
- **"İnsan oyunçuda işləyirmi?"** → Bəli, real klaviatura yarışı bit-bit təkrarlandı. Sürətli oyunçuda
  döngə məsləhəti tapılmaya bilər — sistem bunu açıq deyir və vaxtın harada itdiyini göstərir
  (bizim testdə: ilk 820 m-də 23.2 s, maşın dayanmışdı).
- **"Leaderboard-u botla aldatmaq olarmı?"** → Nəticəni saxtalaşdırmaq olmur (10/10 rədd). Girişləri
  botla yazmaq hələ mümkündür — davranış analizi növbəti addımdır (dürüst cavab).

---

## 13. Pitch-dən əvvəl tamamlanmalı olanlar

- [ ] İnsan pilotu: 5–8 nəfər, məsləhətli döngə vs məsləhətsiz döngə (eyni oyunçu).
- [ ] Qısa sorğu (20–30 oyunçu) — problem slaydı üçün öz rəqəmimiz.
- [x] İlk insan yarışı (2.4% vs 91–95%) — [ ] daha 5–10 yarış hədd üçün.
- [ ] Davamlılıq yoxlaması (şübhəli 5 s qazancları süzmək).
- [ ] Demo videosu (ehtiyat).
- [ ] Pitch-i saatla 3 dəfə məşq etmək.

## Mənbələr
- Tekken 8: oneesports.gg/tekken/tekken-8-replays-and-tips, destructoid.com (Tekken 8 review)
- Chess.com Cloud Analysis: support.chess.com/en/articles/8646880
- Mobalytics: gamesbeat.com, techcrunch.com; Track Titan: simracingonline.co.uk; Trophi.ai: trophi.ai
- Ölçmələr: `proofplay/results/benchmark.md`, `proofplay/results/tamper.json`, `docs/DECISIONS_M2.md` D-094
