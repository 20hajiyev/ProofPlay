#include "RacePath.h"
#include "CarVisual.h"
#include "KeyNames.h"
#include "racer/Performance.h"
#include "racer/runtime/HandlingTrack.h"
#include "racer/runtime/PadRace.h"
#include "racer/runtime/TrackBuilder.h"
#include "racer/Track.h"
#include "racer/CombatTuning.h"

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#include <nlohmann/json.hpp>

using namespace racer;
using namespace racer::runtime;
using namespace wi::scene;
using wi::ecs::Entity;
using wi::ecs::INVALID_ENTITY;

namespace
{
    uint64_t PrivateBytes()
    {
        PROCESS_MEMORY_COUNTERS_EX c{};
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&c), sizeof(c));
        return c.PrivateUsage;
    }

    float Percentile(std::vector<float> v, float p)
    {
        if (v.empty())
            return 0;
        std::sort(v.begin(), v.end());
        return v[std::min(v.size() - 1, size_t(p * v.size()))];
    }

    float ExpDecay(float dt, float rate) { return 1.0f - std::exp(-rate * dt); }

    // Diagnostics only: RACER_DEBUG_SKIP=tc skips track (t) / car (c) visuals, to bisect resource use.
    bool DebugSkip(char part)
    {
        const char* v = std::getenv("RACER_DEBUG_SKIP");
        return v && std::strchr(v, part) != nullptr;
    }

    struct AbilityLook
    {
        XMFLOAT4 color;
        XMFLOAT3 half; // shape differs per ability so it reads without colour (plan 2.8/2.14)
    };
    AbilityLook Look(Ability a)
    {
        switch (a)
        {
        case Ability::Surge: return { { 0.1f, 0.8f, 1.0f, 1 }, { 0.30f, 0.90f, 0.30f } };
        case Ability::Lance: return { { 1.0f, 0.15f, 0.1f, 1 }, { 0.25f, 0.25f, 1.00f } };
        case Ability::Pulse: return { { 0.9f, 0.2f, 0.9f, 1 }, { 0.90f, 0.30f, 0.90f } };
        case Ability::Trap: return { { 0.5f, 0.2f, 1.0f, 1 }, { 0.80f, 0.12f, 0.80f } };
        case Ability::Needle: return { { 1.0f, 0.85f, 0.1f, 1 }, { 0.12f, 1.00f, 0.12f } };
        case Ability::Ward: return { { 0.2f, 0.4f, 1.0f, 1 }, { 0.60f, 0.60f, 0.60f } };
        case Ability::Mend: return { { 0.2f, 1.0f, 0.3f, 1 }, { 0.90f, 0.25f, 0.25f } };
        case Ability::Storm: return { { 0.6f, 1.0f, 0.9f, 1 }, { 0.45f, 0.45f, 0.45f } };
        default: return { { 1, 1, 1, 1 }, { 0.5f, 0.5f, 0.5f } };
        }
    }

    std::string SlotText(const Slot& s, bool selected)
    {
        std::string name = s.ability == Ability::None ? "--" : ToString(s.ability);
        if (s.ability == Ability::Needle)
            name += " x" + std::to_string(s.charges);
        return selected ? "> " + name + " <" : "  " + name + "  ";
    }
}

// Pooled effect entities: created once, shown/hidden, never destroyed during a race.
struct RacePath::CombatVisuals
{
    Scene& scene;
    std::vector<Entity> pads, lances, needles, traps, storms;

    explicit CombatVisuals(Scene& s) : scene(s) {}

    Entity Make(const char* name, XMFLOAT4 color, float emissive)
    {
        Entity e = scene.Entity_CreateCube(name);
        if (MaterialComponent* m = scene.materials.GetComponent(e))
        {
            m->SetBaseColor(color);
            m->SetEmissiveColor(XMFLOAT4(color.x, color.y, color.z, emissive));
        }
        if (ObjectComponent* o = scene.objects.GetComponent(e))
        {
            o->SetCastShadow(false);
            o->SetRenderable(false);
        }
        return e;
    }
    void Pool(std::vector<Entity>& pool, size_t n, const char* name, XMFLOAT4 color, float emissive)
    {
        for (size_t i = 0; i < n; ++i)
            pool.push_back(Make(name, color, emissive));
    }
    void Place(Entity e, XMFLOAT3 pos, XMFLOAT3 half, float yaw, float pitch = 0)
    {
        TransformComponent& t = *scene.transforms.GetComponent(e);
        t.ClearTransform();
        t.Scale(half);
        t.RotateRollPitchYaw(XMFLOAT3(pitch, yaw, 0));
        t.Translate(pos);
        t.SetDirty();
        scene.objects.GetComponent(e)->SetRenderable(true);
    }
    void Hide(Entity e) { scene.objects.GetComponent(e)->SetRenderable(false); }
    void SetEmissive(Entity e, XMFLOAT4 color, float strength)
    {
        if (MaterialComponent* m = scene.materials.GetComponent(e))
        {
            m->SetBaseColor(color);
            m->SetEmissiveColor(XMFLOAT4(color.x, color.y, color.z, strength));
        }
    }
};

RacePath::RacePath(Options options) : options_(std::move(options))
{
    scene = &own_scene_;
}

RacePath::~RacePath()
{
    // The session's bodies live in own_scene_; tear the session down first.
    session_.reset();
    fx_.reset();
    if (options_.audio)
    {
        options_.audio->RemoveEngines();
        options_.audio->StopMusic();
        options_.audio->Update(0.0f);
    }
}

RaceSummary RacePath::PlayerSummary() const
{
    return session_ ? session_->Summary(size_t(player_)) : RaceSummary{};
}

Entity RacePath::LoadCarVisual(size_t car, const XMFLOAT4& paint)
{
    Scene& s = *scene;
    VehicleRuntime& rt = session_->Car(car);
    const std::string model = options_.cooked_dir + "/vehicles/" + rt.Definition().id + ".wiscene";
    Scene staging;
    const Entity root = LoadModel(staging, model, XMMatrixIdentity(), true);
    if (root == INVALID_ENTITY || !staging.transforms.Contains(root))
        return INVALID_ENTITY;
    const bool own_car = !rt.Definition().id.empty() && rt.Definition().id[0] == 'D'; // local cars keep their livery
    ToonifyMaterials(staging); // D-050: one look for every car
    // Customisation (D-053): the model carries every part variant (PART_<slot>_<variant>, rims
    // RIM_<variant>_<wheel>); keep the chosen one per slot - the player's choice, else stock.
    KeepCarParts(staging, ResolveCarParts(options_.content_dir + "/customization/" + rt.Definition().id + ".json",
                                          int(car) == player_ ? options_.player_parts : std::map<std::string, std::string>{}));
    if (own_car)
        PaintCar(staging, paint);
    std::vector<std::pair<Entity, std::string>> mats; // lamps and indicators the game switches (D-060)
    for (size_t i = 0; i < staging.materials.GetCount(); ++i)
        if (const NameComponent* n = staging.names.GetComponent(staging.materials.GetEntity(i)))
            mats.push_back({ staging.materials.GetEntity(i), n->name });
    s.Merge(staging);
    TransformComponent& t = *s.transforms.GetComponent(root);
    t.Translate(XMFLOAT3(0, -rt.BodyOriginHeight(), 0));
    t.UpdateTransform();
    s.Component_Attach(root, rt.Body(), true);
    // the driver character in the seat (D-076): the player's look, a varied one for the AI
    racer::DriverLook look = int(car) == player_ ? options_.driver_look : racer::AiDriverLook(int(car));
    if (const char* f = std::getenv("RACER_DRIVER"); f && int(car) == player_) // diagnostics: the player's face
        look.head = std::clamp(std::atoi(f), 0, racer::kDriverHeads - 1);
    if (!AttachDriver(s, root, options_.cooked_dir + "/characters/driver.wiscene", look, &mats))
        wi::backlog::post("[racer] driver not attached to car " + std::to_string(car), wi::backlog::LogLevel::Warning);
    auto wheel = [&](const char* n) { return s.Entity_FindByName(n, root); };
    rt.SetWheelEntities(wheel("WHEEL_PIVOT_FL"), wheel("WHEEL_PIVOT_FR"), wheel("WHEEL_PIVOT_RL"), wheel("WHEEL_PIVOT_RR"));
    BuildRig(car, root, mats, rt.BodyOriginHeight());
    PrepareDamage(car, root);
    return root;
}

void RacePath::BuildTrackDressing()
{
    // Edge posts every 12 m and start/checkpoint lines, as instances of one shared cube mesh.
    Scene& s = *scene;
    const TrackRoute& route = session_->Route();
    const float hw = route.HalfWidth();
    Entity post_mesh = s.Entity_CreateCube("edge_post");
    if (MaterialComponent* m = s.materials.GetComponent(post_mesh))
        m->SetBaseColor(XMFLOAT4(0.95f, 0.95f, 0.9f, 1));
    s.objects.GetComponent(post_mesh)->SetRenderable(false); // the template itself stays hidden
    auto instance = [&](XMFLOAT3 pos, XMFLOAT3 half, float yaw) {
        Entity e = s.Entity_CreateObject("edge_post");
        s.objects.GetComponent(e)->meshID = post_mesh;
        TransformComponent& t = *s.transforms.GetComponent(e);
        t.Scale(half);
        t.RotateRollPitchYaw(XMFLOAT3(0, yaw, 0));
        t.Translate(pos);
        t.UpdateTransform();
    };
    for (float along = 0; along < route.Length(); along += 12.0f)
    {
        const Vec2 p = route.PointAt(along), d = route.DirectionAt(along);
        for (float side : { -1.0f, 1.0f })
            instance(XMFLOAT3(p.x + d.z * side * (hw + 0.6f), 0.45f, p.z - d.x * side * (hw + 0.6f)), XMFLOAT3(0.12f, 0.45f, 0.12f), 0);
    }
    const Vec2 line = route.PointAt(0.0f), dir = route.DirectionAt(0.0f); // start/finish line
    instance(XMFLOAT3(line.x, 0.02f, line.z), XMFLOAT3(hw, 0.02f, 0.5f), std::atan2(dir.x, dir.z));
}

// Balanced render profile (plan 3.x "Balanced render"): raster lighting, one shadowed sun with
// three cascades, screen-space AO, measured bloom, ACES tonemapping, no RT/dynamic GI, no SSR
// (the only glossy surface is the water, far from the camera). Harbor look: late-afternoon sun,
// physically based sky.
void RacePath::ApplyHarborLook(Scene& s)
{
    // A WeatherComponent entity, not just Scene::weather: the engine copies weathers[0] into the
    // scene every frame and resets its per-frame state (sun selection) only when one exists.
    WeatherComponent& w = s.weathers.GetCount() > 0 ? s.weathers[0] : s.weathers.Create(wi::ecs::CreateEntity());
    // Gradient sky, not SetRealisticSky: in v0.72.106 the realistic sky renders black at random
    // moments in this scene (6 s fine, 16-20 s black; with and without aerial perspective; not
    // the weather/light job order - measured 2026-09-28, docs/ENGINE_BASELINE.md). Stability
    // first; distance fog supplies the harbour haze the realistic sky gave.
    // Time of day (D-052, PS2-era cel look: Auto Modellista dusk, Highway Warriors night). Shadow
    // sides take the ambient colour, so a cool ambient under a warm key gives the anime look of
    // coloured, never black, shadows.
    struct TimeOfDay
    {
        const char* name;
        XMFLOAT3 horizon, zenith, ambient, sun;
        float sun_intensity, sun_elevation_deg, fog_start, fog_density, exposure, bloom_threshold;
        bool lamps;
    };
    static const TimeOfDay kTimes[] = {
        { "day", { 0.80f, 0.90f, 0.98f }, { 0.18f, 0.45f, 0.92f }, { 0.30f, 0.33f, 0.38f }, { 1.0f, 0.88f, 0.72f }, 9.0f, 35.0f, 120.0f, 0.0009f, 0.85f, 1.4f, false },
        { "sunset", { 1.0f, 0.5f, 0.22f }, { 0.1f, 0.12f, 0.32f }, { 0.30f, 0.24f, 0.42f }, { 1.0f, 0.6f, 0.34f }, 8.0f, 11.0f, 160.0f, 0.0006f, 0.9f, 1.2f, true },
        { "night", { 0.05f, 0.1f, 0.11f }, { 0.01f, 0.018f, 0.04f }, { 0.07f, 0.08f, 0.13f }, { 0.55f, 0.65f, 1.0f }, 0.9f, 40.0f, 90.0f, 0.0011f, 1.0f, 1.0f, true },
    };
    // Each track has its own hour; --time= overrides it for testing.
    std::string want = options_.time_of_day;
    if (want.empty())
        want = options_.track == "L01" ? "night" : options_.track == "S01" ? "sunset" : "day";
    const TimeOfDay* tod = &kTimes[0];
    for (const TimeOfDay& t : kTimes)
        if (want == t.name)
            tod = &t;
    lamps_on_ = tod->lamps;
    night_glow_ = tod->lamps;
    lights_on_ = lamps_on_; // the player's headlight switch starts with the hour
    w.horizon = tod->horizon;
    w.zenith = tod->zenith;
    // sky light: no realistic-sky irradiance any more; x1.4 fill for the brighter grade (D-069)
    w.ambient = XMFLOAT3(tod->ambient.x * 1.4f, tod->ambient.y * 1.4f, tod->ambient.z * 1.4f);
    w.fogStart = tod->fog_start;
    w.fogDensity = tod->fog_density;
    w.skyExposure = 1.0f;
    // Weather (D-083): the sky greys and the sun softens with the intensity, fog closes in to the
    // visibility, Wicked's rain particles fall, the road turns dark and glossy (below); the storm
    // adds lightning (UpdateWeather). Physics (grip) is the RaceSession's, the same for every car.
    const WeatherEffects wfx = EffectsOf(options_.weather);
    {
        const float k = std::clamp(options_.weather.intensity, 0.0f, 1.0f);
        const WeatherKind kind = options_.weather.kind;
        weather_grey_ = k * (kind == WeatherKind::Storm ? 1.0f : kind == WeatherKind::Rain ? 0.85f : kind == WeatherKind::Overcast ? 0.7f : kind == WeatherKind::Fog ? 0.6f : 0.0f);
        auto grey_of = [&](const XMFLOAT3& c, float lift) {
            const float l = (0.3f * c.x + 0.59f * c.y + 0.11f * c.z) * lift;
            const XMFLOAT3 g(l * 0.92f, l * 0.97f, l * 1.05f);
            return XMFLOAT3(c.x + (g.x - c.x) * weather_grey_, c.y + (g.y - c.y) * weather_grey_, c.z + (g.z - c.z) * weather_grey_);
        };
        w.horizon = grey_of(tod->horizon, kind == WeatherKind::Fog ? 0.95f : 0.72f); // rain: a heavy, darker sky
        w.zenith = grey_of(tod->zenith, kind == WeatherKind::Fog ? 1.1f : 0.85f);
        const float fill = 1.0f + 0.25f * weather_grey_; // flat, shadowless overcast light
        w.ambient = XMFLOAT3(w.ambient.x * fill, w.ambient.y * fill, w.ambient.z * fill);
        if (kind != WeatherKind::Clear)
        {
            // 2.5/visibility washed the whole frame white by day (measured): a softer veil
            w.fogStart = std::min(tod->fog_start, wfx.visibility_m * 0.2f);
            w.fogDensity = std::max(tod->fog_density, 1.1f / wfx.visibility_m);
        }
        // Not the engine's rain (D-083, measured): it collides with a top-down depth map, so it
        // splashed on the clouds and the gantry as speckle, and the ink pass drew every drop black.
        // BuildRain makes our own streaks instead.
        // The engine draws its rain blocker map only while rain_amount > 0; a millionth keeps the map
        // (our drops stop and splash on it) and leaves the engine's own emitter at ~1 drop a second.
        w.rain_amount = wfx.wetness > 0.0f ? 1e-6f : 0.0f;
        w.rain_splash_scale = 0.03f; // 7 cm splashes read as white squares on the road (measured)
    }
    const XMFLOAT3 sun_color = tod->sun;
    const float sun_intensity = tod->sun_intensity * (1.0f - 0.75f * weather_grey_);
    if (weather_grey_ >= 0.6f) // a dark wet day: headlights and the street lamps are on, as real drivers do
        lamps_on_ = lights_on_ = true;
    Entity sun = s.Entity_CreateLight("sun", XMFLOAT3(0, 50, 0), sun_color, sun_intensity, 1000.0f, LightComponent::DIRECTIONAL);
    if (TransformComponent* t = s.transforms.GetComponent(sun))
    {
        // Pitch 90 - elevation; a low sun at dusk throws long shadows across the road.
        const float elev = std::getenv("RACER_SUN_ELEV") ? float(std::atof(std::getenv("RACER_SUN_ELEV"))) : tod->sun_elevation_deg; // diagnostics
        t->RotateRollPitchYaw(XMFLOAT3(XMConvertToRadians(90.0f - elev), XMConvertToRadians(35.0f), 0));
        t->UpdateTransform();
        // The sky's sun lives on the WeatherComponent too. The engine copies weathers[0] over
        // Scene::weather every frame in a job that can run after the light job that fills in the
        // sun, leaving sunColor = 0 and a black sky at random moments (measured 2026-09-28).
        XMStoreFloat3(&w.sunDirection, XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(0, 1, 0, 0), XMLoadFloat4x4(&t->world))));
        w.sunColor = XMFLOAT3(sun_color.x * sun_intensity, sun_color.y * sun_intensity, sun_color.z * sun_intensity);
    }
    if (LightComponent* l = s.lights.GetComponent(sun))
    {
        l->SetCastShadow(true);
        l->cascade_distances = { 12.0f, 60.0f, 300.0f }; // chase camera: detail near the car
        if (const char* c = std::getenv("RACER_CASCADES")) // diagnostics: "12,60,300"
        {
            float v[3] = { 12, 60, 300 };
            std::sscanf(c, "%f,%f,%f", &v[0], &v[1], &v[2]);
            l->cascade_distances = { v[0], v[1], v[2] };
        }
    }
    wi::renderer::SetShadowProps2D(2048);

    // No occlusion culling (D-090, owner: "pieces beside the road disappear like chunks while I drive"):
    // its GPU queries answer a frame late, so what the barriers or a car hid pops back late as the
    // camera moves. The open port has few real occluders: off costs nothing measurable (S01 6.08 ->
    // 6.09 ms, L01 6.06 -> 6.07 ms, 12 cars, 20 s).
    wi::renderer::SetOcclusionCullingEnabled(std::getenv("RACER_OCCLUSION") != nullptr);
    setAO(AO_MSAO);
    setAOPower(1.2f);
    setAORange(2.0f);
    setBloomEnabled(true);
    setBloomThreshold(tod->bloom_threshold); // lamps, pickup rings and brake lights bloom; sunlit concrete does not
    setTonemap(wi::renderer::Tonemap::ACES);
    // Auto Modellista grade (D-069): a brighter, punchier palette with more fill light in the shade.
    setExposure(tod->exposure * 1.1f);
    setContrast(1.08f);
    setSaturation(1.25f);
    setBrightness(0.02f);
    setFXAAEnabled(true);
    setSharpenFilterEnabled(true);
    setSharpenFilterAmount(0.25f);
    setSSREnabled(false);
    setEyeAdaptionEnabled(false); // stable brightness: readable HUD and combat colours
    setMotionBlurEnabled(options_.motion_blur); // accessibility: some players get motion sick
    setMotionBlurStrength(0.6f);
    // Outlines are drawn only for materials that ask for them (toon cars).
    setOutlineEnabled(true);
    setOutlineThickness(2.2f); // heavier ink (D-069)
    setOutlineThreshold(0.1f);
    // PS2-era cel look (D-052): ink also where an outlined surface folds by more than ~37 degrees
    // (engine patch P-003), so panel edges and creases are drawn, not only silhouettes.
    // Keep at 0.8: 1.0 inks every facet of the round meshes (clouds, cars) solid black (D-066/D-069).
    setOutlineCrease(0.8f);
    setOutlineColor(XMFLOAT4(0.05f, 0.05f, 0.07f, 1.0f));
    // Diagnostics (D-069): RACER_GRADE=thickness,crease,contrast,saturation,brightness,exposure_mul,ambient_mul
    // to tune the grade one knob at a time without rebuilding.
    if (const char* g = std::getenv("RACER_GRADE"))
    {
        float v[7] = { 2.2f, 0.8f, 1.08f, 1.25f, 0.02f, 1.0f, 1.0f }; // exposure/ambient: multipliers on top
        std::sscanf(g, "%f,%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6]);
        setOutlineThickness(v[0]);
        setOutlineCrease(v[1]);
        setContrast(v[2]);
        setSaturation(v[3]);
        setBrightness(v[4]);
        setExposure(tod->exposure * v[5]);
        w.ambient = XMFLOAT3(w.ambient.x * v[6], w.ambient.y * v[6], w.ambient.z * v[6]);
    }
    base_exposure_ = tod->exposure * 1.1f;
    base_ambient_ = w.ambient;
    // Wet road (D-083): the ground materials darken and gloss with the wetness - asphalt reads wet
    // by its darker tone and the sharp highlights of the lights on it.
    if (wfx.wetness > 0.0f)
        for (size_t i = 0; i < s.materials.GetCount(); ++i)
        {
            const NameComponent* n = s.names.GetComponent(s.materials.GetEntity(i));
            if (!n || (n->name.find("Road") == std::string::npos && n->name.find("Quay") == std::string::npos))
                continue;
            MaterialComponent& m = s.materials[i];
            const XMFLOAT4 c = m.baseColor;
            const float dark = 1.0f - 0.38f * wfx.wetness;
            m.SetBaseColor(XMFLOAT4(c.x * dark, c.y * dark, c.z * dark, c.w));
            m.SetRoughness(std::min(m.roughness, 1.0f - 0.35f * wfx.wetness)); // glossier blew the headlights up into white blobs
            m.SetReflectance(0.04f + 0.06f * wfx.wetness);
        }
    wetness_ = wfx.wetness;
    // SSR on the wet road was tried (D-083): +0.3 ms in a storm and the reflections barely read on
    // the toon asphalt - left off.
    if (wfx.wetness > 0.0f)
        BuildRain(s, wfx.wetness * (options_.weather.kind == WeatherKind::Storm ? 1.0f : 0.65f), wfx.wind_ms, std::string(tod->name) != "day");
    BuildSky(s, tod->name, w.sunDirection);
}

// Rain (D-083, rebuilt after the owner's review): long thin streaks falling at 13 m/s, stretched by
// their speed, slanted by the wind. They spawn anywhere in a 36 x 14 x 36 m box that turns with the
// camera and starts 2 m ahead of it (no drop sits by the lens), and the box is filled at load, so it
// is already raining when the race opens. The engine's rain blocker (a top-down depth map around the
// camera) stops every drop where it meets the road, a roof or a car and turns it into a short splash -
// before this they fell through the ground - and no drop spawns under a roof (none in the cabin).
// Unlit: lit drops caught every light and thousands of them overlapped into white blobs (measured).
namespace
{
    const XMFLOAT3 kRainBox(18.0f, 7.0f, 18.0f); // half extents of the rain volume (m)
}

void RacePath::BuildRain(Scene& s, float amount, float wind_ms, bool dark)
{
    rain_emitter_ = s.Entity_CreateEmitter("rain", XMFLOAT3(0, 14, 0));
    wi::EmittedParticleSystem& e = *s.emitters.GetComponent(rain_emitter_);
    e.SetVolumeEnabled(true); // no mesh: random points in the unit cube scaled by the transform
    e._flags |= wi::EmittedParticleSystem::FLAG_USE_RAIN_BLOCKER;
    e.SetMaxParticleCount(32000);
    e.count = 18000.0f * amount; // per second: a downpour, not a drizzle (7500 read as drizzle - measured)
    e.life = 1.2f;
    e.random_life = 0.4f;
    e.size = 0.015f;
    e.random_factor = 0.04f;
    e.normal_factor = 0.0f;
    // The engine transforms the velocity by the whole world matrix, box scale included (18 x 7 x 18 -
    // unscaled, the drops flew at 230 m/s as screen-long lines: measured), so it is given pre-divided.
    e.velocity = XMFLOAT3((wind_ms * 0.6f + 1.2f) / kRainBox.x, -13.0f / kRainBox.y, 0.4f / kRainBox.z);
    e.gravity = XMFLOAT3(0, 0, 0);
    e.drag = 1.0f;
    e.motionBlurAmount = 0.05f; // streak length = speed x amount: ~0.65 m
    e.shaderType = wi::EmittedParticleSystem::SOFT; // textured and unlit (SIMPLE is a debug shader: opaque flat grey, no texture - the hard white lines and squares)
    e.SetCollidersDisabled(true);
    e.SetDepthCollisionEnabled(false);
    e.SetOpacityCurveControl(0.05f, 0.92f);
    TextureParticles(rain_emitter_, "rain_dot.png"); // a soft dot, stretched by the speed into a soft streak
    if (MaterialComponent* m = s.materials.GetComponent(rain_emitter_))
    {
        m->SetBaseColor(dark ? XMFLOAT4(0.45f, 0.5f, 0.56f, 0.35f) : XMFLOAT4(0.88f, 0.9f, 0.95f, 0.5f));
        m->userBlendMode = wi::enums::BLENDMODE_ALPHA;
    }
}

// Puddles (D-084): the session's puddles (the same ones the tyres feel), each a flattened cube on
// the road with a painted water texture (dark pool, reflection band, ripple rings, damp rim)
// cut out by its alpha, turned at random. Measured dead ends: a code-built quad's material never
// took, decals do not draw on the toon road, and instances sharing a hidden template cube drew
// nothing; one cube per puddle (as the combat effects do) works. 1.5 cm above the route height sat
// under the visual road: 6 cm.
void RacePath::BuildPuddles(Scene& s)
{
    const auto& ps = session_->Puddles();
    const TrackRoute& route = session_->Route();
    const std::string tex = options_.content_dir + "/fx/puddle.png";
    const wi::Resource res = wi::resourcemanager::Load(tex);
    std::mt19937 rng(551);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    for (const Puddle& p : ps)
    {
        const float y = route.ElevationAt(route.Project(p.centre).s) + 0.06f; // 1.5 cm sat under the visual road (measured)
        const float r = p.radius * 1.25f; // the texture's water fills ~72% of the square
        const Entity e = s.Entity_CreateCube("Road Puddle");
        if (MaterialComponent* m = s.materials.GetComponent(e))
        {
            m->shaderType = MaterialComponent::SHADERTYPE_CARTOON;
            m->SetOutlineEnabled(false);
            m->SetAlphaRef(0.3f);
            m->SetRoughness(0.3f);
            m->textures[MaterialComponent::BASECOLORMAP].name = tex;
            m->textures[MaterialComponent::BASECOLORMAP].resource = res;
        }
        s.objects.GetComponent(e)->SetCastShadow(false);
        TransformComponent& t = *s.transforms.GetComponent(e);
        t.Scale(XMFLOAT3(r, 0.004f, r)); // the cube is 2 m across: scale = half size
        t.RotateRollPitchYaw(XMFLOAT3(0, u(rng) * XM_2PI, 0));
        t.Translate(XMFLOAT3(p.centre.x, y, p.centre.z));
        t.UpdateTransform();
    }
}

// Visible damage (D-085). The body and its fitted parts keep their undamaged vertex positions; when a
// zone's dent grows (RaceSession tracks it from the hits), every vertex is moved by racer::DeformPoint
// and the mesh re-uploaded - a handful of times a race per car, never per frame.
void RacePath::PrepareDamage(size_t car, Entity root)
{
    Scene& s = *scene;
    damage_.resize(std::max(damage_.size(), car + 1));
    CarDamage& d = damage_[car];
    d.bounds = { 1e9f, -1e9f, 1e9f, -1e9f };
    for (size_t i = 0; i < s.objects.GetCount(); ++i)
    {
        const Entity e = s.objects.GetEntity(i);
        const NameComponent* n = s.names.GetComponent(e);
        if (!n || (n->name != "BODY" && n->name.rfind("PART_", 0) != 0))
            continue;
        const HierarchyComponent* h = s.hierarchy.GetComponent(e);
        bool ours = false; // under this car's root (directly or via BODY); the mesh's frame in the car's
        XMMATRIX to_car = s.transforms.GetComponent(e)->GetLocalMatrix();
        for (Entity p = h ? h->parentID : INVALID_ENTITY; p != INVALID_ENTITY && !ours;)
        {
            ours = p == root;
            if (!ours)
                if (const TransformComponent* pt = s.transforms.GetComponent(p))
                    to_car = to_car * pt->GetLocalMatrix();
            const HierarchyComponent* ph = s.hierarchy.GetComponent(p);
            p = ph ? ph->parentID : INVALID_ENTITY;
        }
        const Entity mesh_e = s.objects[i].meshID;
        const MeshComponent* m = s.meshes.GetComponent(mesh_e);
        if (!ours || !m || m->vertex_positions.empty())
            continue;
        CarDamage::Piece piece{ mesh_e, m->vertex_positions };
        XMStoreFloat4x4(&piece.to_car, to_car);
        XMStoreFloat4x4(&piece.from_car, XMMatrixInverse(nullptr, to_car));
        d.meshes.push_back(piece);
        if (n->name == "BODY")
            for (const XMFLOAT3& v : m->vertex_positions)
            {
                XMFLOAT3 c;
                XMStoreFloat3(&c, XMVector3Transform(XMLoadFloat3(&v), to_car));
                d.bounds = { std::min(d.bounds.min_x, c.x), std::max(d.bounds.max_x, c.x), std::min(d.bounds.min_z, c.z), std::max(d.bounds.max_z, c.z) };
            }
    }
    // Engine smoke from under the bonnet: light grey puffs, only when the health is low (racer::SmokeAmount).
    d.smoke = s.Entity_CreateEmitter("engine_smoke", XMFLOAT3(0, 0.95f, d.bounds.max_z - 0.7f));
    s.Component_Attach(d.smoke, root, true);
    wi::EmittedParticleSystem& em = *s.emitters.GetComponent(d.smoke);
    em.SetMaxParticleCount(160);
    em.count = 0.0f;
    em.life = 1.6f;
    em.random_life = 0.5f;
    em.size = 0.3f;
    em.scaleX = em.scaleY = 3.0f;
    em.random_factor = 0.5f;
    em.normal_factor = 0.0f;
    em.velocity = XMFLOAT3(0, 1.3f, 0);
    em.gravity = XMFLOAT3(0, 0.4f, 0);
    em.drag = 0.97f;
    em.rotation = 0.5f;
    em.shaderType = wi::EmittedParticleSystem::SOFT;
    em.SetCollidersDisabled(true);
    em.SetOpacityCurveControl(0.1f, 0.4f);
    if (MaterialComponent* mat = s.materials.GetComponent(d.smoke))
    {
        mat->SetBaseColor(XMFLOAT4(0.28f, 0.28f, 0.3f, 0.35f));
        mat->userBlendMode = wi::enums::BLENDMODE_ALPHA;
    }
    TextureParticles(d.smoke, "spray_puff.png");
}

void RacePath::UpdateDamage()
{
    if (!session_)
        return;
    Scene& s = *scene;
    for (size_t car = 0; car < damage_.size() && car < session_->CarCount(); ++car)
    {
        CarDamage& d = damage_[car];
        const Combatant& c = session_->Combat().Get(int(car));
        DentState now = session_->Dents(car);
        if (const char* dbg = std::getenv("RACER_DENT"); dbg && int(car) == player_) // diagnostics: "front,right" at full depth
        {
            const std::string z = dbg;
            const char* names[4] = { "front", "rear", "left", "right" };
            for (int k = 0; k < 4; ++k)
                if (z.find(names[k]) != std::string::npos)
                    now.depth[k] = 1.0f;
        }
        float hp = c.max_health > 0 ? c.health / c.max_health : 1.0f;
        if (std::getenv("RACER_LOW_HP") && int(car) == player_) // diagnostics: smoke at 8% health
            hp = 0.08f;
        if (wi::EmittedParticleSystem* em = d.smoke != INVALID_ENTITY ? s.emitters.GetComponent(d.smoke) : nullptr)
            em->count = 14.0f * SmokeAmount(hp); // a wisp, not a fire
        bool grew = false;
        for (int z = 0; z < 4; ++z)
            grew |= now.depth[z] > d.shown.depth[z] + 0.02f;
        if (!grew)
            continue;
        d.shown = now;
        for (const CarDamage::Piece& pc : d.meshes)
            if (MeshComponent* m = s.meshes.GetComponent(pc.mesh))
            {
                const XMMATRIX to = XMLoadFloat4x4(&pc.to_car), from = XMLoadFloat4x4(&pc.from_car);
                for (size_t k = 0; k < pc.original.size() && k < m->vertex_positions.size(); ++k)
                {
                    XMFLOAT3 c;
                    XMStoreFloat3(&c, XMVector3Transform(XMLoadFloat3(&pc.original[k]), to));
                    const Point3 q = DeformPoint({ c.x, c.y, c.z }, d.bounds, d.shown);
                    XMStoreFloat3(&m->vertex_positions[k], XMVector3Transform(XMVectorSet(q.x, q.y, q.z, 1), from));
                }
                m->CreateRenderData();
            }
    }
}

void RacePath::TextureParticles(Entity material, const char* file)
{
    if (MaterialComponent* m = scene->materials.GetComponent(material))
    {
        const std::string path = options_.content_dir + "/fx/" + file;
        m->textures[MaterialComponent::BASECOLORMAP].name = path;
        m->textures[MaterialComponent::BASECOLORMAP].resource = wi::resourcemanager::Load(path);
        m->SetDirty();
    }
}

// Storm lightning (D-083): every 5-12 s a double flash - the whole frame jumps to white-blue for a
// few frames, the comic-book way (plan: "the screen goes to ink silhouette for 1-2 frames").
void RacePath::UpdateWeather(float dt)
{
    if (rain_emitter_ != INVALID_ENTITY && session_)
        if (TransformComponent* t = scene->transforms.GetComponent(rain_emitter_))
        {
            // above and ahead of the camera, turned with it
            const XMFLOAT3 eye = camera->Eye, at = camera->At;
            XMVECTOR fwd = XMVectorSet(at.x, 0, at.z, 0);
            fwd = XMVectorGetX(XMVector3Length(fwd)) > 1e-3f ? XMVector3Normalize(fwd) : XMVectorSet(0, 0, 1, 0);
            XMFLOAT3 p;
            XMStoreFloat3(&p, XMLoadFloat3(&eye) + fwd * 20.0f + XMVectorSet(0, 5.0f, 0, 0));
            t->ClearTransform();
            t->Scale(kRainBox); // the box spans 2..38 m ahead, 2 m below to 12 m above the eye
            t->RotateRollPitchYaw(XMFLOAT3(0, std::atan2(XMVectorGetX(fwd), XMVectorGetZ(fwd)), 0));
            t->Translate(p);
            t->UpdateTransform();
            if (!rain_filled_) // once the box is in place: fill it, so it is raining from the first frame
                if (wi::EmittedParticleSystem* em = scene->emitters.GetComponent(rain_emitter_))
                    em->Burst(int(em->count * em->life)), rain_filled_ = true;
        }
    for (size_t i = 0; i < spray_.size() && session_ && i < session_->CarCount(); ++i)
        if (wi::EmittedParticleSystem* em = scene->emitters.GetComponent(spray_[i]))
        {
            // thrown back and up behind the car, more the faster it goes (none below ~25 km/h)
            const VehicleTelemetry& t = session_->Car(i).Telemetry();
            const float v = std::sqrt(t.velocity[0] * t.velocity[0] + t.velocity[2] * t.velocity[2]);
            em->count = wetness_ * std::clamp((v - 7.0f) / 30.0f, 0.0f, 1.0f) * 220.0f;
            em->velocity = XMFLOAT3(t.velocity[0] * 0.35f, 1.4f, t.velocity[2] * 0.35f);
        }
    if (options_.weather.kind != WeatherKind::Storm || scene->weathers.GetCount() == 0)
        return;
    WeatherComponent& w = scene->weathers[0];
    lightning_next_ -= dt;
    if (lightning_next_ <= 0.0f)
    {
        lightning_left_ = 0.32f;
        lightning_next_ = 5.0f + 7.0f * float(std::rand() % 1000) / 1000.0f; // presentation only: not part of the simulation
    }
    float flash = 0.0f;
    if (lightning_left_ > 0.0f)
    {
        lightning_left_ -= dt;
        const float t = 0.32f - lightning_left_;
        flash = (t < 0.06f || (t > 0.16f && t < 0.24f)) ? 1.0f : 0.25f; // two strokes
    }
    setExposure(base_exposure_ * (1.0f + 1.6f * flash));
    w.ambient = XMFLOAT3(base_ambient_.x + 1.2f * flash, base_ambient_.y + 1.25f * flash, base_ambient_.z + 1.4f * flash);
}

// Night street lights (D-052): one downward sodium spot per lamp post, positions exported by the
// track generator (content/lamps/<id>.json). No shadows: pools of light on the road are the look.
void RacePath::AddStreetLights(Scene& s)
{
    std::ifstream f(options_.content_dir + "/lamps/" + options_.track + ".json", std::ios::binary);
    if (!f)
        return;
    const nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    if (j.is_discarded() || !j.contains("lamps") || !j["lamps"].is_array())
        return;
    for (const auto& l : j["lamps"])
    {
        if (!l.is_array() || l.size() != 5)
            continue;
        const XMFLOAT3 pos(l[0].get<float>(), l[1].get<float>(), l[2].get<float>());
        const float ax = l[3].get<float>(), az = l[4].get<float>(); // horizontal direction to the road
        const Entity e = s.Entity_CreateLight("street_light", pos, XMFLOAT3(1.0f, 0.62f, 0.3f), 900.0f, 32.0f, LightComponent::SPOT, 0.95f, 0.6f);
        if (TransformComponent* t = s.transforms.GetComponent(e))
        {
            // Identity spots shine down (-Y); tilt ~30 degrees so the pool lands on the lanes, not
            // behind the barrier the post stands beside.
            const XMVECTOR down = XMVector3Normalize(XMVectorSet(ax * 0.55f, -1.0f, az * 0.55f, 0));
            const XMVECTOR q = XMQuaternionRotationAxis(XMVector3Normalize(XMVector3Cross(XMVectorSet(0, -1, 0, 0), down)),
                                                        std::acos(std::clamp(-XMVectorGetY(down), -1.0f, 1.0f)));
            t->Rotate(q);
            t->UpdateTransform();
        }
    }
}

// One spot per car, attached to the visual root: a pool of light ahead of every car at night.
Entity RacePath::AddHeadlight(Scene& s, Entity root, float length_m)
{
    const Entity e = s.Entity_CreateLight("headlight", XMFLOAT3(0, 0, 0), XMFLOAT3(1.0f, 0.95f, 0.85f), 45.0f, 38.0f, LightComponent::SPOT, 0.55f, 0.3f);
    if (TransformComponent* t = s.transforms.GetComponent(e))
    {
        // Down (-Y) rotated to forward (+Z) and ~10 degrees towards the road.
        t->RotateRollPitchYaw(XMFLOAT3(XMConvertToRadians(-80.0f), 0, 0));
        t->Translate(XMFLOAT3(0, 0.7f, length_m * 0.5f + 0.1f));
        t->UpdateTransform();
    }
    s.Component_Attach(e, root, true);
    return e;
}

// Toon sky (D-057). Clouds: 16 copies of 6 cumulus shapes on a 450-750 m ring, 110-260 m up, tinted
// for the hour (white by day, pink-orange at dusk, blue-grey at night). The sun (or the moon at
// night) sits along the key light's direction; stars only at night.
void RacePath::BuildSky(Scene& s, const std::string& time_of_day, const XMFLOAT3& sun_dir)
{
    Scene kit;
    const Entity kit_root = LoadModel(kit, options_.cooked_dir + "/sky/sky_kit.wiscene", XMMatrixIdentity(), true);
    if (kit_root == INVALID_ENTITY)
        return; // no sky kit cooked: plain gradient sky
    ToonifyMaterials(kit);
    const bool night = time_of_day == "night", dusk = time_of_day == "sunset";
    XMFLOAT4 cloud = night ? XMFLOAT4(0.2f, 0.24f, 0.36f, 1) : dusk ? XMFLOAT4(1.0f, 0.7f, 0.62f, 1) : XMFLOAT4(0.97f, 0.98f, 1.0f, 1);
    {
        // rain clouds (D-083): heavier and slate grey with the weather
        const float g = weather_grey_, l = night ? 0.16f : 0.5f;
        cloud = XMFLOAT4(cloud.x + (l * 0.95f - cloud.x) * g, cloud.y + (l - cloud.y) * g, cloud.z + (l * 1.08f - cloud.z) * g, 1);
    }
    for (size_t i = 0; i < kit.materials.GetCount(); ++i)
    {
        const NameComponent* n = kit.names.GetComponent(kit.materials.GetEntity(i));
        if (n && n->name.find("Cloud") != std::string::npos)
            kit.materials[i].SetBaseColor(cloud);
        if (n && n->name.find("Sun") != std::string::npos && dusk) // a big low orange sun at dusk
            kit.materials[i].SetBaseColor(XMFLOAT4(1.0f, 0.55f, 0.25f, kit.materials[i].GetOpacity())),
                kit.materials[i].SetEmissiveColor(XMFLOAT4(1.0f, 0.55f, 0.25f, kit.materials[i].GetEmissiveStrength()));
    }
    s.Merge(kit);
    sky_clouds_ = wi::ecs::CreateEntity();
    s.transforms.Create(sky_clouds_);
    sky_fixed_ = wi::ecs::CreateEntity();
    s.transforms.Create(sky_fixed_);
    auto find = [&](const char* name) { return s.Entity_FindByName(name, kit_root); };
    auto place = [&](Entity e, Entity parent, const XMFLOAT3& pos, float yaw, float scale) {
        if (e == INVALID_ENTITY)
            return;
        s.Component_Detach(e);
        if (TransformComponent* t = s.transforms.GetComponent(e))
        {
            t->ClearTransform();
            t->Scale(XMFLOAT3(scale, scale, scale));
            t->RotateRollPitchYaw(XMFLOAT3(0, yaw, 0));
            t->Translate(pos);
            t->UpdateTransform();
        }
        s.Component_Attach(e, parent, true);
    };
    std::mt19937 rng(911);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    for (int i = 0; i < 16; ++i)
    {
        const Entity src = find(("CLOUD_" + std::to_string(i % 6)).c_str());
        if (src == INVALID_ENTITY)
            continue;
        const Entity c = i < 6 ? src : s.Entity_Duplicate(src);
        const float a = XM_2PI * (float(i) + 0.4f * u(rng)) / 16.0f, r = 450.0f + 300.0f * u(rng);
        place(c, sky_clouds_, XMFLOAT3(std::cos(a) * r, 55.0f + 95.0f * u(rng), std::sin(a) * r), u(rng) * XM_2PI, 0.8f + 0.6f * u(rng));
    }
    const XMVECTOR d = XMVector3Normalize(XMLoadFloat3(&sun_dir));
    XMFLOAT3 at;
    XMStoreFloat3(&at, d * (night ? 560.0f : 640.0f));
    at.y = std::max(at.y, dusk ? 45.0f : 80.0f); // keep the disc above the harbour skyline
    place(find("SUN"), sky_fixed_, at, 0, dusk ? 1.6f : 1.0f);
    place(find("SUN_HALO"), sky_fixed_, at, 0, dusk ? 1.8f : 1.0f);
    place(find("MOON"), sky_fixed_, at, 0.6f, 1.0f);
    place(find("STARS"), sky_fixed_, XMFLOAT3(0, 0, 0), 0, 1.0f);
    // Only what this hour shows stays in the scene.
    const char* drop[] = { night ? "SUN" : "MOON", night ? "SUN_HALO" : "STARS" };
    for (const char* name : drop)
        if (const Entity e = s.Entity_FindByName(name); e != INVALID_ENTITY)
            s.Entity_Remove(e, true);
    if (weather_grey_ > 0.5f) // a heavy sky hides the sun, the moon and the stars
        for (const char* name : { "SUN", "SUN_HALO", "STARS", "MOON" })
            if (const Entity e = s.Entity_FindByName(name); e != INVALID_ENTITY)
                s.Entity_Remove(e, true);
    if (!night)
        if (const Entity e = s.Entity_FindByName("MOON"); e != INVALID_ENTITY)
            s.Entity_Remove(e, true);
}

void RacePath::UpdateSky(float dt)
{
    Scene& s = *scene;
    sky_drift_ += dt * 0.004f; // wind: the whole cloud ring turns ~1 degree every 4 s
    const XMFLOAT3 eye = camera->Eye;
    if (TransformComponent* t = s.transforms.GetComponent(sky_clouds_))
    {
        t->ClearTransform();
        t->RotateRollPitchYaw(XMFLOAT3(0, sky_drift_, 0));
        t->Translate(XMFLOAT3(eye.x, 0, eye.z));
        t->UpdateTransform();
    }
    if (TransformComponent* t = s.transforms.GetComponent(sky_fixed_))
    {
        t->ClearTransform();
        t->Translate(XMFLOAT3(eye.x, 0, eye.z));
        t->UpdateTransform();
    }
}

void RacePath::Start()
{
    RenderPath3D::Start();
    if (loaded_)
        return;
    loaded_ = true;
    Scene& s = *scene;

    // The class D roster (plan 2.7); AI cars cycle through it, the player picks one.
    std::vector<VehicleDefinition> roster;
    std::vector<ValidationIssue> issues;
    auto load_def = [&](const std::string& path, const std::string& id) {
        std::ifstream f(path, std::ios::binary);
        std::stringstream text;
        text << f.rdbuf();
        VehicleDefinition d;
        if (!ParseVehicleDefinition(text.str(), d, issues))
        {
            error_ = id + ".json invalid: " + (issues.empty() ? std::string("unreadable") : issues[0].path + " " + issues[0].message);
            return false;
        }
        roster.push_back(d);
        return true;
    };
    for (const char* id : { "D01", "D02", "D03", "D04", "D05" })
        if (!load_def(options_.content_dir + "/vehicles/" + id + ".json", id))
            return;
    const auto player_car = std::find_if(roster.begin(), roster.end(), [&](const VehicleDefinition& d) { return d.id == options_.car; });
    if (player_car == roster.end())
    {
        error_ = "unknown car '" + options_.car + "'";
        return;
    }
    // the player's garage performance upgrades change the physics definition (D-071)
    CarSetup perf_setup;
    perf_setup.parts = options_.player_parts;
    const VehicleDefinition def = ApplyPerformance(*player_car, perf_setup);

    const int total = options_.opponents + 1;
    // Destruction: the hunter starts at the back with every target ahead (plan 2.12 E06).
    player_ = options_.mode == EventMode::Destruction ? total - 1 : total / 2;
    std::vector<VehicleDefinition> cars;
    // Opponents: the class D roster (D-048; the local real-brand cars were removed, D-091).
    std::vector<VehicleDefinition> field;
    for (const VehicleDefinition& d : roster)
        if (!d.id.empty() && d.id[0] == 'D')
            field.push_back(d);
    const size_t offset = options_.field_offset >= 0 ? size_t(options_.field_offset) % field.size()
                                                     : size_t(std::chrono::steady_clock::now().time_since_epoch().count() / 1000) % field.size();
    for (int i = 0, k = 0; i < total; ++i)
        cars.push_back(i == player_ ? def : field[(offset + size_t(k++)) % field.size()]);
    // Rival Duel is a mirror match: the rival drives the player's car, so skill decides it (a D02
    // rival lost 17 s over 2 laps to a D01 purely on the car; D-044).
    if (options_.mode == EventMode::RivalDuel)
        for (auto& c : cars)
            c = def;
    RaceSetup setup;
    const bool pad = options_.track == "pad";
    if (pad)
    {
        BuildHandlingTrack(s, true);
        setup = MakePadRaceSetup(cars, options_.laps);
    }
    else
    {
        // content/tracks/<id>_<stem>.json, e.g. S01_dock_loop.json, L01_container_run.json.
        std::string track_file;
        std::error_code fs_error;
        for (const auto& entry : std::filesystem::directory_iterator(options_.content_dir + "/tracks", fs_error))
            if (entry.path().extension() == ".json" && entry.path().filename().string().rfind(options_.track + "_", 0) == 0)
                track_file = entry.path().string();
        if (track_file.empty())
        {
            error_ = "no track definition for '" + options_.track + "'";
            return;
        }
        std::ifstream tf(track_file, std::ios::binary);
        std::stringstream tt;
        tt << tf.rdbuf();
        TrackDefinition track;
        issues.clear();
        if (!ParseTrackDefinition(tt.str(), track, issues))
        {
            error_ = track_file + " invalid: " + (issues.empty() ? std::string("unreadable") : issues[0].path + " " + issues[0].message);
            return;
        }
        BuildTrackPhysics(s, track);
        // Visuals are presentation only; the drive surface and walls come from the definition.
        if (!DebugSkip('t'))
        {
            Scene staging;
            const Entity visual = LoadModel(staging, options_.cooked_dir + "/tracks/" + track.id + ".wiscene", XMMatrixIdentity(), true);
            if (visual == INVALID_ENTITY)
            {
                error_ = "missing cooked track visuals for " + track.id;
                return;
            }
            ToonifyMaterials(staging); // D-050: the world shares the cars' toon look
            s.Merge(staging);
        }
        setup = MakeTrackRaceSetup(track, cars, options_.laps);
    }
    // Event rules: field difficulty and the abilities this event allows (plan 2.12).
    setup.difficulty = options_.difficulty;
    if (options_.restrict_abilities)
    {
        if (options_.allowed_abilities.empty())
            setup.pickups.clear(); // timed events: no pickups at all
        else
        {
            std::vector<Ability> authored;
            for (auto& p : setup.pickups)
                authored.push_back(p.ability);
            const auto filtered = FilterPickupAbilities(authored, options_.allowed_abilities);
            for (size_t i = 0; i < setup.pickups.size(); ++i)
                setup.pickups[i].ability = filtered[i];
        }
    }
    ApplyHarborLook(s);
    // Car paint and glass read as real only when they reflect their surroundings (sky, road,
    // containers, other cars). One real-time probe rides with the player's car; 10 Hz updates at
    // 128 px, limited to 150 m, keep the cost small. Its box covers the pack around the player.
    reflection_probe_ = s.Entity_CreateEnvironmentProbe("player_reflections", XMFLOAT3(0, 2, 0));
    if (EnvironmentProbeComponent* probe = s.probes.GetComponent(reflection_probe_))
    {
        probe->resolution = 128;      // 256 px / 250 m gave 13 ms p99 spikes with full-poly cars
        probe->view_distance = 150.0f;
        probe->SetRealTime(true);
        probe->SetUpdateInterval(0.1f);
    }
    if (TransformComponent* t = s.transforms.GetComponent(reflection_probe_))
    {
        t->Scale(XMFLOAT3(60, 20, 60));
        t->UpdateTransform();
    }
    s.weather = s.weathers[0];

    setup.player_index = player_;
    setup.mode = options_.mode;
    setup.weather = options_.weather;
    {
        std::ifstream cf(options_.content_dir + "/combat/tuning.json", std::ios::binary);
        std::stringstream ct;
        ct << cf.rdbuf();
        issues.clear();
        if (!ParseCombatTuning(ct.str(), setup.tuning, issues))
        {
            error_ = "combat/tuning.json invalid: " + (issues.empty() ? std::string("unreadable") : issues[0].path + " " + issues[0].message);
            return;
        }
    }
    setup.time_limit_s = options_.time_limit_s;
    session_ = std::make_unique<RaceSession>(s, setup);
    if (!session_->Ready())
    {
        error_ = "race session not ready (tuning or race config)";
        return;
    }

    const XMFLOAT4 palette[] = { { 0.8f, 0.1f, 0.1f, 1 }, { 0.1f, 0.3f, 0.8f, 1 }, { 0.9f, 0.9f, 0.9f, 1 }, { 0.1f, 0.1f, 0.12f, 1 },
                                 { 0.1f, 0.6f, 0.3f, 1 }, { 0.6f, 0.5f, 0.3f, 1 }, { 0.4f, 0.1f, 0.5f, 1 }, { 0.2f, 0.6f, 0.7f, 1 } };
    for (size_t i = 0; i < session_->CarCount(); ++i)
    {
        const XMFLOAT4 paint = int(i) == player_ ? options_.player_paint : palette[i % 8];
        if (DebugSkip('c'))
            continue;
        const Entity root = LoadCarVisual(i, paint);
        if (root == INVALID_ENTITY)
        {
            error_ = "missing cooked vehicle model";
            return;
        }
        visual_roots_.push_back(root);
        if (int(i) == player_)
            if (const auto w = options_.player_parts.find("weapon"); w != options_.player_parts.end())
                session_->SetMountedWeapon(i, racer::MountedWeaponFromString(w->second)); // D-064

        // Every car has headlights now; the switch (player) or the hour (AI) turns them on (D-060).
        rigs_[i].headlight = AddHeadlight(s, root, session_->Car(i).Definition().length_m);
        if (wetness_ > 0.0f)
        {
            // Tyre spray (D-083): a mist thrown up behind every car on a wet road, more with speed.
            const Entity e = s.Entity_CreateEmitter("spray", XMFLOAT3(0, 0.25f, -session_->Car(i).Definition().length_m * 0.5f));
            s.Component_Attach(e, root, true);
            wi::EmittedParticleSystem& em = *s.emitters.GetComponent(e);
            em.SetMaxParticleCount(400);
            em.count = 0.0f;
            em.life = 0.7f;
            em.random_life = 0.4f;
            em.size = 0.45f;
            em.scaleX = em.scaleY = 2.2f; // puffs grow as they spread
            em.random_factor = 1.6f;
            em.normal_factor = 0.0f;
            em.drag = 0.94f;
            em.gravity = XMFLOAT3(0, -2.0f, 0);
            em.rotation = 0.6f;
            em.shaderType = wi::EmittedParticleSystem::SOFT;
            em.SetCollidersDisabled(true);
            em.SetOpacityCurveControl(0.05f, 0.25f);
            if (MaterialComponent* m = s.materials.GetComponent(e))
            {
                m->SetBaseColor(night_glow_ ? XMFLOAT4(0.32f, 0.34f, 0.38f, 0.16f) : XMFLOAT4(0.92f, 0.93f, 0.95f, 0.26f));
                m->userBlendMode = wi::enums::BLENDMODE_ALPHA;
            }
            TextureParticles(e, "spray_puff.png");
            spray_.push_back(e);
        }
    }
    if (lamps_on_)
        AddStreetLights(s);
    if (night_glow_) // not for a rainy day's lamps: the cabin screens bloomed into a white haze (D-083)
    {
        // After dark every lamp, lens and brake light glows harder: the blooming lights of a
        // night highway are most of that look.
        for (size_t i = 0; i < s.materials.GetCount(); ++i)
            if (s.materials[i].GetEmissiveStrength() > 0)
                s.materials[i].SetEmissiveStrength(s.materials[i].GetEmissiveStrength() * 1.8f); // more washes red to white
    }

    if (pad)
        BuildTrackDressing(); // S01 has its own barriers, lamps and start gantry
    BuildPuddles(s);
    fx_ = std::make_unique<CombatVisuals>(s);
    for (const PickupPad& pad : session_->Combat().Pads())
    {
        const AbilityLook look = Look(pad.ability);
        fx_->pads.push_back(fx_->Make("pickup", look.color, 2.0f));
    }
    fx_->Pool(fx_->lances, 40, "lance", Look(Ability::Lance).color, 4.0f);
    fx_->Pool(fx_->needles, 80, "needle", Look(Ability::Needle).color, 4.0f);
    fx_->Pool(fx_->traps, 40, "trap", Look(Ability::Trap).color, 3.0f);
    fx_->Pool(fx_->storms, 24, "storm", XMFLOAT4(1, 0.9f, 0.2f, 1), 1.0f);

    if (options_.autotest_seconds > 0)
    {
        AIParams pilot = MakeAIParams(AIDifficulty::Normal, 99);
        pilot.lateral_accel *= EffectsOf(options_.weather).grip; // the stand-in player drives to the weather too
        pilot.brake_decel *= EffectsOf(options_.weather).grip;
        autopilot_ = std::make_unique<AIDriver>(session_->Route(), ApplyWeather(def, EffectsOf(options_.weather)), pilot);
        autopilot_combat_ = std::make_unique<AICombatPlanner>(player_, session_->Route(), MakeAICombatParams(AIDifficulty::Normal, AIPersonality::Aggressor, 99));
    }

    // the player's turbo / exhaust sound models from the garage parts (D-072)
    {
        auto part = [&](const char* slot) {
            const auto it = options_.player_parts.find(std::string(kPerformancePrefix) + slot);
            return it == options_.player_parts.end() ? std::string() : it->second;
        };
        const std::string turbo = part("turbo"), exhaust = part("exhaust_sys");
        turbo_audio_ = std::make_unique<TurboAudioModel>(turbo == "race" ? 1.0f : turbo == "street" ? 0.6f : 0.0f);
        backfire_audio_ = std::make_unique<BackfireModel>(exhaust == "straight" ? 1.0f : exhaust == "sport" ? 0.5f : 0.0f, 1234u);
        exhaust_gain_ = exhaust == "straight" ? 1.4f : exhaust == "sport" ? 1.2f : 1.0f;
    }
    if (options_.audio)
    {
        for (size_t i = 0; i < session_->CarCount(); ++i)
            engine_handles_.push_back(options_.audio->AddEngine(session_->Car(i).Definition().audio_family, int(i) == player_));
        options_.audio->StartMusic("harbor");
    }

    session_->Start();
    if (options_.track != "pad")
    {
        // ProofPlay: everything replay needs to rebuild this exact race.
        proofplay::SessionSetup pp;
        pp.track = options_.track;
        pp.laps = options_.laps;
        pp.cars = int(session_->CarCount());
        pp.player_index = player_;
        for (size_t i = 0; i < session_->CarCount(); ++i)
            pp.grid.push_back(session_->Car(i).Definition().id);
        pp.player_parts = options_.player_parts;
        pp.difficulty = options_.difficulty == AIDifficulty::Easy ? "Easy" : options_.difficulty == AIDifficulty::Hard ? "Hard" : "Normal";
        pp.mode = int(options_.mode);
        pp.weather_kind = int(options_.weather.kind);
        pp.weather_intensity = options_.weather.intensity;
        pp.time_limit_s = options_.time_limit_s;
        pp.restrict_abilities = options_.restrict_abilities;
        for (Ability a : options_.allowed_abilities)
            pp.allowed_abilities.push_back(int(a));
        proofplay_.Begin(pp, RACER_PROOFPLAY_SESSIONS);
    }
    camera->zNearP = 0.1f;
    camera->zFarP = 3000.0f;
    camera->fov = XMConvertToRadians(65.0f);
    wi::backlog::post("[racer] race loaded: " + std::to_string(total) + " cars, " + std::to_string(session_->Combat().Pads().size()) + " pickups");
}

bool RacePath::PlayerDone() const
{
    return options_.mode == EventMode::Destruction ? session_->TimeUp() : session_->Rules().Participant(player_).finished;
}

void RacePath::ReadPlayerInput(float dt, VehicleCommand& c, CombatCommand& combat)
{
    using namespace wi::input;
    (void)dt;
    // Keyboard from the player's bindings (Settings, remappable); gamepad fixed. The arrow keys
    // stay as a second driving set unless the player bound one of them to something else.
    auto bound = [&](const char* action) {
        const auto it = options_.bindings.find(action);
        return it != options_.bindings.end() ? KeyButton(it->second) : BUTTON_NONE;
    };
    auto arrow = [&](BUTTON b) {
        for (const auto& [a, k] : options_.bindings)
            if (KeyButton(k) == b)
                return false;
        return Down(b);
    };
    auto down = [&](const char* action) { const BUTTON b = bound(action); return b != BUTTON_NONE && Down(b); };
    auto pressed = [&](const char* action) { const BUTTON b = bound(action); return b != BUTTON_NONE && Press(b); };
    auto press = [](char k) { return Press(static_cast<BUTTON>(k)); };
    const XMFLOAT4 stick = GetAnalog(GAMEPAD_ANALOG_THUMBSTICK_L);
    c.throttle = std::max((down("throttle") || arrow(KEYBOARD_BUTTON_UP)) ? 1.0f : 0.0f, GetAnalog(GAMEPAD_ANALOG_TRIGGER_R).x);
    c.brake = std::max((down("brake") || arrow(KEYBOARD_BUTTON_DOWN)) ? 1.0f : 0.0f, GetAnalog(GAMEPAD_ANALOG_TRIGGER_L).x);
    float steer = (down("steer_right") || arrow(KEYBOARD_BUTTON_RIGHT) ? 1.0f : 0.0f) - (down("steer_left") || arrow(KEYBOARD_BUTTON_LEFT) ? 1.0f : 0.0f);
    if (std::fabs(stick.x) > options_.stick_deadzone)
        steer = stick.x;
    c.steer = steer;
    c.handbrake = (down("handbrake") || Down(GAMEPAD_BUTTON_XBOX_R1)) ? 1.0f : 0.0f;

    // Plan 2.14 bindings (edges; accumulated until a simulation step consumes them).
    if (pressed("fire_forward") || Press(GAMEPAD_BUTTON_XBOX_A))
        combat.use = true, combat.direction = UseDirection::Forward;
    if (pressed("weapon") || Press(GAMEPAD_BUTTON_XBOX_R3)) // the garage-fitted weapon (D-064)
        combat.fire_mounted = true, pending_reach_ = 5;
    if (pressed("fire_back") || Press(GAMEPAD_BUTTON_XBOX_B))
        combat.use = true, combat.direction = UseDirection::Backward;
    if (pressed("cycle") || Press(GAMEPAD_BUTTON_XBOX_X))
        combat.cycle = true;
    if (pressed("drop") || Press(GAMEPAD_BUTTON_XBOX_Y))
        combat.drop = true;
    for (int i = 0; i < 3; ++i)
        if (press(char('1' + i)))
            combat.select = i;
    if (bound("reset") != BUTTON_NONE && Hold(bound("reset"), 60)) // ~1 s hold at 60 fps (plan 2.9)
    {
        session_->ResetCar(size_t(player_));
        reset_pending_ = true;
    }
}

void RacePath::Update(float dt)
{
    if (!error_.empty() || !session_)
    {
        RenderPath3D::Update(dt);
        return;
    }
    const bool autotest = options_.autotest_seconds > 0;
    if (!autotest && wi::input::Press(wi::input::KEYBOARD_BUTTON_ESCAPE))
        paused_ = !paused_;
    clock_.SetPaused(paused_ || (!autotest && !window_active_));

    VehicleCommand drive;
    CombatCommand combat;
    if (autotest)
    {
        const VehicleTelemetry& t = session_->Car(size_t(player_)).Telemetry();
        AIObservation o{ { t.position[0], t.position[2] }, t.heading_rad, t.forward_speed_kmh / 3.6f, session_->Rules().Participant(player_).s };
        drive = autopilot_->Drive(o, dt);
        combat = autopilot_combat_->Think(session_->Combat(), session_->CombatCars(), dt);
    }
    else
    {
        ReadPlayerInput(dt, drive, combat);
        ReadCarFeatureInput();
    }
    // Merge edges so a press is never lost when a frame runs zero steps.
    pending_combat_.use |= combat.use;
    if (combat.use)
        pending_combat_.direction = combat.direction;
    pending_combat_.cycle |= combat.cycle;
    pending_combat_.drop |= combat.drop;
    if (combat.select >= 0)
        pending_combat_.select = combat.select;

    const ClockAdvance advance = clock_.Advance(dt);
    // Physics runs on the true poses: undo last frame's display blend first (bit-exact restore).
    if (sim_blended_)
        WriteSimPoses(sim_curr_, sim_curr_, 0.0f), sim_blended_ = false;
    for (uint32_t i = 0; i < advance.steps; ++i)
    {
        ReadSimPoses(sim_prev_);
        const CombatCommand step_combat = i == 0 ? pending_combat_ : CombatCommand{};
        proofplay_.BeforeStep(*session_, { drive, step_combat, reset_pending_ });
        reset_pending_ = false;
        session_->Step(drive, step_combat, float(clock_.StepSeconds()));
        proofplay_.AfterStep(*session_);
        frame_events_.insert(frame_events_.end(), session_->Combat().Events().begin(), session_->Combat().Events().end());
        if (i == 0)
            pending_combat_ = CombatCommand{};
    }
    // Show the cars between the last two steps: with 120 Hz physics and a 60/75/144 Hz display the
    // steps per frame vary (0, 1, 2...), which made cars and the camera judder (owner: "the camera
    // shakes"). Blending by the leftover fraction moves them evenly every frame.
    if (advance.steps > 0 || sim_curr_.size() != session_->CarCount())
        ReadSimPoses(sim_curr_);
    if (sim_prev_.size() == sim_curr_.size() && !sim_curr_.empty())
    {
        WriteSimPoses(sim_prev_, sim_curr_, advance.interpolation_alpha);
        sim_blended_ = true;
    }
    time_ += dt;

    if (!autotest)
    {
        if (PlayerDone())
        {
            finished_for_s_ += dt;
            if (finished_for_s_ > 1.5f && (wi::input::Press(wi::input::KEYBOARD_BUTTON_ENTER) || wi::input::Press(wi::input::GAMEPAD_BUTTON_XBOX_A)))
                exit_requested_ = true;
        }
        else if (paused_ && wi::input::Press(static_cast<wi::input::BUTTON>('Q')))
        {
            exit_requested_ = true; // abandon: PlayerSummary().finished stays false
        }
    }

    if (visual_root_y_.size() != session_->CarCount())
    {
        visual_root_y_.resize(session_->CarCount());
        for (size_t i = 0; i < visual_root_y_.size(); ++i)
            visual_root_y_[i] = -session_->Car(i).BodyOriginHeight(); // LoadCarVisual's offset
    }
    for (size_t i = 0; i < session_->CarCount(); ++i)
        session_->Car(i).UpdateWheelVisuals(visual_root_y_[i]);
    // Imported cars (D-035) are modelled standing on their own wheels; our suspension settles the
    // chassis lower than the nominal origin height, which sank their bodies over the wheels (side
    // view, 2026-09-29). Once the grid has settled, seat each model so its wheel centres (at
    // wheel_radius above its ground) meet the physics wheel centres.
    if (!visuals_aligned_ && time_ > 1.0f && visual_roots_.size() == session_->CarCount())
    {
        visuals_aligned_ = true;
        for (size_t i = 0; i < session_->CarCount(); ++i)
        {
            VehicleRuntime& car = session_->Car(i);
            if (car.Definition().id.empty() || car.Definition().id[0] != 'L')
                continue;
            visual_root_y_[i] = car.WheelCentreHeight() - car.Definition().wheel_radius_m;
            if (TransformComponent* t = scene->transforms.GetComponent(visual_roots_[i]))
            {
                t->translation_local.y = visual_root_y_[i];
                t->SetDirty();
            }
        }
    }
    SyncCombatVisuals(dt);
    RenderPath3D::Update(dt);
    UpdateCamera(dt);
    UpdateSky(dt);
    UpdateWeather(dt);
    if (TransformComponent* t = scene->transforms.GetComponent(reflection_probe_))
    {
        const VehicleTelemetry& me = session_->Car(size_t(player_)).Telemetry();
        t->translation_local = XMFLOAT3(me.position[0], me.position[1] + 1.5f, me.position[2]);
        t->SetDirty();
    }
    AnimateBodies(dt);
    UpdateCarRigs(dt);
    UpdateDamage();
    UpdateMirrors(dt);
    UpdateAudio(dt);
    frame_events_.clear(); // consumed by the body animation and the audio
    if (autotest)
        RecordFrame(dt);
}

void RacePath::AnimateBodies(float dt)
{
    if (dt <= 0.0f)
        return;
    if (body_anim_.size() != visual_roots_.size())
    {
        body_anim_.clear();
        body_anim_.resize(visual_roots_.size());
        for (size_t i = 0; i < visual_roots_.size(); ++i)
        {
            body_anim_[i].body = scene->Entity_FindByName("BODY", visual_roots_[i]);
            if (const TransformComponent* t = scene->transforms.GetComponent(body_anim_[i].body))
            {
                body_anim_[i].base_rotation = t->rotation_local;
                body_anim_[i].base_scale = t->scale_local;
            }
        }
    }
    // Hits kick the body of the car that was hit, from the side the shot came from.
    for (const CombatEvent& ev : frame_events_)
        if (ev.type == CombatEventType::Damage && ev.target >= 0 && size_t(ev.target) < body_anim_.size())
        {
            const VehicleTelemetry& t = session_->Car(size_t(ev.target)).Telemetry();
            const float side = ev.impulse_dir.x * std::cos(t.heading_rad) - ev.impulse_dir.z * std::sin(t.heading_rad);
            const float front = ev.impulse_dir.x * std::sin(t.heading_rad) + ev.impulse_dir.z * std::cos(t.heading_rad);
            body_anim_[size_t(ev.target)].motion.Hit(-side, -front, std::min(1.5f, ev.amount / 20.0f + 0.4f));
        }
    for (size_t i = 0; i < body_anim_.size() && i < session_->CarCount(); ++i)
    {
        BodyAnim& a = body_anim_[i];
        TransformComponent* tr = scene->transforms.GetComponent(a.body);
        if (!tr)
            continue;
        const VehicleTelemetry& t = session_->Car(i).Telemetry();
        BodyMotionInput in;
        in.forward_speed = t.forward_speed_kmh / 3.6f;
        in.yaw_rate = t.yaw_rate;
        in.vertical_speed = t.velocity[1];
        in.grounded = t.AnyWheelContact();
        a.motion.Update(in, dt);
        const BodyPose& pose = a.motion.Pose();
        // Car space: +Z forward, +X left side is -X right (D-002). Roll about Z, pitch about X.
        const XMVECTOR q = XMQuaternionMultiply(XMLoadFloat4(&a.base_rotation), XMQuaternionRotationRollPitchYaw(-pose.pitch, 0.0f, -pose.roll));
        XMStoreFloat4(&tr->rotation_local, q);
        const float wide = 1.0f / std::sqrt(pose.squash); // keep the volume: squash spreads it
        tr->scale_local = XMFLOAT3(a.base_scale.x * wide, a.base_scale.y * pose.squash, a.base_scale.z * wide);
        tr->SetDirty();
        if (int(i) == player_)
        {
            max_roll_ = std::max(max_roll_, std::fabs(pose.roll));
            max_squash_dip_ = std::max(max_squash_dip_, 1.0f - pose.squash);
            if (std::fabs(t.yaw_rate) > 0.15f && in.forward_speed > 10.0f && std::fabs(pose.roll) > 0.005f)
            {
                ++turn_frames_;
                outward_frames_ += (pose.roll > 0.0f) != (t.yaw_rate > 0.0f); // leans away from the turn
            }
        }
    }
}

// The player's performance-part sound events (D-072), run with or without an audio device so the
// race report can count them in autotests.
void RacePath::UpdatePartSounds(float dt)
{
    part_blowoffs_ = part_pops_ = 0;
    if (!turbo_audio_ || !backfire_audio_ || dt <= 0.0f || size_t(player_) >= session_->CarCount())
        return;
    const VehicleTelemetry& t = session_->Car(size_t(player_)).Telemetry();
    const DriverInput& in = session_->Car(size_t(player_)).LastInput();
    const VehicleDefinition& d = session_->Car(size_t(player_)).Definition();
    const float throttle = std::max(0.0f, in.forward) * (1.0f - in.brake);
    const float rpm_frac = std::clamp((t.rpm - d.min_rpm) / std::max(1.0f, d.max_rpm - d.min_rpm), 0.0f, 1.0f);
    part_boost_ = turbo_audio_->Update(rpm_frac, throttle, dt);
    if (turbo_audio_->TakeBlowoff())
        ++part_blowoffs_, ++blowoff_count_;
    if (backfire_audio_->Update(rpm_frac, throttle, dt) > 0)
        ++part_pops_, ++pop_count_;
}

void RacePath::UpdateAudio(float dt)
{
    UpdatePartSounds(dt);
    AudioRuntime* audio = options_.audio;
    if (!audio || dt <= 0.0f)
        return;
    audio->SetVolumes(options_.volume_master, options_.volume_music, paused_ ? 0.0f : options_.volume_sfx);

    const XMVECTOR eye = XMLoadFloat3(&cam_eye_);
    XMFLOAT3 fwd, vel;
    XMStoreFloat3(&fwd, XMVector3Normalize(XMLoadFloat3(&cam_at_) - eye));
    XMStoreFloat3(&vel, (eye - XMLoadFloat3(&prev_eye_)) / dt);
    prev_eye_ = cam_eye_;
    audio->SetListener({ cam_eye_.x, cam_eye_.y, cam_eye_.z }, { fwd.x, fwd.y, fwd.z }, { vel.x, vel.y, vel.z });

    auto car_pos = [&](int i) {
        const VehicleTelemetry& t = session_->Car(size_t(i)).Telemetry();
        return AudioVec3{ t.position[0], t.position[1], t.position[2] };
    };
    for (size_t i = 0; i < engine_handles_.size(); ++i)
    {
        const VehicleTelemetry& t = session_->Car(i).Telemetry();
        const DriverInput& in = session_->Car(i).LastInput();
        EngineAudioInput e;
        e.rpm = t.rpm;
        e.throttle = std::max(0.0f, in.forward) * (1.0f - in.brake);
        e.airborne = !t.AnyWheelContact();
        e.position = { t.position[0], t.position[1], t.position[2] };
        e.velocity = { t.velocity[0], t.velocity[1], t.velocity[2] };
        audio->UpdateEngine(engine_handles_[i], e, dt);
        if (int(i) == player_)
            audio->SetEngineExtras(engine_handles_[i], exhaust_gain_, part_boost_);
    }
    for (int k = 0; k < part_blowoffs_; ++k)
        audio->PlaySfx("blowoff", 0.75f);
    for (int k = 0; k < part_pops_; ++k)
        audio->PlaySfx((pop_count_ - part_pops_ + k) % 2 ? "backfire2" : "backfire", 0.85f);

    // Start lights: one beep per countdown second, a distinct tone on green.
    const RaceRules& rules = session_->Rules();
    if (rules.Phase() == RacePhase::Countdown)
    {
        const int second = int(std::ceil(rules.CountdownRemaining()));
        if (second != last_countdown_)
            audio->PlaySfx("countdown");
        last_countdown_ = second;
    }
    if (rules.Phase() == RacePhase::Racing && last_phase_ == RacePhase::Countdown)
        audio->PlaySfx("go");
    last_phase_ = rules.Phase();

    for (const CombatEvent& ev : frame_events_)
    {
        const int at = ev.target >= 0 ? ev.target : ev.source;
        if (at < 0)
            continue;
        const AudioVec3 p = car_pos(at);
        const bool mine = ev.source == player_ || ev.target == player_;
        if (mine)
            intensity_hold_s_ = 4.0f;
        const float g = mine ? 1.0f : 0.5f; // opponents' fights stay in the background
        switch (ev.type)
        {
        case CombatEventType::Pickup: audio->PlaySfx("pickup", 0.8f * g, &p); break;
        case CombatEventType::Use:
        {
            std::string name = std::string("use_") + ToString(ev.ability);
            std::transform(name.begin(), name.end(), name.begin(), [](char ch) { return char(std::tolower(ch)); });
            const AudioVec3 from = car_pos(ev.source);
            audio->PlaySfx(name, g, &from);
            break;
        }
        case CombatEventType::Damage: audio->PlaySfx(ev.amount >= 15.0f ? "impact_heavy" : "impact_light", g, &p); break;
        case CombatEventType::Blocked:
        case CombatEventType::WardDown: audio->PlaySfx("block", 0.9f * g, &p); break;
        case CombatEventType::Wreck: audio->PlaySfx("wreck", g, &p); break;
        case CombatEventType::StormWarning:
            if (ev.target == player_)
                audio->PlaySfx("warning");
            break;
        default: break;
        }
    }

    // Threat warning for homing shots locked on the player (plan 2.8), once per projectile.
    for (const Projectile& pr : session_->Combat().Projectiles())
        if (pr.kind == Ability::Lance && pr.target == player_ &&
            std::find(warned_projectiles_.begin(), warned_projectiles_.end(), pr.id) == warned_projectiles_.end())
        {
            warned_projectiles_.push_back(pr.id);
            audio->PlaySfx("warning");
        }

    intensity_hold_s_ = std::max(0.0f, intensity_hold_s_ - dt);
    audio->SetMusicIntensity(intensity_hold_s_ > 0.0f ? 1.0f : 0.25f);
    audio->Update(dt);
}

void RacePath::SyncCombatVisuals(float dt)
{
    (void)dt;
    const CombatSystem& c = session_->Combat();
    const float spin = time_ * 2.0f;
    for (size_t i = 0; i < c.Pads().size(); ++i)
    {
        const PickupPad& pad = c.Pads()[i];
        if (pad.respawn_left > 0.0f)
            fx_->Hide(fx_->pads[i]);
        else
            fx_->Place(fx_->pads[i], XMFLOAT3(pad.position.x, 1.1f + 0.15f * std::sin(time_ * 3 + i), pad.position.z), Look(pad.ability).half, spin);
    }
    size_t nl = 0, nn = 0, nt = 0, ns = 0;
    for (const Projectile& p : c.Projectiles())
    {
        const float yaw = std::atan2(p.velocity.x, p.velocity.z);
        if (p.kind == Ability::Lance && nl < fx_->lances.size())
            fx_->Place(fx_->lances[nl++], XMFLOAT3(p.position.x, 0.9f, p.position.z), XMFLOAT3(0.25f, 0.25f, 1.2f), yaw);
        else if (p.kind == Ability::Needle && nn < fx_->needles.size())
            fx_->Place(fx_->needles[nn++], XMFLOAT3(p.position.x, 0.9f, p.position.z), XMFLOAT3(0.08f, 0.08f, 0.7f), yaw);
    }
    for (const Trap& t : c.Traps())
        if (nt < fx_->traps.size())
        {
            const bool armed = t.flight_left <= 0.0f && t.age >= c.Tuning().trap_arm_s;
            const float y = t.flight_left > 0.0f ? 1.5f : 0.12f;
            fx_->Place(fx_->traps[nt], XMFLOAT3(t.position.x, y, t.position.z), XMFLOAT3(0.9f, armed ? 0.12f : 0.3f, 0.9f), time_ * 4);
            ++nt;
        }
    const TrackRoute& route = session_->Route();
    for (const StormZone& z : c.StormZones())
        if (ns < fx_->storms.size())
        {
            const float mid_s = z.route_s + c.Tuning().storm_zone_length_m * 0.5f;
            const Vec2 p = route.PointAt(mid_s), d = route.DirectionAt(mid_s);
            const float lat = (z.lateral_min + z.lateral_max) * 0.5f;
            const float width = (z.lateral_max - z.lateral_min) * 0.5f;
            const bool warning = z.warning_left > 0.0f;
            // Warning pulses yellow and low; active is solid red and tall (shape + colour).
            const float pulse = 0.5f + 0.5f * std::sin(time_ * 12.0f);
            fx_->SetEmissive(fx_->storms[ns], warning ? XMFLOAT4(1, 0.85f, 0.1f, 1) : XMFLOAT4(1, 0.1f, 0.05f, 1), warning ? 1.0f + 2.0f * pulse : 6.0f);
            fx_->Place(fx_->storms[ns], XMFLOAT3(p.x + d.z * lat, warning ? 0.1f : 1.2f, p.z - d.x * lat),
                       XMFLOAT3(width, warning ? 0.08f : 1.2f, c.Tuning().storm_zone_length_m * 0.5f), std::atan2(d.x, d.z));
            ++ns;
        }
    for (; nl < fx_->lances.size(); ++nl) fx_->Hide(fx_->lances[nl]);
    for (; nn < fx_->needles.size(); ++nn) fx_->Hide(fx_->needles[nn]);
    for (; nt < fx_->traps.size(); ++nt) fx_->Hide(fx_->traps[nt]);
    for (; ns < fx_->storms.size(); ++ns) fx_->Hide(fx_->storms[ns]);
}

void RacePath::ReadSimPoses(std::vector<SimPose>& out)
{
    out.resize(session_->CarCount());
    for (size_t i = 0; i < out.size(); ++i)
        if (const TransformComponent* t = scene->transforms.GetComponent(session_->Car(i).Body()))
            out[i].p = t->translation_local, out[i].q = t->rotation_local;
}

void RacePath::WriteSimPoses(const std::vector<SimPose>& a, const std::vector<SimPose>& b, float k)
{
    for (size_t i = 0; i < b.size() && i < a.size(); ++i)
        if (TransformComponent* t = scene->transforms.GetComponent(session_->Car(i).Body()))
        {
            if (k <= 0.0f)
                t->translation_local = b[i].p, t->rotation_local = b[i].q;
            else
            {
                // a teleport (recovery, reset) is not blended across
                const XMVECTOR pa = XMLoadFloat3(&a[i].p), pb = XMLoadFloat3(&b[i].p);
                const bool jump = XMVectorGetX(XMVector3LengthSq(pb - pa)) > 4.0f;
                XMStoreFloat3(&t->translation_local, jump ? pb : XMVectorLerp(pa, pb, k));
                XMStoreFloat4(&t->rotation_local, jump ? XMLoadFloat4(&b[i].q) : XMQuaternionSlerp(XMLoadFloat4(&a[i].q), XMLoadFloat4(&b[i].q), k));
            }
            t->SetDirty();
            t->UpdateTransform();
        }
}

static float Smooth01(float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }

void RacePath::UpdateCamera(float dt)
{
    const TransformComponent* body = scene->transforms.GetComponent(session_->Car(size_t(player_)).Body());
    if (!body)
        return;
    const XMMATRIX w = XMLoadFloat4x4(&body->world);
    const XMVECTOR pos = w.r[3];
    const XMVECTOR fwd = XMVector3Normalize(XMVectorSetY(w.r[2], 0));
    XMFLOAT3 eye, at;
    // Diagnostics only: RACER_DEBUG_CAM=side looks at the player's car from the side at wheel
    // height, to check wheel/arch fit of imported cars (D-035). Not a gameplay camera.
    if (const char* v = std::getenv("RACER_DEBUG_CAM"); v && (std::strcmp(v, "face") == 0 || std::strcmp(v, "face_side") == 0) &&
                                                        size_t(player_) < rigs_.size() && rigs_[size_t(player_)].has_eye)
    {
        // Diagnostics (D-068): the player's driver's face with the game's own shading and ink.
        const bool side = std::strcmp(v, "face_side") == 0;
        const XMVECTOR bf = XMVector3Normalize(w.r[2]), bu = XMVector3Normalize(w.r[1]), br = XMVector3Normalize(w.r[0]);
        const XMVECTOR e = XMVector3Transform(XMLoadFloat3(&rigs_[size_t(player_)].eye_local), w);
        XMStoreFloat3(&cam_eye_, side ? e - br * 1.3f + bf * 0.15f : e + bf * 0.75f + br * 0.12f + bu * 0.02f);
        XMStoreFloat3(&cam_at_, e - bu * 0.06f - bf * 0.05f);
        camera->Eye = cam_eye_;
        camera->At = XMFLOAT3(cam_at_.x - cam_eye_.x, cam_at_.y - cam_eye_.y, cam_at_.z - cam_eye_.z);
        camera->Up = XMFLOAT3(0, 1, 0);
        camera->fov = XMConvertToRadians(side ? 22.0f : 30.0f);
        camera->SetDirty();
        camera->UpdateCamera();
        return;
    }
    if (const char* v = std::getenv("RACER_DEBUG_CAM"); v && std::strcmp(v, "front") == 0)
    {
        // Diagnostics (D-072): low front three-quarter view - bumper parts, ride height.
        const XMVECTOR right = XMVector3Cross(XMVectorSet(0, 1, 0, 0), fwd);
        XMStoreFloat3(&cam_eye_, pos + fwd * 4.2f + right * 1.6f + XMVectorSet(0, 0.55f, 0, 0));
        XMStoreFloat3(&cam_at_, pos + fwd * 1.6f + XMVectorSet(0, 0.35f, 0, 0));
        camera->Eye = cam_eye_;
        camera->At = XMFLOAT3(cam_at_.x - cam_eye_.x, cam_at_.y - cam_eye_.y, cam_at_.z - cam_eye_.z);
        camera->Up = XMFLOAT3(0, 1, 0);
        camera->SetDirty();
        camera->UpdateCamera();
        return;
    }
    if (const char* v = std::getenv("RACER_DEBUG_CAM"); v && std::strcmp(v, "taunt") == 0)
    {
        // Diagnostics: the driver's side from ahead, so an arm out of the window reads clearly.
        const XMVECTOR right = XMVector3Cross(XMVectorSet(0, 1, 0, 0), fwd);
        XMStoreFloat3(&cam_eye_, pos - right * 2.4f + fwd * 2.2f + XMVectorSet(0, 1.3f, 0, 0));
        XMStoreFloat3(&cam_at_, pos - right * 0.8f - fwd * 0.3f + XMVectorSet(0, 1.0f, 0, 0));
        camera->Eye = cam_eye_;
        camera->At = XMFLOAT3(cam_at_.x - cam_eye_.x, cam_at_.y - cam_eye_.y, cam_at_.z - cam_eye_.z);
        camera->Up = XMFLOAT3(0, 1, 0);
        camera->SetDirty();
        camera->UpdateCamera();
        return;
    }
    if (const char* v = std::getenv("RACER_DEBUG_CAM"); v && std::strcmp(v, "window") == 0)
    {
        // Diagnostics: close look through the driver's window at the driver and the cabin (D-062).
        const XMVECTOR right = XMVector3Cross(XMVectorSet(0, 1, 0, 0), fwd);
        XMStoreFloat3(&cam_eye_, pos - right * 1.9f + fwd * 0.2f + XMVectorSet(0, 1.05f, 0, 0));
        XMStoreFloat3(&cam_at_, pos - right * 0.2f + fwd * 0.1f + XMVectorSet(0, 0.55f, 0, 0));
        camera->Eye = cam_eye_;
        camera->At = XMFLOAT3(cam_at_.x - cam_eye_.x, cam_at_.y - cam_eye_.y, cam_at_.z - cam_eye_.z);
        camera->Up = XMFLOAT3(0, 1, 0);
        camera->SetDirty();
        camera->UpdateCamera();
        return;
    }
    if (const char* v = std::getenv("RACER_DEBUG_CAM"); v && std::strcmp(v, "side") == 0)
    {
        const XMVECTOR right = XMVector3Cross(XMVectorSet(0, 1, 0, 0), fwd);
        XMStoreFloat3(&cam_eye_, pos - right * 5.5f + XMVectorSet(0, 0.7f, 0, 0)); // grid neighbour is on the right
        XMStoreFloat3(&cam_at_, pos + XMVectorSet(0, 0.5f, 0, 0));
        camera->Eye = cam_eye_;
        camera->At = XMFLOAT3(cam_at_.x - cam_eye_.x, cam_at_.y - cam_eye_.y, cam_at_.z - cam_eye_.z);
        camera->Up = XMFLOAT3(0, 1, 0);
        camera->SetDirty();
        camera->UpdateCamera();
        return;
    }
    // Camera feel (D-058): speed opens the view and pulls the camera back, turns swing it out and
    // roll it a little, hits, wall impacts and landings shake it.
    {
        const VehicleTelemetry& tel = session_->Car(size_t(player_)).Telemetry();
        for (const CombatEvent& ev : frame_events_)
        {
            if (ev.type == CombatEventType::Damage && ev.target == player_)
                cam_feel_.AddTrauma(std::min(0.8f, 0.3f + ev.amount / 40.0f));
            else if (ev.type == CombatEventType::Damage && ev.source == player_)
                cam_feel_.AddTrauma(0.12f); // the player's own hit lands: a small kick of feedback
        }
        const float speed = tel.forward_speed_kmh;
        if (cam_prev_speed_ - speed > 18.0f) // lost >18 km/h in one frame: a wall or car impact
            cam_feel_.AddTrauma(std::min(0.7f, (cam_prev_speed_ - speed) / 60.0f));
        cam_prev_speed_ = speed;
        const bool grounded = tel.AnyWheelContact();
        if (!grounded)
            cam_fall_speed_ = std::max(cam_fall_speed_, -tel.velocity[1]);
        else if (!cam_prev_grounded_)
        {
            cam_feel_.AddTrauma(std::clamp((cam_fall_speed_ - 2.0f) / 10.0f, 0.0f, 0.6f)); // landing
            cam_fall_speed_ = 0.0f;
        }
        cam_prev_grounded_ = grounded;
        CameraFeelInput in;
        in.speed_kmh = speed;
        in.yaw_rate = tel.yaw_rate;
        in.boost = session_->Combat().BoostAccel(player_) > 0.5f;
        in.reduce_shake = options_.reduce_shake;
        cam_feel_.Update(in, dt);
    }
    const CameraFeelOutput& feel = cam_feel_.Output();
    const XMVECTOR right = XMVector3Normalize(XMVector3Cross(XMVectorSet(0, 1, 0, 0), fwd));
    // Camera modes (D-060, key C): near and far chase, bonnet, cockpit (the driver's eyes). Tab looks
    // back in every mode. Bonnet and cockpit ride on the car: no lag, no wall push.
    const bool attached = cam_mode_ == CamMode::Hood || cam_mode_ == CamMode::Cockpit;
    const XMVECTOR body_fwd = XMVector3Normalize(w.r[2]), body_up = XMVector3Normalize(w.r[1]);
    const float far_k = cam_mode_ == CamMode::Far ? 1.4f : 1.0f; // "far" is a Windows macro
    if (!attached)
    {
        XMStoreFloat3(&eye, pos - fwd * feel.distance * far_k + right * feel.lateral + XMVectorSet(0, feel.height * (far_k > 1 ? 1.3f : 1.0f), 0, 0));
        XMStoreFloat3(&at, pos + fwd * 4.0f + right * (feel.lateral * 0.3f) + XMVectorSet(0, 0.8f, 0, 0));
        if (look_back_)
        {
            // Look back (D-063): from just above the roof, looking down the road behind - the car's
            // own roof and boot at the bottom edge, the chasing pack filling the view.
            XMStoreFloat3(&eye, pos + fwd * 0.6f + XMVectorSet(0, 2.35f, 0, 0));
            XMStoreFloat3(&at, pos - fwd * 18.0f + XMVectorSet(0, 0.6f, 0, 0));
        }
    }
    else
    {
        XMVECTOR e = pos + body_fwd * 1.1f + body_up * 1.3f; // bonnet
        // Eye offset in body space, from the same body matrix as the rest of the camera: reading
        // the eye node's world matrix would lag a frame (0.8 m at 170 km/h) and shake the cabin.
        if (cam_mode_ == CamMode::Cockpit && size_t(player_) < rigs_.size() && rigs_[size_t(player_)].has_eye)
            e = XMVector3Transform(XMLoadFloat3(&rigs_[size_t(player_)].eye_local), w);
        // Looking back from the cockpit: the driver turns the head over the shoulder - the eye moves
        // up and to the centre so the view clears the headrest and goes out the rear glass.
        if (look_back_ && cam_mode_ == CamMode::Cockpit)
            e += XMVector3Normalize(XMVector3Cross(body_up, body_fwd)) * 0.3f + body_up * 0.08f + body_fwd * 0.05f;
        XMStoreFloat3(&eye, e);
        // Cockpit gaze (D-064): the mouse glances at the left, centre or right mirror.
        // The gaze turns to the chosen mirror glass itself (its marker in the model).
        XMVECTOR look = look_back_ ? -body_fwd : body_fwd;
        float drop = look_back_ ? 0.9f : 0.6f;
        if (!look_back_ && cam_mode_ == CamMode::Cockpit && glance_k_ > 0.001f && size_t(player_) < rigs_.size())
        {
            const CarRig& rg = rigs_[size_t(player_)];
            const int mi = glance_last_ - 1;
            if (rg.has_mirror[mi])
            {
                const XMVECTOR mw = XMVector3Transform(XMLoadFloat3(&rg.mirror_local[mi]), w);
                const XMVECTOR to = XMVector3Normalize(mw - e);
                look = XMVector3Normalize(XMVectorLerp(body_fwd * 10.0f, to * 10.0f, Smooth01(glance_k_)));
                drop *= 1.0f - glance_k_;
            }
        }
        XMStoreFloat3(&at, e + look * 10.0f - body_up * drop);
        cam_eye_ = eye;
        cam_at_ = at;
        cam_initialized_ = true;
    }
    if (!cam_initialized_)
    {
        cam_eye_ = eye;
        cam_at_ = at;
        cam_initialized_ = true;
    }
    if (!attached)
    {
        // Smooth the camera's offset from the car, not its world position: a world-space lag grows
        // with speed (8/s behind a 55 m/s car trails it by ~7 m - owner: "the camera drifts far from
        // the car"); the offset keeps the swing into turns and never falls back with speed.
        static bool off_primed = false;
        const XMVECTOR want_eye = XMLoadFloat3(&eye) - pos, want_at = XMLoadFloat3(&at) - pos;
        if (!off_primed)
            XMStoreFloat3(&cam_off_eye_, want_eye), XMStoreFloat3(&cam_off_at_, want_at), off_primed = true;
        XMStoreFloat3(&cam_off_eye_, XMVectorLerp(XMLoadFloat3(&cam_off_eye_), want_eye, ExpDecay(dt, look_back_ ? 30.0f : 7.0f)));
        XMStoreFloat3(&cam_off_at_, XMVectorLerp(XMLoadFloat3(&cam_off_at_), want_at, ExpDecay(dt, look_back_ ? 30.0f : 12.0f)));
        XMStoreFloat3(&cam_eye_, pos + XMLoadFloat3(&cam_off_eye_));
        XMStoreFloat3(&cam_at_, pos + XMLoadFloat3(&cam_off_at_));
    }
    const XMVECTOR pivot = pos + XMVectorSet(0, 1.2f, 0, 0);
    const XMVECTOR to_eye = XMLoadFloat3(&cam_eye_) - pivot;
    const float dist = attached ? 0.0f : XMVectorGetX(XMVector3Length(to_eye));
    if (dist > 0.01f)
    {
        XMFLOAT3 o, d;
        XMStoreFloat3(&o, pivot);
        XMStoreFloat3(&d, to_eye / dist);
        const wi::physics::RayIntersectionResult hit = wi::physics::Intersects(*scene, wi::primitive::Ray(o, d, 0.0f, dist + 0.3f));
        bool is_car = false; // cars alongside no longer push the camera in and out every frame
        for (size_t i = 0; i < session_->CarCount() && !is_car; ++i)
            is_car = hit.entity == session_->Car(i).Body();
        float clear = dist;
        if (hit.entity != INVALID_ENTITY && !is_car)
            clear = std::min(dist, std::max(0.3f, XMVectorGetX(XMVector3Length(XMLoadFloat3(&hit.position) - pivot)) - 0.3f));
        // in at once (never through a wall), back out smoothly
        cam_clear_ = clear < cam_clear_ ? clear : cam_clear_ + (clear - cam_clear_) * ExpDecay(dt, 4.0f);
        if (cam_clear_ < dist)
            XMStoreFloat3(&cam_eye_, pivot + XMLoadFloat3(&d) * cam_clear_);
    }
    if (std::getenv("RACER_CAM_LOG")) // diagnostics (D-067): visual car speed and camera distance per frame
    {
        static XMFLOAT3 last{};
        XMFLOAT3 p;
        XMStoreFloat3(&p, pos);
        const float vis = dt > 0 ? std::hypot(p.x - last.x, p.z - last.z) / dt : 0.0f;
        const float cd = XMVectorGetX(XMVector3Length(XMLoadFloat3(&cam_eye_) - pos));
        wi::backlog::post("[cam] " + std::to_string(dt) + " " + std::to_string(vis) + " " + std::to_string(cd) + " " + std::to_string(session_->Car(size_t(player_)).Telemetry().forward_speed_kmh));
        last = p;
    }
    // Shake moves eye and target together (the view jolts, it does not swivel); roll tilts "up".
    const XMVECTOR shake = right * feel.shake_x + XMVectorSet(0, feel.shake_y, 0, 0);
    XMFLOAT3 eye_s, at_s;
    XMStoreFloat3(&eye_s, XMLoadFloat3(&cam_eye_) + shake);
    XMStoreFloat3(&at_s, XMLoadFloat3(&cam_at_) + shake);
    const float roll = XMConvertToRadians(feel.roll_deg);
    XMFLOAT3 up;
    XMStoreFloat3(&up, attached ? body_up : XMVector3Normalize(XMVectorSet(0, 1, 0, 0) * std::cos(roll) + right * std::sin(roll)));
    setlayerMask(cam_mode_ == CamMode::Cockpit ? ~kPlayerHeadLayer : ~0u);
    camera->fov = XMConvertToRadians(cam_mode_ == CamMode::Cockpit ? (feel.fov_deg + 6.0f) * (1.0f - glance_k_) + 42.0f * glance_k_ : feel.fov_deg);
    camera->zNearP = attached ? 0.05f : 0.1f;
    camera->Eye = eye_s;
    camera->At = XMFLOAT3(at_s.x - eye_s.x, at_s.y - eye_s.y, at_s.z - eye_s.z);
    camera->Up = up;
    camera->SetDirty();
    camera->UpdateCamera();
}

void RacePath::RecordFrame(float dt)
{
    elapsed_s_ += dt;
    if (session_)
        max_speed_kmh_ = std::max(max_speed_kmh_, session_->Car(size_t(player_)).Telemetry().forward_speed_kmh);
    if (elapsed_s_ >= next_sample_s_)
    {
        memory_samples_.push_back({ elapsed_s_, PrivateBytes() / 1048576.0, wi::graphics::GetDevice()->GetMemoryUsage().usage / 1048576.0 });
        next_sample_s_ += 5.0f;
    }
    if (elapsed_s_ >= 5.0f)
        frame_ms_.push_back(dt * 1000.0f);
    if (elapsed_s_ >= options_.autotest_seconds && !autotest_done_)
    {
        autotest_done_ = true;
        wi::backlog::post("[racer] race autotest finished after " + std::to_string(elapsed_s_) + " s");
        // Performance gate (D-092, owner: "keep an eye on optimisation"): the run fails, with the
        // number in the report's error, when a budget is exceeded. ctest runs it on every build.
        if (options_.budget_mean_ms > 0 && !frame_ms_.empty())
        {
            float mean = 0;
            for (float v : frame_ms_)
                mean += v / float(frame_ms_.size());
            const float p99 = Percentile(frame_ms_, 0.99f);
            const double ram = memory_samples_.empty() ? 0.0 : memory_samples_.back().ram_mib;
            char why[160] = {};
            if (mean > options_.budget_mean_ms)
                std::snprintf(why, sizeof why, "perf budget: mean frame %.2f ms > %.2f", mean, options_.budget_mean_ms);
            else if (options_.budget_p99_ms > 0 && p99 > options_.budget_p99_ms)
                std::snprintf(why, sizeof why, "perf budget: p99 frame %.2f ms > %.2f", p99, options_.budget_p99_ms);
            else if (options_.budget_ram_mib > 0 && ram > options_.budget_ram_mib)
                std::snprintf(why, sizeof why, "perf budget: RAM %.0f MiB > %.0f", ram, options_.budget_ram_mib);
            if (why[0])
                MarkFailed(why);
        }
    }
}

void RacePath::WriteReport() const
{
    std::ofstream out(options_.report_path);
    float mean = 0;
    for (float v : frame_ms_)
        mean += v / std::max<size_t>(1, frame_ms_.size());
    int finished = 0;
    const RaceSession::Stats st = session_ ? session_->GetStats() : RaceSession::Stats{};
    if (session_)
        for (auto& p : session_->Rules().Participants())
            finished += p.finished;
    out << "{\n"
        << "  \"error\": \"" << error_ << "\",\n"
        << "  \"seconds\": " << elapsed_s_ << ",\n"
        << "  \"cars\": " << (session_ ? session_->CarCount() : 0) << ",\n"
        << "  \"phase\": \"" << (session_ ? ToString(session_->Rules().Phase()) : "none") << "\",\n"
        << "  \"finished\": " << finished << ",\n"
        << "  \"player_position\": " << (session_ ? session_->Rules().Participant(player_).position : 0) << ",\n"
        << "  \"player_car\": \"" << (session_ ? session_->Car(size_t(player_)).Definition().id : std::string()) << "\",\n"
        << "  \"player_max_kmh\": " << max_speed_kmh_ << ",\n"
        << "  \"body_max_roll_deg\": " << max_roll_ * 57.2958f << ", \"body_max_squash\": " << max_squash_dip_ << ", \"body_outward_lean_pct\": " << (turn_frames_ ? 100.0f * float(outward_frames_) / float(turn_frames_) : 0.0f) << ",\n"
        << "  \"field\": \"" << [&] { std::string f; for (size_t i = 0; session_ && i < session_->CarCount(); ++i) f += (i ? " " : "") + session_->Car(i).Definition().id; return f; }() << "\",\n"
        << "  \"frame_ms_mean\": " << mean << ",\n"
        << "  \"frame_ms_p95\": " << Percentile(frame_ms_, 0.95f) << ",\n"
        << "  \"frame_ms_p99\": " << Percentile(frame_ms_, 0.99f) << ",\n"
        << "  \"uses\": " << st.uses << ", \"hits\": " << st.hits << ", \"blocks\": " << st.blocks << ", \"wrecks\": " << st.wrecks
        << ", \"pickups\": " << st.pickups << ", \"recoveries\": " << st.recoveries << ", \"insane_steps\": " << st.insane_steps << ", \"blowoffs\": " << blowoff_count_ << ", \"pops\": " << pop_count_ << ",\n"
        << "  \"dents_all_cars\": [" << [&] { std::string f; for (size_t i = 0; session_ && i < session_->CarCount(); ++i) { const DentState& d = session_->Dents(i); char b[64]; std::snprintf(b, sizeof b, "%s[%.2f,%.2f,%.2f,%.2f]", i ? "," : "", d.depth[0], d.depth[1], d.depth[2], d.depth[3]); f += b; } return f; }() << "],\n"
        << "  \"memory_samples\": [";
    for (size_t i = 0; i < memory_samples_.size(); ++i)
        out << (i ? ", " : "") << "[" << memory_samples_[i].t << ", " << memory_samples_[i].ram_mib << ", " << memory_samples_[i].vram_mib << "]";
    out << "]\n}\n";
}

void RacePath::Compose(wi::graphics::CommandList cmd) const
{
    RenderPath3D::Compose(cmd);
    // Manga speed lines (D-058): thin white streaks radiating from the screen centre, only in the
    // outer ring; a new random set every 50 ms reads as rushing air. Off with reduced shake.
    if (const float lines = cam_feel_.Output().speed_lines; lines > 0.02f)
    {
        const float W = GetLogicalWidth(), H = GetLogicalHeight();
        std::mt19937 rng(uint32_t(elapsed_s_ * 20.0f));
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        const int n = int(18 + 30 * lines);
        for (int i = 0; i < n; ++i)
        {
            const float a = u(rng) * XM_2PI;
            const float r0 = 0.58f + 0.2f * u(rng), len = 0.12f + 0.25f * u(rng) * lines;
            const float cx = W * 0.5f + std::cos(a) * W * 0.5f * r0, cy = H * 0.5f + std::sin(a) * H * 0.5f * r0 * 1.2f;
            wi::image::Params q(cx, cy, W * 0.5f * len, 1.5f + 2.0f * u(rng));
            q.pivot = XMFLOAT2(0.0f, 0.5f);
            q.rotation = a;
            q.color = XMFLOAT4(1, 1, 1, (0.18f + 0.35f * u(rng)) * lines);
            q.blendFlag = wi::enums::BLENDMODE_ALPHA;
            wi::image::Draw(nullptr, q, cmd);
        }
    }
    // Accessibility (plan 2.14): HUD text scale and a high-contrast mode (thick dark outline, full
    // brightness). The menus keep their fixed layout: at 144 dpi they already fill the screen.
    const float ui = options_.ui_scale;
    auto style = [&](wi::font::Params& q) {
        q.shadowColor = wi::Color::Black();
        if (options_.high_contrast)
        {
            q.shadow_bolden = 0.7f;
            q.shadow_softness = 0.05f;
            q.shadow_offset_x = q.shadow_offset_y = 0.0f;
            q.enableSDFRendering();
        }
    };
    wi::font::Params p;
    p.posX = 24;
    p.posY = 130 * ui;
    p.size = int(24 * ui);
    style(p);
    p.color = wi::Color::White();
    if (!error_.empty())
    {
        p.color = wi::Color(255, 90, 90, 255);
        wi::font::Draw("ERROR: " + error_, p, cmd);
        return;
    }
    if (!session_)
        return;
    if (toast_t_ > 0.0f) // camera / radio name, fading out
    {
        wi::font::Params q;
        q.posX = float(GetLogicalWidth()) * 0.5f;
        q.posY = float(GetLogicalHeight()) * 0.22f;
        q.size = int(26 * ui);
        q.h_align = wi::font::WIFALIGN_CENTER;
        style(q);
        q.color = wi::Color(255, 220, 120, uint8_t(255 * std::min(1.0f, toast_t_ / 0.4f)));
        wi::font::Draw(toast_, q, cmd);
    }

    const RaceRules& rules = session_->Rules();
    const ParticipantState& me = rules.Participant(player_);
    const Combatant& cb = session_->Combat().Get(player_);
    const int lap = std::min(me.laps_done + 1, int(rules.Participants().size() > 0 ? 99 : 0));
    char line[256];
    if (!options_.title.empty())
    {
        wi::font::Params title = p;
        title.posX = float(GetLogicalWidth()) - 24;
        title.h_align = wi::font::WIFALIGN_RIGHT;
        title.color = wi::Color(255, 190, 90, 255);
        wi::font::Draw(options_.title, title, cmd);
    }
    const float race_time = session_->RaceTime(size_t(player_));
    const RaceSummary sum = session_->Summary(size_t(player_));
    if (options_.mode == EventMode::Destruction)
    {
        // Hunt HUD: hits and wrecks dealt against the clock, no laps or position.
        const float left = session_->TimeLeft();
        std::snprintf(line, sizeof(line), "HITS %d    TIME %d:%04.1f    %.0f km/h", sum.hits_dealt, int(left) / 60, std::fmod(left, 60.0f),
                      session_->Car(size_t(player_)).Telemetry().forward_speed_kmh);
    }
    else
        std::snprintf(line, sizeof(line), "POS %d/%zu    LAP %d/%d    %d:%04.1f    %.0f km/h", me.position, rules.Participants().size(),
                      std::min(lap, options_.laps), options_.laps, int(race_time) / 60, std::fmod(race_time, 60.0f),
                      session_->Car(size_t(player_)).Telemetry().forward_speed_kmh);
    wi::font::Draw(line, p, cmd);

    p.posY += 34 * ui;
    const float frac = cb.health / cb.max_health;
    p.color = frac > 0.5f ? wi::Color(120, 255, 140, 255) : frac > 0.25f ? wi::Color(255, 210, 80, 255) : wi::Color(255, 80, 80, 255);
    std::string hp = "HP " + std::to_string(int(cb.health)) + "/" + std::to_string(int(cb.max_health));
    if (cb.ward_time > 0.0f)
        hp += "   WARD " + std::to_string(cb.ward_hits);
    if (cb.protect_time > 0.0f)
        hp += "   PROTECTED";
    wi::font::Draw(hp, p, cmd);

    p.posY += 34 * ui;
    p.color = wi::Color::White();
    std::string inv;
    for (int i = 0; i < 3; ++i)
        inv += "[" + std::to_string(i + 1) + "]" + SlotText(cb.slots[size_t(i)], cb.selected == i) + "  ";
    wi::font::Draw(inv, p, cmd);

    // Centre messages.
    wi::font::Params big;
    big.posX = float(GetLogicalWidth()) * 0.5f;
    big.posY = float(GetLogicalHeight()) * 0.3f;
    big.h_align = wi::font::WIFALIGN_CENTER;
    big.size = int(72 * ui);
    style(big);
    std::string centre;
    if (rules.Phase() == RacePhase::Countdown)
        centre = std::to_string(int(std::ceil(rules.CountdownRemaining())));
    else if (paused_)
        centre = "PAUSED   (" + options_.text_abandon + ")";
    else if (me.wrong_way)
        centre = "WRONG WAY", big.color = wi::Color(255, 80, 80, 255);
    else if (options_.mode == EventMode::Destruction && session_->TimeUp())
        centre = "TIME UP   " + std::to_string(sum.hits_dealt) + " HITS";
    else if (me.finished)
        centre = "FINISHED  P" + std::to_string(me.position);
    if (!centre.empty())
        wi::font::Draw(centre, big, cmd);

    if (options_.mode == EventMode::Destruction && session_->TimeUp() && finished_for_s_ > 1.5f)
    {
        wi::font::Params r = p;
        r.posX = float(GetLogicalWidth()) * 0.5f - 160;
        r.posY = float(GetLogicalHeight()) * 0.45f;
        r.size = int(24 * ui);
        r.color = wi::Color(140, 255, 160, 255);
        std::snprintf(line, sizeof(line), "hits %d   wrecks caused %d   damage taken %.0f   ", sum.hits_dealt, session_->WrecksCaused(size_t(player_)), sum.damage_taken);
        wi::font::Draw(std::string(line) + options_.text_continue, r, cmd);
    }
    else if (me.finished || rules.Phase() == RacePhase::Results || rules.Phase() == RacePhase::Finished)
    {
        wi::font::Params r = p;
        r.posX = float(GetLogicalWidth()) * 0.5f - 160;
        r.posY = float(GetLogicalHeight()) * 0.4f;
        r.size = int(22 * ui);
        std::vector<const ParticipantState*> order;
        for (auto& q : rules.Participants())
            order.push_back(&q);
        std::sort(order.begin(), order.end(), [](auto a, auto b) { return a->position < b->position; });
        for (auto* q : order)
        {
            const std::string car_id = session_->Car(size_t(q->id)).Definition().id;
            std::snprintf(line, sizeof(line), "%2d.  %s #%02d%s   %s", q->position, car_id.c_str(), q->id, q->id == player_ ? " (you)" : "",
                          q->finished ? "finished" : "racing");
            r.color = q->id == player_ ? wi::Color(255, 160, 40, 255) : wi::Color::White();
            wi::font::Draw(line, r, cmd);
            r.posY += 26 * ui;
        }
        if (me.finished && finished_for_s_ > 1.5f)
        {
            r.posY += 12;
            r.color = wi::Color(140, 255, 160, 255);
            char t[64];
            std::snprintf(t, sizeof(t), "%.2f s   ", session_->RaceTime(size_t(player_)));
            wi::font::Draw(std::string(t) + options_.text_continue, r, cmd);
        }
    }

    wi::font::Params help = p;
    help.posY = float(GetLogicalHeight()) - 40 * ui;
    help.size = int(18 * ui);
    help.color = wi::Color(220, 220, 220, 255);
    auto k = [&](const char* a) { const auto it = options_.bindings.find(a); return it != options_.bindings.end() ? it->second : std::string("?"); };
    const std::string drive_keys = k("throttle") + "/" + k("brake") + " gas/brake  " + k("steer_left") + "/" + k("steer_right") + " steer  " + k("handbrake") + " handbrake";
    const std::string combat_keys = k("fire_forward") + " fire fwd  " + k("fire_back") + " fire back  " + k("cycle") + " cycle  1-3 slot  " + k("drop") + " drop  hold " + k("reset") + " reset  Esc pause";
    // Large HUD text (accessibility) no longer fits one line: split it instead of running off-screen.
    if (wi::font::TextWidth(drive_keys + "  " + combat_keys, help) > float(GetLogicalWidth()) - 48.0f)
    {
        help.posY -= float(help.size) * 1.2f;
        wi::font::Draw(drive_keys, help, cmd);
        help.posY += float(help.size) * 1.2f;
        wi::font::Draw(combat_keys, help, cmd);
    }
    else
        wi::font::Draw(drive_keys + "  " + combat_keys, help, cmd);
}
