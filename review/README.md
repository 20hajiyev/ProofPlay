# Asset qalereyası

Layihə kökündə `python tools/serve_asset_review.py` başladın və http://127.0.0.1:8766/review/index.html açın. Server yalnız lokal kompüterdə işləyir və yalnız `review` / `assets` fayllarını təqdim edir. Dayandırmaq üçün Ctrl+C.

Kataloqu yeniləmək: `python tools/build_asset_catalog.py`. Generasiya zamanı deyil, bütün işçilər faylları yazıb bitirdikdən sonra işlədin. Struktur yoxlaması bədii keyfiyyəti və oyun performansını təsdiqləmir.

3D pəncərə lokal saxlanan Google model-viewer 4.0.0 kitabxanasından istifadə edir. Mənbə: https://github.com/google/model-viewer ; Apache-2.0 bildirişi `vendor/LICENSE-model-viewer.txt` faylındadır. GLB-ni model siyahısından seçib fırlatmaq və yaxınlaşdırmaq olar. `file://` ilə açıldıqda brauzer 3D yüklənməsini bloklaya bilər; HTTP serverdən istifadə edin.

PNG-lər faktiki Blender modellərinin renderləridir. Ayrı konsept bölməsindəki AI şəkli oyun renderi deyil. Səs faylları prosedural sintezdir, real avtomobil mühərriki qeydləri deyil.
