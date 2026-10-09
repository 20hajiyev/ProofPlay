# Mühərrik bazası

| Sahə | Dəyər |
|---|---|
| Upstream | https://github.com/turanszkij/WickedEngine |
| Tag | `v0.72.106` |
| Kilidlənmiş commit | `27c0df160d738925474a2181d3f88bfd59edaefe` (2026-08-03) |
| Yerləşmə | `engine/WickedEngine` (git submodule) |
| Jolt | 5.6.0 (`WickedEngine/Jolt/Core/Core.h` ilə yoxlanıb) |
| Avtomobil komponenti | `RigidBodyPhysicsComponent::Vehicle` — `wiScene_Components.h:457` |
| Toolchain | Visual Studio 2022 Community, VS daxili CMake 3.31.6, x64 |

## Üçüncü tərəf

| Kitabxana | Versiya | Commit |
|---|---|---|
| miniaudio | 0.11.25 | `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d` (`third_party/miniaudio`) |

## Patch növbəsi

| ID | Fayl | Səbəb | Test |
|---|---|---|---|
| P-001 | `engine/patches/0001-expose-vehicle-constraint.patch` (`wiPhysics.h`, `wiPhysics_Jolt.cpp`, +12 sətir) | Public API maşın başına gear ratio, torque əyrisi, əyləc, şin modeli və RPM/gear/slip telemetriyası vermir. `wi::physics::GetVehicleConstraint()` Jolt `VehicleConstraint*`-i qaytarır | `handling: definition tuning reaches the Jolt controller` |

| P-002 | `engine/patches/0002-deterministic-body-creation.patch` (`wiPhysics_Jolt.cpp`, 1 sətir + şərh) | Paralel body yaradılışı Jolt BodyID sırasını thread vaxtlamasından asılı edirdi; eyni yarışlar ayrışırdı | `determinism: the same 20-car S01 race twice gives identical state` |
| P-003 | `engine/patches/0003-toon-crease-ink.patch` (`outlinePS.hlsl`, `wiRenderer.*`, `wiRenderPath3D.*`) | Kontur yalnız dərinlikdən (siluet) tapılırdı; PS2 cel-shading üslubu qırış/panel xətləri də istəyir. `setOutlineCrease(cos)` normal buferini (visibility surface) açır və normal bu bucaqdan çox dönəndə mürəkkəb çəkir | Oyunda ekran şəkli (D-052), kadr vaxtı 6.06 ms (dəyişmədi) |

Submodule yeniləndikdən sonra patch-lər sıra ilə tətbiq olunur:

```
git -C engine/WickedEngine apply ../patches/0001-expose-vehicle-constraint.patch
git -C engine/WickedEngine apply ../patches/0002-deterministic-body-creation.patch
```

Hər ikisi pristine v0.72.106-ya təmiz tətbiq olunur (yoxlanıb). Patch yoxdursa, CMake configure dayanır.

Oyun kodu Jolt başlıqlarını birbaşa daxil edir. Wicked Jolt-u `JPH_DEBUG_RENDERER` ilə yığır (yalnız qovluq səviyyəsində), ona görə `racer_runtime` eyni define-ı PUBLIC ötürür. Uyğunsuzluğu `JPH::VerifyJoltVersionID()` runtime-da tutur.

## Wicked davranış qeydləri (v0.72.106, ölçülüb)

- `RunPhysicsUpdateSystem` `dt <= 0` olanda body yaratmadan qayıdır.
- Fizika aktiv, simulyasiya söndürülü olanda bütün body-lər transformlarına teleport edilir (editor pauza rejimi).
- `OverrideWehicleWheelTransforms` yalnız `IsSimulationEnabled()` olanda işləyir.
- Default `TIMESTEP` 1/60-dır, başlıqdakı şərh isə "120 FPS" deyir.
- Maşın üçün Wicked 10x uzununa şin impulsu tətbiq edir (köhnə Jolt bug-ı üçün workaround).
- 2WD rejimi ön təkərləri sürür.
- `ActivatePath()` path-in `Start()`-ını çağırır, `Load()`-u yox.
- `Application::Initialize` `alwaysactive`-i komanda sətrindən yenidən yazır.
- `LoadModel(Scene&, ...)` birbaşa hədəf səhnəyə serialize edir; canlı səhnə üçün staging + `Merge` lazımdır.
- `helper::screenshot(swapchain, name)` adı atır və clipboard-u dəyişir.
- Gravitasiya `scene.weather.gravity` = −10 m/s².
- Realistic sky bu səhnədə təsadüfi anlarda qara render olunur (6 s mavi, 16–20 s qara; WeatherComponent entity-si, günəşin komponentə yazılması və aerial perspective-in söndürülməsi kömək etmədi; weather sistemi işıqlardan əvvəl `Wait` ilə işlədiyi üçün job sırası da deyil). Həll: gradient göy (horizon/zenith) + məsafə dumanı. Hava həmişə `scene.weathers[0]` komponentində qurulur — engine onu hər kadr `Scene::weather`-ə kopyalayır.
- `SetRealisticSkyAerialPerspective(true)` bizim səhnədə bütün göyü qara göstərir (v0.72.106); istifadə edilmir.
- `.wiscene`-ə cook edərkən `resourcemanager::SetMode(EMBED_FILE_DATA)` lazımdır; əks halda GLB-yə gömülmüş teksturalar yalnız adla yazılır və runtime-da ağ görünür.

## Build

```
cmake -S . -B build/game -G "Visual Studio 17 2022" -A x64
cmake --build build/game --config Release
ctest --test-dir build/game -C Release --output-on-failure
```

Yalnız stok editor/template lazımdırsa: `cmake -S engine/WickedEngine -B build/wicked -DWICKED_TESTS=OFF -DWICKED_IMGUI_EXAMPLE=OFF -DWICKED_ENABLE_SYMLINKS=OFF`.

## Yoxlanmış (2026-09-27, bu kompüter)

- Stok Release build: `Editor.exe`, `Template_Windows.exe`, `WickedEngine.lib` — xətasız.
- Oyun layihəsi mühərriki `add_subdirectory` ilə link edir.
- GLTF import pəncərəsiz DX12 device ilə işləyir (`tools/import_check`).
- 60 təkrar importda RAM +0.12 MiB, VRAM 0 artım (`import_leak_D01`).
- miniaudio cihazsız engine: pitch, PCM-dəqiq start, sinxron stem, sound group, WAV streaming (`audio_fixture`).
  Default cihaz: WASAPI, Realtek.

## Hələ yoxlanmamış

- Referens avadanlıqda (GTX 1660 / RX 5500 XT) işə düşmə.
- Shader kompilyasiyası və render — import yoxlayıcısı render etmir.
- Jolt avtomobilinin 120 Hz addımla işləməsi (M1).
