# Driver character design: why it looks bad and how to fix it

Research note, 2026-10-03. Scope: the procedural drivers in `tools/car_cabin.py` (`driver()`, `build_head()`)
and `tools/generate_driver.py`, rendered by the Wicked cartoon shader plus the P-003 post-process outline
(thickness 2.2 px, crease 0.8). No code was changed for this note.

## Xülasə (Azərbaycanca)

- **Əsas səbəb sifətin quruluşu deyil, işığa nəzarətin olmamasıdır.** Kəllə 112×56 UV-kürədir, hamar normallarla.
  Toon shader-in sərt kölgə sərhədi bu normalları izləyir, ona görə kölgə ya heç düşmür, ya da təsadüfi ləkə kimi düşür.
  Guilty Gear Xrd komandası məhz bunu yazır: üzdə hər əsas formanın normalını əllə düzəldiblər.
- **Kölgə formaları yumşaq vertex color-dur.** Oyunda onlar qrafik kölgə kimi yox, kir və ya əzik kimi görünür.
  Kəskin kənarlı forma (face-corner rəngi) və tintli kölgə rəngi lazımdır.
- **30-a yaxın nazik mürəkkəb borusu** yarışda 0.3–1 piksel enində olur: ya itir, ya titrəyir.
  Onların yerinə az sayda, qalın, faktura ilə çəkilmiş xətt olmalıdır.
- **Gözlər həndəsi baqdır.** Göz ağı, iris və bəbək dərinin 1–4 mm altında, kəllənin içində qalır.
  Model vərəqində gözlər ">  <" kimi yumulu görünür.
- **Model vərəqi (EEVEE) oyunu göstərmir.** Orada toon, kontur və vertex color yoxdur, ona görə qərarlar kor verilir.
- **Proporsiya demək olar real ölçülərdədir** (ANSUR II). Baş bir az kiçik, boyun nazik, çiyinlər geniş.
  Kiçik ekran ölçüsündə oxunsun deyə başı ~10% böyütmək, boynu qalınlaşdırmaq tövsiyə olunur.
- **Post-process kontur qalsın.** Sifət daxilindəki xətlər üçün inverted hull yox, faktura xətləri seçilsin.
  Hull yalnız saç və baş siluetində dəyişən qalınlıq lazım olsa, NoInk materialı ilə əlavə edilə bilər.

---

## 1. Diagnosis: what is actually wrong

Evidence: the model-sheet renders in `assets/characters/driver/` (EEVEE, 2026-10-02 21:26) and the in-game
face debug shot `build/sandbox_reports/anim/ds_face.png` (`RACER_DEBUG_CAM=face`, 2026-10-02 20:44).
`car_cabin.py` was edited later, at 23:00, so check every point against a fresh in-game shot.

| # | Symptom (seen in the images) | Cause in the code |
|---|---|---|
| D1 | In game, the face has **no light/shadow structure**. It is one flat, near-white skin tone with lines drawn on top, so it reads as a line drawing taped onto a mannequin. | The skull is `_sphere(seg=112, rings=56)`, sculpted by Gaussian bumps in `relief()`, with `use_smooth` and automatic normals (`car_cabin.py` 969–1001, 1113–1114). The Wicked cartoon terminator is `smoothstep(0.005, 0.05, NdotL)` on the vertex normal (`shaders/brdf.hlsli` 134–146). With a frontal key light every normal on the face is lit. With a side light the bumps throw arbitrary blotches. Nobody controls where the shadow falls. |
| D2 | The authored "ink shadow shapes" (under brow, cheek, nose, lip, jaw) read as **grey or pink smudges**: dirt, bruising, lipstick. | `paint()` writes tones into a `POINT`-domain colour attribute (`car_cabin.py` 1123–1129, 1264–1271). Per-vertex colour interpolates across the neighbouring quads, so every shape gets a ~1-quad soft fade. D-068 picked this softness on purpose, to avoid the outline pass inking material edges. The colours are also grey multipliers (0.38, 0.6, 0.74), which give muddy shadow instead of a designed shadow colour. |
| D3 | About **30 thin ink tubes per face**: crow's feet, cheek hatching, eye bags, lid crease, lower lid, frown lines, lip crease, nostrils, nose-side and smile lines, plus a scar. In game they sparkle or vanish; up close they look scratchy. | `tube_along(..., 0.0008–0.0027, M['ink'])` (`car_cabin.py` 1236–1263). Section 3.3 has the screen-size numbers: at chase distance these lines are 0.3–0.9 px wide. They are also round tubes standing up to 1.5 mm proud of the skin, so the post-process can ink their silhouettes as well. |
| D4 | **The eyes are broken.** In the EEVEE portraits they read as closed `> <` squints. In the older game shot they are big yellow discs that stare. | The eye white is placed at `on_face(..., -0.011)`, 11 mm *below* the skin, with a 0.0145 × 0.5 = 7.25 mm forward radius. Its front sits about 3.8 mm inside the closed skull. The iris front (about −1.5 mm), the pupil and the glint are buried too. Only the parts of the lash line and lid tubes that poke through stay visible (`car_cabin.py` 1229–1240). |
| D5 | **Hair**: a hard horizontal hairline cut across the forehead (a "helmet"), and 58 separate locks, each outlined in ink, which is noisy. From the side (`driver_profile0.png`) the locks read as spikes and shards. | `shell('hair_mass', …)` cuts the cap with a box-like `keep()` predicate. Then 18 + 40 `lock()` ribbons follow, each its own small surface with automatic normals (`car_cabin.py` 1156–1179). |
| D6 | The **neck** reads as a thin pale pillar, the **head looks small** on a broad red jacket, and the three heads differ only in hair and beard colour. | Head breadth 14.8 cm and length 19.2 cm (`rx, ry, rz = 0.074, 0.096, 0.122`, line 953). The upper neck radius is 0.05 m, about 31 cm in circumference (line 937). Shoulders run to about 53 cm across the deltoid spheres, before the jacket (lines 887, 1291–1297). Section 2.5 compares these with ANSUR II. |
| D7 | The art direction is judged from **renders that do not show the game**. | `generate_driver.py` `review()` renders with EEVEE and a plain Principled BSDF. That means no cel step, no outline, and the vertex colours are not wired into the material. So D1–D3 cannot be seen on the model sheet, and D4 shows up there but not in the earlier in-game shot. |

**In one sentence:** the face is modelled as geometry (bumps plus tubes), but a cel-shaded face is made of
*controlled normals, designed flat shadow shapes and a few bold lines*. None of those three is under
authored control today.

---

## 2. What the primary sources say

### 2.1 Guilty Gear Xrd: Junya C. Motomura, GDC 2015 (Arc System Works)

Sources: the talk's speaker notes as PDF, [Motomura_Junya_GuiltyGearXrd.pdf](https://www.ggxrd.com/Motomura_Junya_GuiltyGearXrd.pdf),
on Arc System Works' official Xrd site; the session page on [GDC Vault](https://www.gdcvault.com/play/1022031/GuiltyGearXrd-s-Art-Style-The);
and ASW's own [announcement of the video](https://www.arcsystemworks.com/guilty-gear-xrds-art-style-the-x-factor-between-2d-and-3d-talk-from-gdc-2015-is-now-available-online/).
Slide numbers below refer to the PDF.

- **Principle (slide 14).** Remove anything that reads as "3D". The shader math is always "correct", but
  correct is not good enough. Everything on screen has to be an intentional choice, which in practice means
  hand-crafting by artists. Instant feedback mattered: the team had a real-time shader preview in the
  modelling tool that matched the game.
- **Data lives in vertices, not textures (slide 15).** About 40,000 triangles per character, details modelled in
  geometry, no normal maps. Vertex normals, vertex colours and UVs carry the data because they interpolate
  without pixelation in close-ups.
- **Why cel shading is hard (slide 16).** A surface is either lit or not. Any small noise in the normal turns
  into a large blotch, so the look needs exact control over where the shades fall.
- **Only three inputs matter (slide 17).** The shader is a `step` on threshold, light vector and normal.
  The job is to control all three.
- **Threshold (slide 18).** A vertex-colour channel offsets the lit/shaded threshold per vertex. It works like
  painted occlusion: a value of zero means always in shadow. Vertex colour was preferred over textures
  because it gives clean results at any resolution and is quick to tune.
- **Light (slide 19).** Each character has its own light vector, chosen to light their pose well. In cut-scenes
  it is animated per frame.
- **Normals (slide 20).** Automatically computed normals are often not what the artist meant. The team edited
  the normals on every major feature of the model. Faces in particular were hand-crafted for a clean anime look.
- **Shadow colour (slides 21–22).** Shading is not "lit colour × ambient". A base texture gives the lit colour
  and a tint texture gives how dark and what hue the shadow is. Skin shadow is tinted red because of the flesh
  under it. The textures are flat colour lookups with no painted detail.
- **Outline (slide 23).** An inverted hull is generated in the shader and pushed out along the normals.
  Vertex colour controls line width, including erasing it. They chose this over a post effect for two reasons:
  you see it in the modelling viewport, and you control it per vertex.
- **Inner lines (slide 24).** Surface lines cannot come from the hull. Plain texture lines pixelate up close, so
  lines are drawn as axis-aligned beams in the texture, and the UVs are laid along them. How far the UVs overlap
  a beam sets the line's thickness. The cost is distorted UVs, which does not matter when the textures carry
  no other detail.
- **Animation (slides 26–28).** Limited animation (every frame a key, no interpolation), around 500 bones,
  heavy scale animation, and deliberate per-frame deformation to break 3D perspective consistency.

### 2.2 Team Fortress 2: Valve, NPAR 2007 (Mitchell, Francke, Eng)

Source: [Illustrative Rendering in Team Fortress 2](https://cdn.cloudflare.steamstatic.com/apps/valve/2007/NPAR07_IllustrativeRenderingInTeamFortress2.pdf), Valve's own paper.

- Shading conventions the authors took from commercial illustration: shadows shift warm to cool and go cool,
  not black. Saturation rises at the terminator, which is often reddened. High-frequency detail is left out
  where possible. Interior details such as clothing folds echo the silhouette shapes. Silhouettes are stressed
  with rim highlights (section 3).
- Characters must be readable at many distances. The nine classes were checked **in pure silhouette** during
  concept, and proportions were designed for distinct silhouettes (section 4.1).
- Colours are close to real but with more saturation and value contrast (section 4.3).
- The diffuse term is warped through an artist-painted 1D ramp, Half Lambert followed by the warp, so the
  terminator is tight while shape information survives on the dark side (section 5.1).

### 2.3 Telltale / The Wolf Among Us (TWAU)

**Gap:** Telltale never published a technical or art breakdown of TWAU's character shading. I found no GDC
talk, art book text or developer post that describes how the faces were built. What first-party statements
exist:

- For *The Walking Dead*, the predecessor in the same engine and pipeline, Telltale described an ink and
  watercolour look meant to match Charlie Adlard's comic, with **textures designed with ink lines and
  watercolour streaks in mind** ([Kotaku, 2011, quoting Telltale](https://kotaku.com/see-the-walking-dead-video-games-superb-comic-book-look-5824043)).
  Designer Sean Vanaman said they want the characters to "feel drawn even when they're moving and talking"
  ([Game Informer, 2011](https://gameinformer.com/games/the_walking_dead_episode_one_a_new_day/b/pc/archive/2011/07/22/telltale-s-walking-dead-art-tease.aspx)).
  So in the Telltale comic look, **the face lines are painted into the texture, not modelled**.
- For the 2026 *TWAU Remastered*, game director Matt Saia says the art style was kept as "sacred".
  Every scene was relit, and the characters were rebuilt with 5–6× the detail without being redesigned
  ([PlayStation Blog, 2026-10-01](https://blog.playstation.com/2026/10/01/return-to-fabletown-with-the-wolf-among-us-remastered-out-october-29/);
  [Nintendo Everything interview](https://nintendoeverything.com/the-wolf-among-us-remastered-interview-telltale-on-why-the-game-is-returning-improvements-detailed-more/)).
  So the look depends heavily on **per-scene authored lighting**.
- Telltale's GDC Europe 2014 talk on adapting *Fables* (Chris Schroyer) covers design, not rendering
  ([summary](https://www.gamedeveloper.com/design/video-how-telltale-adapted-fables-into-i-the-wolf-among-us-i-)).

Anything more specific about TWAU faces is observation, not sourced: the realistic adult proportions, heavy
painted shadow under the brow and nose, a hard jaw shadow, a limited and saturated neon palette. The current
code comments (D-065, D-078) rest on that kind of observation too. The remaster's developer commentary and
concept-art gallery (October 29, per the PlayStation Blog) may become a primary source. Re-check it then.

### 2.4 Other first-party references

- **Hi-Fi RUSH** (Tango Gameworks, GDC 2024, Kosuke Tanaka and Takashi Komada): a deferred toon renderer with a
  dedicated "toon face shadow" solution, which shows that faces needed special handling even in a modern
  toon renderer ([GDC Vault session page](https://gdcvault.com/play/1034251/3D-Toon-Rendering-in-Hi)).
  The details are behind the Vault paywall and I did not review them.
- **Unity Toon Shader** (official docs): "Shading Position Maps" are textures that fix where shadows fall on
  a material, independent of the light ([Unity docs](https://docs.unity3d.com/Packages/com.unity.toonshader@0.8/manual/Basic.html)).
  This is the texture equivalent of GGXrd's vertex-colour threshold.

### 2.5 Proportions: measured adult reference

Source: the 2012 US Army anthropometric survey ANSUR II, Gordon et al. 2014
([DTIC ADA611869](https://apps.dtic.mil/sti/pdfs/ADA611869.pdf), [Internet Archive copy](https://archive.org/details/DTIC_ADA611869)).
Values are male 50th percentiles or means, read from the OCR text of the report.

| Dimension | ANSUR II male | Current driver | Comment |
|---|---|---|---|
| Head breadth | 15.4 cm | 14.8 cm (`2·rx`) | about 4% narrow |
| Head length | 19.9 cm (mean 7.85 in) | 19.2 cm (`2·ry`) | about 4% short |
| Menton–sellion (chin to nose bridge) | 12.3 cm | about 12 cm | OK |
| Neck circumference | 39.5 cm | about 31–40 cm (radius 0.05 at the top, 0.064 at the base, `flat=0.9`) | the top half is thin; that is D-070's "pillar" |
| Biacromial breadth | 41.5 cm | 40 cm (shoulder joints at ±0.2) | OK |
| Bideltoid breadth | 50.9 cm | about 53 cm before the jacket (deltoid spheres r 0.064 at ±0.2) | broad, and the jacket adds more |
| Hand length | 19.3 cm | check `_hand_back` (l=0.075) plus fingers | verify |

So the body is close to real, and the head is slightly *under* real size. Real proportions are a legitimate
choice for a TWAU look. But TF2's paper ties proportion choices to readability at distance and in silhouette
(§2.2). The driver is mostly seen small, from behind, or through glass. A deliberately larger head and hands
(+8–12%) and a full-size neck would read better. That is my design judgement, applied with the TF2
silhouette test, not a sourced number.

---

## 3. What the engine implies (from the code in this repo)

### 3.1 The cartoon shader is a hard step on the vertex normal

`brdf.hlsli` 134–146: `NdotL = smoothstep(0.005, 0.05, NdotL)` and `NdotH = smoothstep(0.98, 0.99, NdotH)`.
Under `CARTOON`, Fresnel is also stepped, `F = smoothstep(0.1, 0.5, F)` (`surfaceHF.hlsli` 310–312).
This is exactly GGXrd's `step(threshold, N·L)` (§2.1, slide 17). Three consequences:

1. **Whoever controls the vertex normals controls every face shadow.** GGXrd's normal-editing method
   (slide 20) carries over directly.
2. The Wicked glTF importer keeps imported normals and only computes them if none exist
   (`Editor/ModelImporter_GLTF.cpp` 1170–1176, 1616–1619). Custom normals authored in Blender should reach
   the game. Verify that `import_check --cook` does not recompute them.
3. There is **no per-vertex threshold input** (GGXrd slide 18). The nearest built-in hook is the material's
   subsurface-scattering term. In cartoon mode, `sss.a` shifts the terminator ("wraparound") and `sss.rgb`
   tints the shadow side (`brdf.hlsli` 136–144; `MaterialComponent::subsurfaceScattering`). That is a
   per-material, not per-vertex, form of GGXrd's tinted shadow (slide 22) and TF2's reddened terminator (§2.2).

### 3.2 The outline is post-process, and its crease part also reads normals

`outlinePS.hlsl` with patch P-003:

- **Depth edges:** a Sobel filter on linear depth, sampled 2.2 px apart, fires when the gradient exceeds
  `0.1 × depth`. The Sobel weights sum to 4, so a depth step has to be at least ~2.5% of the viewing distance:
  about 2 cm at 0.8 m (garage or taunt close-up) and about 14 cm at 5.5 m (chase camera). **Facial relief
  never produces depth ink at race distance.** Only the head silhouette against the seat or background does.
- **Crease edges:** ink is drawn where the *shading normal* turns more than `acos(0.8)` ≈ 37° between pixels
  2 px apart (`t = round(2.2 × 0.75)`) on the same surface. Custom normals therefore **also decide where
  crease ink appears**. A designed break of more than 37° between two face planes gives a free, view-correct
  ink line, such as jaw to under-jaw or brow to eye socket. Changes kept under 37° stay clean.
- **Outlines can be switched off per material:** any material whose name contains `NoInk` or `Sheet` gets
  `SetOutlineEnabled(false)` (`CarVisual.cpp` 29–30). That is the hook for keeping ink geometry and eyes from
  being outlined a second time.
- **Line width is constant in pixels**, so at chase distance the 2.2 px silhouette line is wider than any
  facial line (§3.3).

### 3.3 Screen size of the current face lines (derived)

Assumptions: 1080p, vertical FOV 62° (D-078), the chase camera 5.0 m behind the car (driver at ≈5.5 m),
and the face debug shot as the close-up case (`ds_face.png`, head ≈ 470 px tall).

| Feature (radius in code) | Width | Chase cam (≈163 px/m) | Close-up (≈1930 px/m) |
|---|---|---|---|
| Crow's foot, eye bag (0.0008–0.001) | 1.6–2 mm | 0.3 px | 3–4 px |
| Cheek hatch, lid crease, frown (0.0009–0.0012) | 1.8–2.4 mm | 0.3–0.4 px | 3.5–4.6 px |
| Mouth (0.0019) | 3.8 mm | 0.6 px | 7 px |
| Lash line (0.0027) | 5.4 mm | 0.9 px | 10 px |
| Brow (0.0058–0.0078) | 12–16 mm | 2–2.5 px | 22–30 px |
| Post-process silhouette | 2.2 px | 13.5 mm equivalent | 1.1 mm equivalent |
| Whole head (rz·2) | 244 mm | ≈ 40 px | ≈ 470 px |

At race distance, everything finer than the brows is sub-pixel noise, which matches TF2's advice to leave
out high-frequency detail (§2.2). Up close, the hatching becomes the heaviest-looking thing on the face
while the silhouette line is thin. The line hierarchy flips between the two views.

### 3.4 Inverted hull or post-process?

- Keep the **post-process** for silhouettes. It already covers cars and the world, and switching costs time
  and visual consistency.
- An **inverted hull** gives what GGXrd valued (slide 23): per-vertex width, and lines that get thicker as
  geometry gets closer. In Blender it is a Solidify modifier with Flip Normals and a Material Offset to a dark
  hull material, with a vertex group scaling the thickness
  ([Blender Solidify docs](https://docs.blender.org/manual/en/4.5/modeling/modifiers/generate/solidify.html)).
  The existing `mat()` already exports single-sided materials (`use_backface_culling = True`), which a hull
  needs. The hull material must be named `…_NoInk` so the post pass does not ink the ink.
  - Cost: the head's triangles roughly double.
  - Use: only worth it for the hair and jaw silhouette in close-ups.
  - Rank: low (R8).
- For **interior face lines**, neither method works. Follow Telltale (lines painted into textures, §2.3) and
  GGXrd (texture lines with UVs laid along axis-aligned beams, slide 24). Texture lines scale with the face,
  sit flat on it, and create no depth or crease edges.

---

## 4. Recommendations, ranked by visual impact

Each item gives the change, the source behind it, and the code it touches. Gate every item on in-game
screenshots (`RACER_DEBUG_CAM=face|face_side`, garage menu, chase cam) under day, sunset and night, plus
face/triangle count and frame time, per CLAUDE.md. Record each in `docs/DECISIONS_M2.md`.

### R1. Fix the eyes (bug), then simplify them. Impact: very high, effort: small

- **Bug fix:** the eye white, iris, pupil and glint sit 1–4 mm inside the closed skull (D4). Choose one of two:
  - cut an almond opening in the skull: delete the faces whose centre falls inside the eye outline, in the same
    way `shell()` uses `keep()`;
  - or seat the eye parts on the skin with `on_face(d, +0.0005)` and flatten them to discs.
  
  Lines 1229–1235.
- **Simplify** for the style (GGXrd slide 14: remove what reads as 3D; TF2: leave out high-frequency detail):
  - one bold upper-lid/lash shape: a flat ribbon or texture line, about 3× the current width, overlapping the iris;
  - a darker, smaller iris;
  - no glint in the race LOD;
  - drop `lower_lid`, `eye_bag` and `crows_foot` (lines 1239–1243).
- Name the eye materials `…_NoInk` so the post pass does not ring each sphere. The iris already uses that
  suffix; the eye white (`M['eye_white']`) and `M['ink']` do not.

### R2. Author the face normals. Impact: very high, effort: medium

GGXrd slides 16–17 and 20; engine facts in §3.1–3.2.

- Keep the sculpted positions from `surf()`/`relief()` for silhouette and profile. **Take the normals from a
  simpler proxy:**
  - forehead: a nearly flat plane, tilted up;
  - temples: planes turned 60–80° to the side;
  - cheeks: two broad planes, front-left and front-right;
  - muzzle: a gentle cylinder around Z;
  - under-jaw: a plane facing down, more than 37° away from the jaw-side plane, so the crease pass inks the jaw;
  - eye sockets: normals tilted down, so a top light shades them as a single shape.
- **Implementation (scripted, no GUI):**
  1. Compute the proxy normal per vertex analytically in `build_head()`, for example a weighted blend of the
     plane normals above with the ellipsoid normal (no relief).
  2. Apply it with `skull.data.normals_split_custom_set_from_vertices(normals)`
     ([Blender API, Mesh](https://docs.blender.org/api/4.5/bpy.types.Mesh.html)).
  3. Or build the proxy as a mesh and use the Data Transfer modifier with Custom Normals and Nearest Face
     Interpolated mapping
     ([Data Transfer docs](https://docs.blender.org/manual/en/4.5/modeling/modifiers/modify/data_transfer.html);
     [Editing Normals docs](https://docs.blender.org/manual/en/4.5/modeling/meshes/editing/mesh/normals.html)).
     `export_apply=True` in `generate_driver.py` bakes the modifier.
  4. Make sure `join()` keeps custom normals. If it does not, set them after the join.
- **Do the same for the nose** (`nose` loft, lines 1197–1211): front and side planes, with the underside normals
  facing down so the cast shadow under the nose comes from shading, not paint.
- **Pick one key-light direction for the face** (GGXrd slide 19 uses one per character). The garage and taunt
  cameras can override the sun direction so it falls about 35° to the side and 30° above. That gives the
  TWAU-like half-face shadow.

### R3. Make the shadow shapes crisp, designed and tinted. Impact: high, effort: small to medium

GGXrd slides 18 and 21–22, TF2 §3, Unity Shading Position Maps.

- **Crisp edges:** switch the colour attribute from `'POINT'` to `'CORNER'` domain and write the tone per face
  corner of each painted face (lines 1123–1129, 1264–1271). The colour is then constant inside each quad and
  changes on the edge, with no fade. There is still no material boundary, so D-068's reason for avoiding
  separate materials still holds. With a 112×56 sphere the edges come out stepped. Either Taubin-smooth the
  boundary loop (as `shell()` already does) and cut the faces along it, or use a texture (next point).
- **Better still: one painted face texture.** Telltale paints lines and shadow into textures (§2.3). Give the
  skull a front-projected UV, rasterise the shadow polygons and ink lines in Python into a 1024² texture, and
  use it as the base-colour map. This turns R3 and R4 into a single asset and drops the ~30 tubes.
- **Shadow colour, not grey:** replace the `(0.38, 0.37, 0.4)` and `(0.74, 0.74, 0.74)` multipliers with a
  designed shadow skin colour: darker, more saturated, shifted toward red-brown in the terminator band and
  cooler in deep shadow (TF2 §3; GGXrd slide 22: skin shadows tinted red).
  - Also set the skin material's subsurface-scattering colour in `CarVisual.cpp`, by material family
    `toon_skin*`. The dynamic cel shadow then gets the same tint (`brdf.hlsli` 136–144).
- **Optional engine patch P-004 (the GGXrd method):** read one vertex-colour channel as a per-vertex offset on
  `NdotL` before the `smoothstep` in the cartoon path (GGXrd slide 18). Painted shadow shapes would then appear
  and disappear with the light instead of being fixed. It needs an engine patch with a measured gate (frame
  time unchanged, screenshot diff).

### R4. Replace the tube hatching with a few bold lines. Impact: high, effort: small

TF2 §3 (leave out high-frequency detail), §3.3 numbers, Telltale texture lines (§2.3), GGXrd slide 24.

- **Delete:** `crows_foot`, `cheek_hatch`, `eye_bag`, `lower_lid`, `lid_crease`, `frown_line`, `lip_crease`, the
  scar cross, `nose_side_ink`, and probably `smile_line` (lines 1236–1263). With R2 in place, the plane breaks
  carry that information as shadow.
- **Keep five line groups, two to four times wider:**
  - upper lash;
  - brow underside, or the brow mass itself;
  - one nose-side or nostril stroke;
  - the mouth;
  - the jaw shadow edge, if the crease pass does not already draw it.
- Make the kept lines **tapered** (thick in the middle, pointed ends) and lay them **flat**, either as texture
  lines (R3) or as flat ribbons hugging `on_face()` with a 0.3 mm lift, not as round `tube_along` tubes.
  Name the ribbon material `ink_NoInk`.
- **Minimum widths:** ≥ 3 px at close-up (≥ 1.6 mm) for anything kept. Anything thinner than 1 px at chase
  distance (< 6 mm) goes on a "close-up only" material the game hides in race view.

### R5. Fix the evaluation loop. Impact: high on every later decision, effort: small

GGXrd slide 14: the team needed a preview that matched the game.

- In `generate_driver.py` `review()`:
  - build a Blender toon preview: a Shader-to-RGB node into a Constant colour ramp at the same terminator,
    vertex colour multiplied in, and the skin shadow colour from R3;
  - render a silhouette-only pass (black on white), which is the TF2 concept-phase test (§2.2).
- Better, make the in-game `RACER_DEBUG_CAM=face`/`face_side` shots (with `--menu-script` for the garage) the
  model sheet of record. Add a chase-camera crop at real size (≈ 40 px head) so readability gets judged at the
  size players actually see.

### R6. Hair as a few big masses. Impact: medium to high, effort: medium

GGXrd slide 20 (normals on every major feature), TF2 §4.1 (silhouette; interior shapes echo it).

- Replace the 58 `lock()` ribbons with **5–9 large clumps**, for example a lofted cap with 3–5 points at the
  fringe and nape, plus 2–3 front strands. Taper the tips to points for a clear silhouette.
- **Transfer normals** from a smooth, slightly inflated hull of the whole hair onto all hair geometry
  (Data Transfer custom normals). The cel shadow then falls as one large shape across the hair instead of
  per lock. The crease pass will draw ink only where you design a > 37° break.
- **Hairline:** replace the horizontal cut in `keep()` (line 1158) with a curved hairline with temple recesses
  and a widow's peak, and let the fringe clumps overlap it. That removes the helmet edge.
- Delete `hair_ink` tubes (line 1175). The clump silhouettes and the crease pass already provide the ink.

### R7. Proportions and silhouette. Impact: medium, effort: small

ANSUR II (§2.5), TF2 §4.1.

| Change | Code | Value |
|---|---|---|
| Head scale | `rx, ry, rz` (line 953) | about +8–10% (for example 0.081, 0.105, 0.132): restores real size plus a small stylised boost for distance reading |
| Upper neck | `nk` profile (line 937) | radius about 0.06 along the full length (≈ 39.5 cm circumference); keep the taper only under the jaw |
| Shoulders | torso `secs` 0.78 to 0.9 (line 887), deltoid `scale` (line 1297) | narrow by about 2 cm per side, so bideltoid ≈ 51 cm with the jacket |
| Hands | `_hand_back`, `_finger` (lines 1348–1388) | +10% (gloved hands are a main gameplay read from the cockpit) |
| Variant silhouettes | v0, v1, v2 | give each head a distinct silhouette from behind (long hair, crop with a big beard mass, tall quiff) and check them in the silhouette pass (R5) |

### R8. Optional inverted hull for close-up silhouettes. Impact: low to medium, effort: medium

GGXrd slide 23; Blender Solidify docs.

Use this only if, after R1–R7, the 2.2 px post line looks too thin or too even in the garage close-up.

- Add a Solidify modifier to the head and hair: Flip Normals, Material Offset to a `hull_NoInk` material,
  thickness about 2–3 mm, and a vertex group that thins it at the nose, lips and eyes.
- Measure the triangle cost and the frame time.

---

## 5. Suggested order and gates

1. **R5 (preview) and R1 (eyes):** so later steps can be judged, and to remove the obvious bug.
2. **R2 (normals) and R3 (crisp tinted shadow, first with corner colours):** the core of the look.
3. **R4 (lines):** delete first, then add the five bold lines back.
4. **R6 (hair), then R7 (proportions).**
5. **R8** only if it is still needed.

Gates for each step:

- in-game face, face-side, garage and chase shots in three lighting setups;
- the driver GLB face count (currently 48k, D-076);
- S01 frame time (6.06 ms baseline);
- ctest and core tests;
- flow soak leak gate.

## Sources

- Motomura, J. C. *GuiltyGearXrd's Art Style: The X Factor Between 2D and 3D*, GDC 2015.
  - Speaker notes: https://www.ggxrd.com/Motomura_Junya_GuiltyGearXrd.pdf
  - Vault: https://www.gdcvault.com/play/1022031/GuiltyGearXrd-s-Art-Style-The
  - ASW post: https://www.arcsystemworks.com/guilty-gear-xrds-art-style-the-x-factor-between-2d-and-3d-talk-from-gdc-2015-is-now-available-online/
- Mitchell, Francke, Eng (Valve). *Illustrative Rendering in Team Fortress 2*, NPAR 2007:
  https://cdn.cloudflare.steamstatic.com/apps/valve/2007/NPAR07_IllustrativeRenderingInTeamFortress2.pdf
- Telltale statements:
  - Kotaku 2011: https://kotaku.com/see-the-walking-dead-video-games-superb-comic-book-look-5824043
  - Game Informer 2011: https://gameinformer.com/games/the_walking_dead_episode_one_a_new_day/b/pc/archive/2011/07/22/telltale-s-walking-dead-art-tease.aspx
  - PlayStation Blog 2026: https://blog.playstation.com/2026/10/01/return-to-fabletown-with-the-wolf-among-us-remastered-out-october-29/
  - Nintendo Everything: https://nintendoeverything.com/the-wolf-among-us-remastered-interview-telltale-on-why-the-game-is-returning-improvements-detailed-more/
  - GDC Europe 2014 summary: https://www.gamedeveloper.com/design/video-how-telltale-adapted-fables-into-i-the-wolf-among-us-i-
- Tanaka, Komada (Tango Gameworks). *3D Toon Rendering in Hi-Fi RUSH*, GDC 2024: https://gdcvault.com/play/1034251/3D-Toon-Rendering-in-Hi
- Unity Toon Shader manual, Shadow Control Maps: https://docs.unity3d.com/Packages/com.unity.toonshader@0.8/manual/Basic.html
- Blender 4.5 documentation:
  - Mesh API (`normals_split_custom_set*`): https://docs.blender.org/api/4.5/bpy.types.Mesh.html
  - Data Transfer modifier: https://docs.blender.org/manual/en/4.5/modeling/modifiers/modify/data_transfer.html
  - Editing Normals: https://docs.blender.org/manual/en/4.5/modeling/meshes/editing/mesh/normals.html
  - Solidify modifier: https://docs.blender.org/manual/en/4.5/modeling/modifiers/generate/solidify.html
  - glTF exporter (Normals option, vertex attributes): https://docs.blender.org/manual/en/4.5/addons/import_export/scene_gltf2.html
- Gordon, C. C. et al. *2012 Anthropometric Survey of U.S. Army Personnel: Methods and Summary Statistics*, 2014:
  https://apps.dtic.mil/sti/pdfs/ADA611869.pdf (OCR copy: https://archive.org/details/DTIC_ADA611869)
- Repo code cited:
  - `tools/car_cabin.py` (`driver()`, `build_head()`)
  - `tools/generate_driver.py` (`review()`, glTF export)
  - `engine/WickedEngine/WickedEngine/shaders/brdf.hlsli`, `surfaceHF.hlsli`, `outlinePS.hlsl`
  - `engine/patches/0003-toon-crease-ink.patch`
  - `engine/WickedEngine/Editor/ModelImporter_GLTF.cpp`
  - `game/sandbox/CarVisual.cpp`, `game/sandbox/MenuPath.cpp`
