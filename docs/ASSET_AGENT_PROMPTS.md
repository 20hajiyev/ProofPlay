# Təkrar istehsal və növbəti asset agentləri üçün prompt paketi

## Bütün agentlər üçün dəyişməz təlimat

Sən bu yarış oyununun asset istehsal agentisən. Əvvəl `docs/ASSET_CONTRACT.md`, `docs/DECISIONS.md` və öz tapşırığının mövcud generatorunu oxu. Yalnız sənə ayrılmış faylları dəyiş. Başqa agentin dəyişikliklərini geri qaytarma. Real GLB/Blend/WAV/SVG faylı yarat; şəkil və promptu işlək 3D asset kimi təqdim etmə. Nəticəni generator, deterministik seed, mənbə istinadları, ölçülər, versiya, hash və yoxlama nəticələri ilə sənədləşdir. Uğursuz və aparılmamış yoxlamaları açıq göstər. Asset IDs təkrarlanmamalıdır.

Blender: metr, Z yuxarı, avtomobilin burnu +Y, sol tərəfi -X. Standart glTF çevrilməsindən sonra Wicked importer nəticəsini ayrıca yoxla; sadəcə glTF metadata yazaraq runtime istiqamətini təsdiqləmə. Təkər mərkəzləri WHEEL_PIVOT_FL/FR/RL/RR. LOD0/1/2/3 maksimum üçbucaqları 60000/30000/12000/4000, maksimum 8 əsas material. Modifier tətbiq olunandan və GLB yazılandan sonra faktiki sayları ölç. Source polygon sayına etibar etmə. Obyekt pivotunu dəyişərkən world transformunu qoruyub yoxla. Hər modeldən sonra Blender orphan data-nı təmizlə.

## Agent A — seçilmiş dörd avtomobil

Sahiblik: `tools/generate_realistic_vehicles.py`, `assets/vehicles_realistic/**`. Golf GTI Mk5, BMW M3 E46, Subaru WRX STI 2005 və Ford Ranger Raptor 2019 modellərinin siluetini, təkər bazasını, dam xəttini, kapot/fara/barmaqlıq konturlarını öz modelinə uyğun düzəlt. Ölçüləri istehsalçı materialları ilə tutuşdur; bazar/il fərqlərini metadata-da qeyd et. Dörd ayrı avtomobili eyni kuzovun rəng variantı kimi yaratma. Hərəsində 4 LOD, ayrıca sadə collision, redaktə edilə bilən mənbə, ön/arxa/yan/üst render və fərdi ölçü metadata-sı ver. Faralar kuzova oturmalı, bamper və qanadlar arasındakı boşluqlar yalnız real konstruksiyaya uyğun olmalıdır. Arxa və ön təkər oyuqlarını maşının bütün enindən keçən kəsiklə yaratma. Mövcud D01–D04 fayllarına toxunma.

## Agent B — müasir Alman modelləri

Sahiblik: `tools/generate_modern_germans.py`, `assets/modern_germans/**`. BMW M4 G82 və Mercedes-AMG A45 S W177 hazırla. Kupenin uzun kapotu və aşağı dam xəttini hetçbekin qısa arxa hissəsi ilə fərqləndir. M4 barmaqlığının şaquli forması, A45 ön hava girişləri və hatch damı oxunaqlı olsun. Şüşəni sadə düz plane kimi əyri cabin içindən keçirmə; səthə uyğun və fiziki oturmuş şüşə yarat. Spoiler dayaqları kuzova birləşsin. Hər modeldə 4 LOD, collision, Blender mənbəyi, studio render, ölçülər, istinadlar və həqiqi GLB yoxlama hesabatı saxla.

## Agent C — müasir kupelər

Sahiblik: `tools/generate_modern_coupes.py`, `assets/modern_coupes/**`. Porsche 911 GT3 (992) və Nissan Z RZ34 hazırla. 911-in yuvarlaq qanadları, alçaq burun/geriyə çəkilmiş cabin, dairəvi inteqrasiya edilmiş faraları və GT3 qanadı; Z-nin uzun kapot/qısa arxa hissə, fərqli ön/arxa optikası nəzərə alınsın. Kuzovda kəsik və boşluq, havada asılmış lampalar, genişlənmiş şüşə köpüyü qəbul edilmir. Arch boolean yalnız müvafiq yan zonanı kəssin. Hər modeldə 4 LOD, collision, Blender mənbəyi, ön/arxa render, ölçülər, istinadlar və GLB yoxlama nəticələri ver.

## Agent D — növbəti bədii keyfiyyət keçidi

Əvvəl hər avtomobilin faktiki ön/yan/arxa/üst renderini real modelin istehsalçı referansları ilə müqayisə et. Siluet və kuzov nisbətlərini material parıltısından əvvəl düzəlt. Panel tikişləri, qapı/kapot/baqaj ayrı hissələri, düzgün lamp housing, əyləc kaliperi, təkər protektoru, altlıq, sadələşdirilmiş salon və UV düzülüşü əlavə et. Boya/metal/şüşə/rezin/plastik üçün PBR materialları, texture atlasing və texel sıxlığı hesabatı hazırla. Zədələnmə mərhələlərində təkərlər və gameplay collider sabit qalmalıdır. Performance limitini aşmadan nə əldə edildiyini göstər. Başqasının generatoruna paralel yazma; hər model üçün əvvəl ownership al.

## Audio agenti

48 kHz / 24-bit WAV: mono positional effects, stereo music. Mühərrik loop-ları nominal RPM və load etiketləri, sample loop markerləri ilə ayrı saxlanmalıdır. Golf: turbo I4; E46: atmosfer I6; STI: turbo flat-4; Ranger 2019 üçün seçilmiş 2.0 biturbo diesel bazar variantını açıq qeyd et. Müasir avtomobillərin fərdi audio ailəsi müəyyənləşdirilmədən köhnə səsləri modelə dəqiq uyğun kimi etiketləmə. Sintezlə real recording fərqini metadata-da göstər. Endpoint continuity, DC, sample peak və decoding yoxlaması apar; dinlənilməyən materialı bədii təsdiqlənmiş kimi qeyd etmə. Musiqi stem-ləri eyni uzunluq, tempo, bar count və başlanğıc sample ilə sinxron olmalıdır. `assets/audio/game` başqa iş axınına aiddir; razılaşdırılmadan dəyişmə.

## Harbor / tras agenti

13 hazır liman propunu və Dock Loop-un 2200 m/14 m ilkin geometriyasını baza götür. Gameplay yolunu kolliziyadan təmiz saxla. Start grid, checkpoint, pickup və reset anchor-ları spline əsasında yarat. Bir istiqamətdə ordered checkpoint traversal, grid spacing, çıxış yolları, AI xəttinin collision-dan məsafəsi və resetdən sonra yola qayıtma testi tələb et. Yalnız həndəsi yoxlama ilə AI yarışı tamamlayır iddiası etmə. Collision, vizual mesh, LOD, instancing və material sayı ayrı hesabatda olsun.

## Qəbul agenti

İşçilər export-u bitirdikdən sonra kataloqu yenilə. Bütün LOD GLB-lərdə finite vertex, index/buffer hüdudları, real triangle/material sayı, dörd wheel pivot, seçilmiş scene hierarchy və collision mövcudluğunu yoxla. Wicked import smoke-test nəticəsini tam gameplay inteqrasiyası ilə qarışdırma. Qalereyada bütün maşınların görünməsi və faktiki GLB-nin yüklənməsi ayrıca UI yoxlamasıdır. 1080p60 yalnız hədəf səhnə, hardware, yarışçı sayı və p95/p99 frame-time ölçüləri ilə təsdiqlənə bilər.

## Harbor konsepti üçün image-generation prompt

Create a premium art-direction reference for an original realistic arcade combat racer, with high-speed visual readability and restrained energy effects. Sunset cargo harbor with containers, cranes, wide asphalt, barriers and sparse cyan pickup markers. Foreground cyan performance hatch in three-quarter view with realistic proportions, dark grille and silver wheels; amber coupe and red pickup behind. Grounded painted metal, asphalt and glass, restrained bloom, no watermark or text. This is an art-direction concept, not a game screenshot or usable mesh. Preserve clear drivable road composition. Output one landscape concept image.

Saxlanmış nəticə: `assets/concepts/harbor-direction-v1.png`. Bu konsept hazır 3D modellərin keyfiyyətini sübut etmir.
