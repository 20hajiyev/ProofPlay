# Hava şəraiti planı (D-078, gələcək üçün)

Sahib (2026-10-03): "müxtəlif hava şəraitləri də olar, bunu gələcək üçün planlayarsan".
Bu sənəd plandır, kod hələ yazılmayıb. Hər mərhələnin ölçülən qapısı var (layihə qaydası).

## Məqsəd

Liman pistlərində havanın həm görünüşü, həm sürüşü dəyişməsi. Stil komiks/cel-shade-dir: yağış
fotoreal damcı deyil, mürəkkəb ştrixləri olan "çəkilmiş" yağış zolaqlarıdır (Auto Modellista və
Jet Set Radio ruhunda).

## Hava növləri

| Hava | Görünüş | Sürüşə təsir | Səs |
|---|---|---|---|
| Aydın (indiki) | — | — | — |
| Buludlu | sıx cel buludlar, yumşaq kölgə, günəş halosu yoxdur | — | yüngül külək |
| Yağış | çəkilmiş yağış ştrixləri, yaş yolda tünd ton və işıq zolaqları, təkərlərdən su sıçrantısı, kokpitdə şüşədə damcılar (silgəclər artıq işləyir) | tutuş ×0.85, əyləc yolu +15% | yağış loopu, təkər şırıltısı |
| Fırtına (gecə) | güclü yağış, şimşək çaxanda ekran mürəkkəb siluetinə keçir (1–2 kadr ağ-qara) | tutuş ×0.8, yan külək itələməsi | ildırım, külək |
| Duman | cel duman qatı, uzaq obyektlər tək tonlu siluetlər, faralar konus şəklində | görmə məsafəsi (AI və oyunçu üçün eyni) | uzaqda gəmi siqnalı |

Qar liman ruhuna uyğun deyil, sonraya saxlanılır.

## Texniki yanaşma

1. **Core (TDD):** `racer/Weather.h`.
   - `WeatherState` (növ, intensivlik 0..1, keçid vaxtı).
   - `WeatherEffects(state)` → tutuş, əyləc, görmə və külək dəyərləri; deterministikdir (seed).
   - **Testlər:** yağışda tutuş azalır; keçid hamardır; eyni seed eyni nəticəni verir; AI və oyunçu eyni təsiri alır.
2. **Fizika:** `ApplyPerformance` kimi bir `ApplyWeather(VehicleDefinition&)` (tutuş və əyləc).
   - **Fizika testi:** 100-0 əyləc yolu yağışda uzanır.
3. **Görüntü:**
   - **Yağış:** kameraya bağlı bir neçə min instanced kvadrat (bir draw call), çəkilmiş ştrix fakturası ilə.
   - **Yaş yol:** `toon_asphalt` üçün ikinci "yaş" variant (tünd + parıltı zolaqları). Hava ilə material faktura dəyişimi və ya qarışdırma.
   - **Sıçrantı:** təkər arxasında qısa ömürlü toon hissəciklər.
   - **Şimşək:** post-process-də 1–2 kadrlıq kontrast və mürəkkəb filtri.
   - **Duman:** WeatherComponent fog + rəng; cel buludlar daha sıx.
4. **Səs:** `generate_game_audio.py`-də prosedural yağış, külək və ildırım səsləri.
5. **Kontent:** event JSON-a `"weather"` sahəsi (köhnə fayllar aydın qalır); karyerada bəzi yarışlar yağışlı və ya gecə fırtınalı olur.
6. **Menyu:** sərbəst yarışda hava seçimi.

## Ölçülən qapılar

- **Kadr büdcəsi:** yağışda 12 maşınla S01-də kadr vaxtı +1 ms-dən çox artmamalıdır (indi 6.06 ms).
- **Yaddaş:** flow soak leak gate PASS (hissəciklər hovuzdan, sızma yox).
- **Determinizm:** eyni seed və hava ilə 20 maşınlıq yarış iki dəfə eyni nəticə verir.
- **Oxunaqlılıq:** yağış və fırtınada ekran şəkilləri ilə HUD və pickup-lar aydın görünür.

## Ardıcıllıq

1. Core və fizika (yağış tutuşu).
2. Yağış görüntüsü və yaş yol.
3. Səs.
4. Fırtına, şimşək və duman.
5. Event və menyu inteqrasiyası.
