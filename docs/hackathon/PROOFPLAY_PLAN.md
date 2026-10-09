# ProofPlay — hakaton planı

> "Hər AI məşqçi məsləhət verə bilər. ProofPlay yalnız işlədiyini sübut etdiyi məsləhəti verir."

Trek: **AI Gaming**. Kart: Dəyər 25 · Prototip+AI 30 · Test 20 · Reallaşdırma 15 · Orijinallıq 10.
Hədəf: 250 komandadan ilk 15 (final).

---

## 1. Qalib strategiya (niyə ilk 15-ə düşəcəyik)

Kart deyir: *"İki günlük AI demo asanlıqla təsirli, çətinliklə dürüst olur."* Test + Reallaşdırma = 35 bal.
Komandaların əksəriyyəti LLM-ə statistik verib "AI məşqçi" quracaq. Biz onlarla **eyni şeyi ölçüb** fərqi rəqəmlə göstərəcəyik.

Beş prinsip:

1. **Canlı dövr hər şeydən vacibdir.** Yarış → dashboard → sübutlu məsləhət → yenidən yarış → yaxşılaşma. Birinci gecə bu dövr əvvəldən sona işləməlidir, gözəllik sonra.
2. **Rəqəm, rəqəm, rəqəm.** Hər slaydda ölçülmüş rəqəm: ablasiya, kalibrləmə, xərc, insan pilotu. Rəqəmsiz iddia yoxdur.
3. **"LLM məşqçisi" ilə müqayisə** — bizim ən güclü slaydımız. Rəqiblərin edəcəyi şeyi biz baseline kimi qurub sınayırıq.
4. **Uğursuzluqları özümüz göstəririk.** Kart bunu açıq istəyir, komandaların çoxu gizlədir.
5. **Dürüst açıqlama:** "Oyun əvvəlki işimizdir; hakaton işi ProofPlay qatıdır" — ilk 20 saniyədə. Yeni kod ayrıca `proofplay/` qovluğunda.

Əlavə: demo internetsiz də işləməlidir (LLM düşsə, izahlar şablona keçir); demo videosu ehtiyat kimi hazır olur; pitch ən azı 3 dəfə saatla məşq edilir.

---

## 2. Arxitektura

```
Oyun (racer_sandbox)           ProofPlay CLI (proofplay.exe, headless)        Dashboard (veb)
 RacePath → SessionRecorder ─►  record | replay | analyze | verify      ─►  server.py + SQLite
 sessions/*.json                 • deterministik replay (sübut bazası)          index.html
 (setup + hər tick girişlər      • atributlar (0–99), bot kalibrləməsi          • Bacarıq profili
  + son vəziyyət checksum)       • döngə üzrə vaxt itkisi                       • Tras xəritəsi (itki rəngi)
                                 • What-If axtarışı (insan məhdudiyyətli)       • 3 sübutlu məsləhət + xəyal xətt
                                 • LLM izahı (yalnız rəqəmlərdən) / şablon      • Tarixçə
                                 → report.json                                  • Leaderboard (verify ✓)
```

**Sessiya faylı** yalnız qurulumu və oyunçunun girişlərini saxlayır; qalan hər şey deterministik
replay ilə yenidən hesablanır (kiçik fayl, saxtalaşdırılmaz). Determinizm eyni build daxilində
zəmanətlidir → sessiyada `build_id` saxlanır, verify eyni build ilə aparılır.

---

## 3. İş paketləri (bloklama sırası ilə)

| # | Paket | Çıxış | Bloklayır |
|---|---|---|---|
| P0 | **Ölçmə**: headless replay sürəti (saniyə/yarış) | rəqəm | What-If həcmi |
| P1 | **Sessiya formatı + record/replay** (`proofplay record`, `proofplay replay`) — bot sürücü, qüsur əkmə (`--flaw late_brake@corner3`) | replay checksum eyni | hamısı |
| P2 | **Analiz**: döngə seqmentləri, vaxt itkisi (Hard bot etalonu), 6 atribut, kalibrləmə | report.json | P4, P5 |
| P3 | **What-If**: tormoz nöqtəsi → Ward vaxtı → xətt; insan məhdudiyyəti (reaksiya ≥ 200 ms, sükan sürəti) | sübutlu məsləhət + xəyal trayektoriya | demo |
| P4 | **Dashboard**: server.py, profil, xəritə, məsləhət animasiyası, tarixçə | brauzerdə canlı | demo |
| P5 | **Oyun inteqrasiyası**: RacePath-də SessionRecorder, yarış bitəndə fayl → dashboard özü yenilənir | canlı dövr | demo |
| P6 | **Leaderboard + verify** + davranış yoxlaması (insan üçün qeyri-mümkün reaksiya) | ✓ işarəsi | T5 slaydı |
| P7 | **Baseline məşqçilər**: qayda əsaslı + yalnız-LLM; simulyasiyada yoxlanması | ablasiya rəqəmi | pitch |
| P8 | **Testlər/ölçmələr** (bölmə 4) + insan pilotu | rəqəmlər | pitch |
| P9 | **Pitch**: slaydlar, video ehtiyat, Q&A hazırlığı | 5 dəq | — |

---

## 4. Ölçüləcəklər (pitch rəqəmləri)

| Test | Metod | Kart |
|---|---|---|
| Əkilmiş qüsurun tapılması | Bot + məlum qüsur (gec tormoz, gec Ward, geniş xətt) × 3 döngə × 3 tras; ProofPlay düzgün döngəni + düzgün düzəlişi tapırmı | Test, AI |
| Ablasiya | Eyni 50 hal: qayda əsaslı / yalnız LLM / ProofPlay → simulyasiyada işləyən məsləhət %-i, həll tapılmayan hal %-i | AI, Test |
| Kalibrləmə | Easy < Normal < Hard OVR sırası, 20 yarış | Test |
| Təkrar sabitlik | Eyni bot, 2 sessiya, atribut fərqi | Test |
| Leaderboard | 10 saxta nəticə → rədd faizi; kadr-dəqiq bot → davranış bayrağı | Test, T5 |
| Xərc | analiz saniyəsi, LLM $/analiz, sessiya MB | Reallaşdırma |
| İnsan pilotu | 5–8 nəfər; məsləhətli döngə vs məsləhətsiz döngə, eyni oyunçu | Dəyər |

Uğursuzluq slaydı: xilası mümkün olmayan qəza; az datada qeyri-sabit profil; yavaşladılmış oyunla edilən girişlər.

---

## 5. Vaxt cədvəli (2 gün)

| | 1-ci gün | 2-ci gün |
|---|---|---|
| Səhər | P0, P1 | P3 (Ward, xətt), P6 |
| Günorta | P2, P3 (tormoz) | P7, P8, insan pilotu |
| Axşam | P4 + P5 → **canlı dövr işləyir** | P9: slaydlar, video, 3× məşq |

Kəsilir (slaydda "növbəti addım"): ekran videosu/vision, 3D replay klipləri, onlayn hesablar,
ikinci janr adapteri.

---

## 6. Pitch (5 dəq)

0:00 açıqlama · 0:20 problem + bazar (şahmat, Tekken 8, Mobalytics) · 0:50 **canlı dövr** ·
2:30 ablasiya (LLM məşqçi vs ProofPlay) · 3:20 pilot + uğursuzluqlar · 3:50 xərc, adapter, növbəti addım ·
4:20 rəqiblərlə fərq · 4:40 yekun cümlə.

## 7. Q&A hazırlığı

- "Bu AI-dirmi, yoxsa sadəcə simulyasiya?" → axtarış/planlaşdırma + ablasiya rəqəmi.
- "Deterministik olmayan oyunlarda?" → rollback/lockstep oyunları hazırdır; digərlərinə snapşot lazımdır.
- "Tekken 8 bunu edir." → əllə, bir oyun üçün; biz avtomatik, sübutlu, adapterlə.
- "İnsan məsləhəti tətbiq edə bilirmi?" → insan məhdudiyyətləri + pilot rəqəmləri.
- "Oyun hakatonda yazılmayıb." → açıqlama + `proofplay/` commit tarixçəsi.
