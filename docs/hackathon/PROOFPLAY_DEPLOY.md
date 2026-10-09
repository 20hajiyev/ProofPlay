# ProofPlay: başqa kompüterdə və internetdə necə işləyir

## Əsas fikir: ekran yazısı lazım deyil

Oyun yarışı video kimi yazmır. O, **hər fizika addımında oyunçunun girişlərini** yazır: sükan, qaz, əyləc, silah. Bu girişlər JSON faylında saxlanılır (`sessions/<sürücü>/<vaxt>.json`, 3 dövrəlik yarış üçün 1–2 MB). Oyun deterministikdir, yəni server bu fayldan yarışı bit-bit eyni şəkildə yenidən qurur. Buna görə:

- Video ilə müqayisədə 100–1000 dəfə kiçikdir və yüklənməsi bir saniyə çəkir.
- Daha dəqiqdir: sürət, xətt və əyləc nöqtəsi təxmin edilmir, ölçülür.
- Saxtalaşdırıla bilmir. Faylda nəyisə dəyişsən, yarış eyni nəticə ilə bitmir və server onu "verified deyil" kimi işarələyir. Tamper testində 10 saxta faylın 10-u rədd edildi.
- Ekran yazısı və ya screenshot üçün heç bir icazə, proqram və GPU lazım deyil.

Ekran yazısı yalnız deterministik replay-i olmayan **başqa oyunlar** üçün lazım ola bilər. Bu gələcək işdir və hazırda iddia etmirik.

## Gündəlik istifadə (AI və terminal olmadan)

1. `ProofPlay.pyw` üzərinə iki dəfə klik edin. Server arxa planda başlayır və sayt brauzerdə özü açılır.
2. Oyununuzu **özünüz** açın. Sayt hansı dəstəklənən oyunun açıq olduğunu tanıyır, məsələn "RIVAL LINE is open".
3. Saytda **Start session** düyməsini basıb adınızı yazın.
4. Oynayın. Sessiya davam etdiyi müddətdə bitirdiyiniz hər yarış sizin adınıza yazılır və avtomatik analiz olunur. Sayt yeni yarışa özü keçir.
5. Bitirəndə **Stop session** basın. Səhifənin aşağısındakı **Quit ProofPlay** serveri dayandırır.

Sayt oyun açmır və hansı oyunun olduğunu təxmin etmir. Dəstəklənən oyunlar `dashboard/games.json` reyestrində yazılıb:

- oyunun adı;
- prosesinin adı (`racer_sandbox.exe`);
- adapterin növü;
- oyunun ad təyin edilmədən yazdığı "inbox" qovluğu.

Gələcəkdə yeni oyun əlavə etmək iki iş tələb edir: reyestrə bir sətir yazmaq və həmin oyun üçün adapter qurmaq. Adapter yarışı yazmalı, sonra yenidən oynatmalıdır (replay). Bu, deterministik oyunlarda mümkündür. Başqa oyunlar üçün ekran yazısı əsaslı adapter gələcək işdir.

Təhlükəsizlik:

- "Start session" və "Quit" yalnız serverin öz kompüterindən işləyir.
- Bu sorğular xüsusi `X-ProofPlay` başlığı tələb edir, ona görə brauzerdə açıq olan başqa sayt onları göndərə bilmir (CSRF).

## Üç işləmə rejimi

| Rejim | Nə vaxt | Necə |
|---|---|---|
| **1. Bir kompüter** | Demo, oyunçunun öz PC-si | `start_server.bat`, sonra `http://localhost:8787` açılır. Oyun yarışı `sessions/`-ə yazır, server özü götürür. |
| **2. Lokal şəbəkə (LAN)** | Hakaton zalı, dostlar | Server PC-də `start_server_lan.bat` işə salınır. Digər PC-lər `http://<server-IP>:8787` açır. Yarış faylı saytdakı **yükləmə düyməsi** ilə, sürüklə-burax ilə və ya `start_uploader.bat` (avtomatik) ilə göndərilir. |
| **3. İnternet (bulud)** | Hamı üçün açıq sayt | Windows VM-də (məsələn, Azure tələbə krediti ilə B2s) eyni paket işə salınır. Qarşısına HTTPS reverse proxy qoyulur (Caddy və ya IIS). Oyunçular uploader və ya sayt vasitəsilə fayl göndərir. |

## Paketləmə (başqa kompüter üçün)

```bash
python proofplay/package.py --zip
```

Nəticə `dist/ProofPlay/` qovluğu və `dist/ProofPlay.zip` arxividir:

- `bin/proofplay.exe`: analiz və replay. GPU tələb etmir, prosesə təxminən 39 MB RAM lazımdır.
- `content/{tracks,vehicles,combat}`: replay-in oxuduğu yeganə oyun məlumatı.
- `dashboard/`: sayt və server (yalnız Python stdlib).
- `agent/uploader.py`: oyunçunun PC-si üçün avtomatik göndərici.
- `start_*.bat`, `README.txt`.

Tələblər:

- Server PC-də Python 3.10+ quraşdırılmalıdır.
- `proofplay.exe` Windows üçündür. Linux server üçün Wicked/Jolt Linux-da ayrıca build olunmalıdır (Wicked Linux-u dəstəkləyir, bu layihədə hələ sınanmayıb).

## Server parametrləri

```
python dashboard/server.py --port 8787 --host 0.0.0.0 --data D:\proofplay-data --exe D:\pp\bin\proofplay.exe
```

- `--host 127.0.0.1` (default): sayt yalnız bu kompüterdən açılır.
- `--host 0.0.0.0`: sayt şəbəkədəki başqa kompüterlərdən də açılır.
- `--data`: sessiyalar, hesabatlar və SQLite bazası.
- `PROOFPLAY_CONTENT`: content qovluğu başqa yerdədirsə.
- Oyun tərəfində `PROOFPLAY_SESSIONS` yarışların harada saxlanacağını göstərir (uploader da onu oxuyur), `PROOFPLAY_PLAYER` isə sürücü adını təyin edir.

## Təhlükəsizlik (açıq serverdə)

`POST /api/upload` belə qorunur:

- Fayl ölçüsü 8 MB ilə məhdudlaşdırılıb.
- Sürücü adı yalnız `[A-Za-z0-9 _-]{1,32}` simvollarından ibarət ola bilər, buna görə fayl yolu ilə hücum mümkün deyil.
- Track id yoxlanılır.
- Fayl atomik yazılır (.tmp, sonra rename).

Liderlər cədvəlinə yalnız replay-i eyni nəticə verən yarışlar düşür.

İnternetə çıxarmazdan əvvəl bunlar lazımdır:

- HTTPS;
- sorğu limiti (rate limit) reverse proxy-də;
- disk kvotası;
- istifadəçi hesabları. Hazırda istənilən ad ilə yükləmək olur.

Bunlar hakaton demosu üçün açıq qalan işlərdir.

## Məhdudiyyətlər

- Şriftlər (Google Fonts) və ikonlar (unpkg Phosphor) CDN-dən gəlir. İnternetsiz LAN-da sayt sistem şriftləri ilə işləyir, amma ikonlar görünmür. Tam oflayn üçün bu faylları paketə əlavə etmək lazımdır, bu isə üçüncü tərəf faylının endirilməsidir, sahibin icazəsi ilə edilir.
- Analiz server PC-nin CPU-sunda işləyir: tam analiz təxminən 35–90 s, sürətli mərhələ təxminən 9 s çəkir. Eyni anda çox yükləmə gəlsə, onlar növbəyə düşür.
- Paketdə oyunun özü yoxdur, yalnız analiz hissəsi var. Oyunçunun kompüterində RIVAL LINE quraşdırılmalıdır, onun ayrıca paketlənməsi M-milestone işidir. Oyun yarışları öz build qovluğundakı `proofplay/sessions`-ə yazır. Başqa yerə yazmaq üçün `PROOFPLAY_SESSIONS` dəyişənindən istifadə olunur.
