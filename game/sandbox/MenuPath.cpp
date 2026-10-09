#include "MenuPath.h"
#include "CarVisual.h"
#include "racer/Performance.h"

#include <algorithm>
#include <cmath>
#include "KeyNames.h"

using namespace racer;

void MenuPath::Select(const std::string& event_id, const std::string& car)
{
    for (size_t i = 0; i < ctx_.events->events.size(); ++i)
        if (ctx_.events->events[i].id == event_id)
            selected_ = int(i);
    for (size_t i = 0; i < ctx_.cars.size(); ++i)
        if (ctx_.cars[i] == car)
            car_ = int(i);
}

void MenuPath::Ui(const char* sound) const
{
    if (ctx_.audio)
        ctx_.audio->PlaySfx(sound, 0.6f);
}

namespace
{
    enum SettingsRow { kMaster, kMusic, kSfx, kLanguage, kUiScale, kContrast, kMotionBlur, kShake, kDeadzone, kControls, kBack, kRows };
    float Step(float v, int dir) { return std::clamp(std::round((v + 0.1f * float(dir)) * 10.0f) / 10.0f, 0.0f, 1.0f); }
}

// Settings screen (plan 2.14 "Parametrlər"): volumes and language, keyboard and gamepad only,
// values applied live so the player hears the change. The game writes settings.json on exit.
void MenuPath::UpdateControls()
{
    // Keyboard remap (plan 2.14): pick an action, Enter, then press the new key. A key already in
    // use swaps with it (racer::Rebind), so no action is ever left without a key.
    const auto& actions = ControlActions();
    const int n = int(actions.size());
    if (listening_)
    {
        if (actions_.back)
            listening_ = false, Ui("ui_back");
        else if (!actions_.key.empty() && ctx_.settings)
        {
            Rebind(*ctx_.settings, actions[size_t(controls_row_)], actions_.key);
            listening_ = false;
            Ui("ui_confirm");
        }
        return;
    }
    if (actions_.down)
        controls_row_ = (controls_row_ + 1) % (n + 1), Ui("ui_move");
    if (actions_.up)
        controls_row_ = (controls_row_ + n) % (n + 1), Ui("ui_move");
    if (actions_.confirm && controls_row_ < n)
        listening_ = true, Ui("ui_confirm");
    else if (actions_.back || (actions_.confirm && controls_row_ == n))
        controls_open_ = false, Ui("ui_back");
}

void MenuPath::UpdateSettings()
{
    using namespace wi::input;
    if (controls_open_)
    {
        UpdateControls();
        return;
    }
    if (actions_.confirm && settings_row_ == kControls)
    {
        controls_open_ = true;
        controls_row_ = 0;
        Ui("ui_confirm");
        return;
    }
    if (actions_.down)
        settings_row_ = (settings_row_ + 1) % kRows, Ui("ui_move");
    if (actions_.up)
        settings_row_ = (settings_row_ + kRows - 1) % kRows, Ui("ui_move");
    const int dir = actions_.right ? 1 : actions_.left ? -1 : 0;
    racer::Settings* s = ctx_.settings;
    if (dir != 0 && s)
    {
        if (settings_row_ == kMaster) s->master_volume = Step(s->master_volume, dir);
        if (settings_row_ == kMusic) s->music_volume = Step(s->music_volume, dir);
        if (settings_row_ == kSfx) s->sfx_volume = Step(s->sfx_volume, dir);
        if (settings_row_ == kLanguage && ctx_.language) *ctx_.language = *ctx_.language == "az" ? "en" : "az";
        if (settings_row_ == kUiScale) s->ui_scale = std::clamp(std::round((s->ui_scale + 0.1f * float(dir)) * 10.0f) / 10.0f, 0.8f, 1.5f);
        if (settings_row_ == kContrast) s->high_contrast = !s->high_contrast;
        if (settings_row_ == kMotionBlur) s->motion_blur = !s->motion_blur;
        if (settings_row_ == kShake) s->reduce_shake = !s->reduce_shake;
        if (settings_row_ == kDeadzone) s->stick_deadzone = std::clamp(std::round((s->stick_deadzone + 0.05f * float(dir)) * 20.0f) / 20.0f, 0.05f, 0.4f);
        if (ctx_.audio)
            ctx_.audio->SetVolumes(s->master_volume, s->music_volume, s->sfx_volume);
        Ui("ui_move");
    }
    const bool back = actions_.back ||
                      (settings_row_ == kBack && actions_.confirm);
    if (back)
    {
        settings_open_ = false;
        Ui("ui_back");
    }
}

MenuPath::Actions MenuPath::ReadActions()
{
    Actions a;
    if (script_pos_ < script_.size())
    {
        switch (script_[script_pos_++])
        {
        case 'U': a.up = true; break;
        case 'D': a.down = true; break;
        case 'L': a.left = true; break;
        case 'R': a.right = true; break;
        case 'E': a.confirm = true; break;
        case 'B': a.back = true; break;
        case 'O': a.settings = true; break;
        case 'G': a.language = true; break;
        case 'C': a.customize = true; break;
        case 'k': // next character is the key pressed while the remap screen listens
            if (script_pos_ < script_.size())
                a.key = std::string(1, script_[script_pos_++]);
            break;
        default: break;
        }
        return a;
    }
    if (!script_.empty())
        return a; // scripted run: ignore the real keyboard
    using namespace wi::input;
    a.up = Press(KEYBOARD_BUTTON_UP) || Press(GAMEPAD_BUTTON_UP);
    a.down = Press(KEYBOARD_BUTTON_DOWN) || Press(GAMEPAD_BUTTON_DOWN);
    a.left = Press(KEYBOARD_BUTTON_LEFT) || Press(GAMEPAD_BUTTON_LEFT);
    a.right = Press(KEYBOARD_BUTTON_RIGHT) || Press(GAMEPAD_BUTTON_RIGHT);
    a.confirm = Press(KEYBOARD_BUTTON_ENTER) || Press(GAMEPAD_BUTTON_XBOX_A);
    a.back = Press(KEYBOARD_BUTTON_ESCAPE) || Press(GAMEPAD_BUTTON_XBOX_B);
    a.settings = Press(static_cast<BUTTON>('O')) || Press(GAMEPAD_BUTTON_XBOX_Y);
    a.language = Press(static_cast<BUTTON>('L'));
    a.customize = Press(static_cast<BUTTON>('C')) || Press(GAMEPAD_BUTTON_XBOX_X);
    if (listening_)
        a.key = PressedKeyName();
    return a;
}

// Garage preview (D-055): the selected car with the chosen parts and paint on a slow turntable,
// in the race's toon look. Reloaded only when the setup changes; cleared when the screen closes.
void MenuPath::UpdatePreview(float dt)
{
    using namespace wi::scene;
    if (!look_set_)
    {
        look_set_ = true;
        setOutlineEnabled(true);
        setOutlineThickness(2.2f); // the race's grade (D-069)
        setContrast(1.08f);
        setSaturation(1.25f);
        setBrightness(0.02f);
        setOutlineThreshold(0.1f);
        setOutlineCrease(0.8f);
        setOutlineColor(XMFLOAT4(0.05f, 0.05f, 0.07f, 1.0f));
        setBloomEnabled(true);
        setBloomThreshold(1.3f);
        setTonemap(wi::renderer::Tonemap::ACES);
        setExposure(0.95f);
        setFXAAEnabled(true);
    }
    time_ += dt;
    const std::string car = SelectedCar();
    CarSetup setup;
    if (ctx_.garage)
        if (const auto it = ctx_.garage->garage.find(car); it != ctx_.garage->garage.end())
            setup = it->second;
    const auto cat = ctx_.catalogs.find(car);
    auto parts = cat != ctx_.catalogs.end() ? ResolvedParts(cat->second, setup) : std::map<std::string, std::string>{};
    for (const auto& [k, v] : setup.parts) // performance parts show too (intercooler, D-072)
        if (k.rfind(kPerformancePrefix, 0) == 0)
            parts[k] = v;
    const DriverLook look = ctx_.garage ? ctx_.garage->driver : DriverLook{};
    std::string key = car + "|" + std::to_string(setup.paint) + "|drv" + std::to_string(look.head) + std::to_string(look.skin) +
                      std::to_string(look.hair) + std::to_string(look.jacket);
    for (const auto& [slot, v] : parts)
        key += "|" + slot + "=" + v;
    if (key != preview_key_)
    {
        preview_key_ = key;
        garage_scene_.Clear();
        WeatherComponent& w = garage_scene_.weathers.Create(wi::ecs::CreateEntity());
        w.horizon = XMFLOAT3(0.35f, 0.16f, 0.3f);  // dusk magenta over a deep blue night
        w.zenith = XMFLOAT3(0.02f, 0.03f, 0.1f);
        w.ambient = XMFLOAT3(0.32f, 0.28f, 0.42f); // purple fill: coloured cel shadows
        const wi::ecs::Entity sun = garage_scene_.Entity_CreateLight("key", XMFLOAT3(0, 10, 0), XMFLOAT3(1.0f, 0.9f, 0.78f), 7.0f, 100.0f, LightComponent::DIRECTIONAL);
        if (TransformComponent* t = garage_scene_.transforms.GetComponent(sun))
        {
            t->RotateRollPitchYaw(XMFLOAT3(XMConvertToRadians(50.0f), XMConvertToRadians(-40.0f), 0));
            t->UpdateTransform();
            XMStoreFloat3(&w.sunDirection, XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(0, 1, 0, 0), XMLoadFloat4x4(&t->world))));
            w.sunColor = XMFLOAT3(7.0f, 6.3f, 5.5f);
        }
        if (LightComponent* l = garage_scene_.lights.GetComponent(sun))
            l->SetCastShadow(true);
        // cyan rim light from behind: the anime showroom edge glow
        const wi::ecs::Entity rim = garage_scene_.Entity_CreateLight("rim", XMFLOAT3(0, 10, 0), XMFLOAT3(0.35f, 0.8f, 1.0f), 3.0f, 100.0f, LightComponent::DIRECTIONAL);
        if (TransformComponent* t = garage_scene_.transforms.GetComponent(rim))
        {
            t->RotateRollPitchYaw(XMFLOAT3(XMConvertToRadians(-35.0f), XMConvertToRadians(150.0f), 0));
            t->UpdateTransform();
        }
        // turntable: a dark disc with a lit rim
        const wi::ecs::Entity floor = garage_scene_.Entity_CreatePlane("turntable");
        if (TransformComponent* t = garage_scene_.transforms.GetComponent(floor))
        {
            t->Scale(XMFLOAT3(40.0f, 1, 40.0f)); // edges far outside the frame
            t->UpdateTransform();
        }
        for (size_t i = 0; i < garage_scene_.materials.GetCount(); ++i)
            garage_scene_.materials[i].SetBaseColor(XMFLOAT4(0.03f, 0.028f, 0.045f, 1));
        Scene staging;
        preview_root_ = LoadModel(staging, ctx_.cooked_dir + "/vehicles/" + car + ".wiscene", XMMatrixIdentity(), true);
        ToonifyMaterials(staging);
        KeepCarParts(staging, parts);
        if (!car.empty() && car[0] == 'D')
        {
            XMFLOAT4 paint(1.0f, 0.45f, 0.0f, 1.0f); // the player's signature stock colour, as in the race
            if (setup.paint >= 0)
            {
                const PaintPreset& c = GetPaintPreset(setup.paint);
                paint = XMFLOAT4(c.r, c.g, c.b, 1.0f);
            }
            PaintCar(staging, paint);
        }
        garage_scene_.Merge(staging);
        if (!car.empty() && car[0] == 'D') // the player's driver in the seat (D-076)
            AttachDriver(garage_scene_, preview_root_, ctx_.cooked_dir + "/characters/driver.wiscene", look);
    }
    spin_ += dt * 0.35f;
    if (TransformComponent* t = garage_scene_.transforms.GetComponent(preview_root_))
    {
        t->ClearTransform();
        t->RotateRollPitchYaw(XMFLOAT3(0, spin_, 0));
        t->UpdateTransform();
    }
    // Camera: three-quarter view, low like a showroom photo; aimed left of the car so it sits
    // between the option list and the stats panel.
    garage_cam_.CreatePerspective(float(GetPhysicalWidth()), float(GetPhysicalHeight()), 0.1f, 200.0f, XMConvertToRadians(38.0f));
    // The title screen frames the car large and central-right; menus keep it right of the list.
    garage_cam_.Eye = title_ ? XMFLOAT3(5.6f, 1.4f, -7.4f) : XMFLOAT3(6.6f, 2.2f, -9.4f);
    const XMFLOAT3 at = title_ ? XMFLOAT3(-1.3f, 0.8f, 0.4f) : XMFLOAT3(-3.1f, 0.15f, 0.2f);
    garage_cam_.At = XMFLOAT3(at.x - garage_cam_.Eye.x, at.y - garage_cam_.Eye.y, at.z - garage_cam_.Eye.z);
    garage_cam_.Up = XMFLOAT3(0, 1, 0);
    garage_cam_.SetDirty();
    garage_cam_.UpdateCamera();
}

void MenuPath::Update(float dt)
{
    UpdatePreview(dt);
    RenderPath3D::Update(dt);
    actions_ = ReadActions();
    if (ctx_.audio)
        ctx_.audio->Update(dt); // one-shot pools are polled here while no race runs
    if (title_)
    {
        if (actions_.confirm)
            title_ = false, Ui("ui_confirm");
        else if (actions_.back)
            quit_requested_ = true;
        return;
    }
    if (settings_open_)
    {
        UpdateSettings();
        return;
    }
    if (custom_open_)
    {
        UpdateCustomize();
        return;
    }
    using namespace wi::input;
    if (actions_.customize)
    {
        if (ctx_.catalogs.count(SelectedCar()) && ctx_.garage)
            custom_open_ = true, custom_row_ = 0, Ui("ui_confirm");
        else
            SetMessage(Text().Get("custom.none_for_car")), Ui("ui_back");
        return;
    }
    if (actions_.settings)
    {
        settings_open_ = true;
        settings_row_ = 0;
        Ui("ui_confirm");
        return;
    }
    const int n = int(ctx_.events->events.size());
    if (actions_.down)
        selected_ = (selected_ + 1) % n, Ui("ui_move");
    if (actions_.up)
        selected_ = (selected_ + n - 1) % n, Ui("ui_move");
    if (actions_.right)
        car_ = (car_ + 1) % int(ctx_.cars.size()), Ui("ui_move");
    if (actions_.left)
        car_ = (car_ + int(ctx_.cars.size()) - 1) % int(ctx_.cars.size()), Ui("ui_move");
    if (actions_.language && ctx_.language)
        *ctx_.language = *ctx_.language == "az" ? "en" : "az";
    if (actions_.confirm)
    {
        const EventDefinition& e = SelectedEvent();
        int region = 0, number = 0;
        ParseEventId(e.id, region, number);
        const bool region_open = region <= ctx_.profile->unlocked_region;
        const bool duel_gate = e.mode != EventMode::RivalDuel || DuelUnlocked(*ctx_.profile, region);
        if (e.available && region_open && duel_gate)
            start_requested_ = true, Ui("ui_confirm");
        else
            Ui("ui_back"); // locked: an audible "no"
    }
    if (actions_.back)
        quit_requested_ = true;
}

namespace
{
    // Menu look (D-056): dark glass panels over the 3D showroom, one orange accent, chips.
    const XMFLOAT4 kOrange(1.0f, 0.52f, 0.08f, 1.0f);
    const XMFLOAT4 kPanel(0.03f, 0.03f, 0.08f, 0.78f);
    const XMFLOAT4 kCard(0.07f, 0.07f, 0.13f, 0.85f);
    void Rect(float x, float y, float w, float h, const XMFLOAT4& c, wi::graphics::CommandList cmd)
    {
        wi::image::Params p(x, y, w, h);
        p.color = c;
        p.blendFlag = wi::enums::BLENDMODE_ALPHA;
        wi::image::Draw(nullptr, p, cmd);
    }
    void Label(const std::string& s, float x, float y, float size, wi::Color c, wi::graphics::CommandList cmd, bool right = false)
    {
        wi::font::Params p;
        p.posX = x;
        p.posY = y;
        p.size = int(size);
        p.color = c;
        p.shadowColor = wi::Color(0, 0, 0, 170);
        if (right)
            p.h_align = wi::font::WIFALIGN_RIGHT;
        wi::font::Draw(s, p, cmd);
    }
    // A key chip: "[Enter] start"
    float Chip(const std::string& key, const std::string& label, float x, float y, wi::graphics::CommandList cmd)
    {
        const float kw = 12.0f + 9.0f * float(key.size());
        Rect(x, y, kw, 20, XMFLOAT4(0.9f, 0.9f, 0.92f, 0.95f), cmd);
        Label(key, x + 6, y + 2, 15, wi::Color(20, 20, 30, 255), cmd);
        Label(label, x + kw + 6, y + 2, 15, wi::Color(215, 215, 225, 255), cmd);
        return x + kw + 14 + 8.2f * float(label.size());
    }
}

// Car panel (plan 2.14 "qaraj / maşın müqayisəsi"): name, class chip and graphic bars, each scaled
// against the best car of the same group, under the 3D car on the right.
void MenuPath::ComposeGarage(wi::graphics::CommandList cmd) const
{
    const auto it = ctx_.car_defs.find(SelectedCar());
    if (it == ctx_.car_defs.end())
        return;
    // the stats with the garage performance upgrades; the change from stock shows green/red (D-071)
    const VehicleDefinition& stock = it->second;
    CarSetup setup;
    if (ctx_.garage)
        if (const auto g = ctx_.garage->garage.find(SelectedCar()); g != ctx_.garage->garage.end())
            setup = g->second;
    const VehicleDefinition d = ApplyPerformance(stock, setup);
    const Strings& t = Text();
    const float W = GetLogicalWidth(), H = GetLogicalHeight();
    float max_torque = 1, max_top = 1, max_health = 1, max_mass = 1;
    const bool local = !d.id.empty() && d.id[0] != 'D';
    for (const auto& [id, def] : ctx_.car_defs)
    {
        if ((!id.empty() && id[0] != 'D') != local)
            continue;
        max_torque = std::max(max_torque, def.max_engine_torque_nm);
        max_top = std::max(max_top, def.top_speed_target_kmh);
        max_health = std::max(max_health, def.health);
        max_mass = std::max(max_mass, def.mass_kg);
    }
    max_torque *= kMaxTorqueGain; // room for a full build
    max_top *= 1.3f;
    const float x = W * 0.56f, y = H * 0.66f, w = W * 0.4f;
    Rect(x - 14, y - 10, w + 28, H * 0.34f - 40, kPanel, cmd);
    Rect(x - 14, y - 10, 4, H * 0.34f - 40, kOrange, cmd);
    const auto named = ctx_.car_names.find(SelectedCar());
    Label("<  " + (named != ctx_.car_names.end() ? named->second : SelectedCar()) + "  >", x, y, 30, wi::Color::White(), cmd);
    const char* drive = d.drivetrain == Drivetrain::FWD ? "FWD" : d.drivetrain == Drivetrain::RWD ? "RWD" : "AWD";
    const std::string cls = t.Get("garage.class") + " " + std::string(1, d.id.empty() ? '?' : (d.id[0] == 'D' ? 'D' : 'L')) + "  " + drive;
    Rect(x + w - 118, y + 6, 118, 22, kOrange, cmd);
    Label(cls, x + w - 110, y + 8, 15, wi::Color(20, 20, 30, 255), cmd);
    const int pi = PerformanceIndex(d), pi0 = PerformanceIndex(stock);
    Rect(x + w - 200, y + 6, 74, 22, pi > pi0 ? XMFLOAT4(0.25f, 0.85f, 0.4f, 1) : kCard, cmd);
    Label("PI " + std::to_string(pi), x + w - 194, y + 8, 15, pi > pi0 ? wi::Color(10, 30, 15, 255) : wi::Color::White(), cmd);
    auto bar = [&](int k, const std::string& label, float value, float base, float max, const std::string& unit, bool lower_better = false) {
        const float bx = x + float(k % 2) * (w * 0.52f), by = y + 48 + float(k / 2) * 44;
        Label(label, bx, by, 14, wi::Color(185, 185, 200, 255), cmd);
        Label(std::to_string(int(std::round(value))) + " " + unit, bx + w * 0.46f, by, 14, wi::Color::White(), cmd, true);
        Rect(bx, by + 20, w * 0.46f, 7, XMFLOAT4(1, 1, 1, 0.12f), cmd);
        const float fv = std::clamp(value / max, 0.0f, 1.0f), fb = std::clamp(base / max, 0.0f, 1.0f);
        Rect(bx, by + 20, w * 0.46f * fv, 7, XMFLOAT4(0.3f, 0.85f, 1.0f, 1.0f), cmd);
        if (std::fabs(fv - fb) > 0.002f) // the change from stock: green gained, red lost
            Rect(bx + w * 0.46f * std::min(fv, fb), by + 20, w * 0.46f * std::fabs(fv - fb), 7,
                 (fv > fb) != lower_better ? XMFLOAT4(0.3f, 0.95f, 0.45f, 1.0f) : XMFLOAT4(1.0f, 0.35f, 0.3f, 1.0f), cmd);
    };
    bar(0, t.Get("garage.torque"), d.max_engine_torque_nm, stock.max_engine_torque_nm, max_torque, "Nm");
    bar(1, t.Get("garage.top_speed"), d.top_speed_target_kmh, stock.top_speed_target_kmh, max_top, "km/h");
    bar(2, t.Get("garage.health"), d.health, stock.health, max_health, "HP");
    bar(3, t.Get("garage.mass"), d.mass_kg, stock.mass_kg, max_mass, "kg", true);
}

void MenuPath::ComposeControls(wi::graphics::CommandList cmd) const
{
    const Strings& t = Text();
    wi::font::Params p;
    p.posX = 80;
    p.posY = 70;
    p.size = 44;
    p.shadowColor = wi::Color::Black();
    p.color = wi::Color(255, 170, 60, 255);
    wi::font::Draw(t.Get("controls.title"), p, cmd);
    p.size = 24;
    p.posY += 66;
    const auto& actions = ControlActions();
    for (size_t i = 0; i <= actions.size(); ++i)
    {
        const bool sel = int(i) == controls_row_;
        std::string line;
        if (i < actions.size())
        {
            const auto it = ctx_.settings ? ctx_.settings->bindings.find(actions[i]) : decltype(ctx_.settings->bindings.end()){};
            const std::string key = sel && listening_ ? t.Get("controls.press_key") : (ctx_.settings && it != ctx_.settings->bindings.end() ? it->second : std::string("?"));
            line = t.Get("controls." + actions[i]) + ":  " + key;
        }
        else
            line = t.Get("settings.back");
        p.color = sel ? (listening_ ? wi::Color(120, 220, 255, 255) : wi::Color(255, 220, 120, 255)) : wi::Color::White();
        wi::font::Draw((sel ? "> " : "  ") + line, p, cmd);
        p.posY += 33;
    }
    p.posY = float(GetLogicalHeight()) - 60;
    p.size = 20;
    p.color = wi::Color(200, 200, 200, 255);
    wi::font::Draw(t.Get("controls.hint"), p, cmd);
}

void MenuPath::ComposeSettings(wi::graphics::CommandList cmd) const
{
    Rect(0, 0, GetLogicalWidth(), GetLogicalHeight(), XMFLOAT4(0.02f, 0.02f, 0.06f, 0.82f), cmd);
    Rect(60, 118, 360, 4, kOrange, cmd);
    if (controls_open_)
    {
        ComposeControls(cmd);
        return;
    }
    const Strings& t = Text();
    wi::font::Params p;
    p.posX = 80;
    p.posY = 70;
    p.size = 44;
    p.shadowColor = wi::Color::Black();
    p.color = wi::Color(255, 170, 60, 255);
    wi::font::Draw(t.Get("settings.title"), p, cmd);
    p.size = 24;
    p.posY += 70;
    auto bar = [](float v) { const int k = int(std::round(v * 10.0f)); return std::string(size_t(k), '#') + std::string(size_t(10 - k), '-') + "  " + std::to_string(k * 10) + "%"; };
    const racer::Settings* s = ctx_.settings;
    const std::string rows[kRows] = {
        t.Get("settings.master") + ":  " + (s ? bar(s->master_volume) : std::string()),
        t.Get("settings.music") + ":  " + (s ? bar(s->music_volume) : std::string()),
        t.Get("settings.sfx") + ":  " + (s ? bar(s->sfx_volume) : std::string()),
        t.Get("settings.language") + ":  < " + (ctx_.language && *ctx_.language == "en" ? "English" : "Az\xC9\x99rbaycan" /* UTF-8, independent of the compiler code page */) + " >",
        t.Get("settings.ui_scale") + ":  < " + (s ? std::to_string(int(std::round(s->ui_scale * 100))) : std::string()) + "% >",
        t.Get("settings.high_contrast") + ":  < " + (s && s->high_contrast ? t.Get("settings.on") : t.Get("settings.off")) + " >",
        t.Get("settings.motion_blur") + ":  < " + (s && s->motion_blur ? t.Get("settings.on") : t.Get("settings.off")) + " >",
        t.Get("settings.reduce_shake") + ":  < " + (s && s->reduce_shake ? t.Get("settings.on") : t.Get("settings.off")) + " >",
        t.Get("settings.deadzone") + ":  < " + (s ? std::to_string(int(std::round(s->stick_deadzone * 100))) : std::string()) + "% >",
        t.Get("settings.controls") + "  >",
        t.Get("settings.back") };
    for (int i = 0; i < kRows; ++i)
    {
        p.color = i == settings_row_ ? wi::Color(255, 220, 120, 255) : wi::Color::White();
        wi::font::Draw((i == settings_row_ ? "> " : "  ") + rows[i], p, cmd);
        p.posY += 38;
    }
    p.posY = float(GetLogicalHeight()) - 60;
    p.size = 20;
    p.color = wi::Color(200, 200, 200, 255);
    wi::font::Draw(t.Get("settings.hint"), p, cmd);
}

// Customisation (plan 2.13, D-055): paint and every part slot of the selected car; Left/Right
// cycles, each change is written to the profile at once (the game saves it).
void MenuPath::UpdateCustomize()
{
    const CarCustomization& cat = ctx_.catalogs.at(SelectedCar());
    const int nperf = int(PerformanceSlots().size());
    constexpr int kDrv = 4; // driver: face, skin, hair, jacket (D-076)
    const int rows = 1 + kDrv + int(cat.slots.size()) + nperf + 1; // paint, driver, visual slots, performance, back
    if (actions_.down)
        custom_row_ = (custom_row_ + 1) % rows, Ui("ui_move");
    if (actions_.up)
        custom_row_ = (custom_row_ + rows - 1) % rows, Ui("ui_move");
    const int dir = actions_.right ? 1 : actions_.left ? -1 : 0;
    if (dir != 0 && custom_row_ < rows - 1)
    {
        CarSetup& setup = ctx_.garage->garage[SelectedCar()];
        const int vis = int(cat.slots.size());
        if (custom_row_ == 0)
            CyclePaint(setup, dir);
        else if (custom_row_ <= kDrv)
            CycleDriverLook(ctx_.garage->driver, DriverField(custom_row_ - 1), dir);
        else if (custom_row_ <= kDrv + vis)
            CycleVariant(cat, setup, cat.slots[size_t(custom_row_ - 1 - kDrv)].id, dir);
        else
            CyclePerformance(setup, PerformanceSlots()[size_t(custom_row_ - 1 - kDrv - vis)].id, dir);
        garage_changed_ = true;
        Ui("ui_move");
    }
    if (actions_.back || (actions_.confirm && custom_row_ == rows - 1))
        custom_open_ = false, Ui("ui_back");
}

void MenuPath::ComposeCustomize(wi::graphics::CommandList cmd) const
{
    const Strings& t = Text();
    const float W = GetLogicalWidth(), H = GetLogicalHeight();
    const CarCustomization& cat = ctx_.catalogs.at(SelectedCar());
    CarSetup setup;
    if (ctx_.garage)
        if (const auto it = ctx_.garage->garage.find(SelectedCar()); it != ctx_.garage->garage.end())
            setup = it->second;
    Rect(0, 0, W * 0.47f, H, kPanel, cmd);
    Label(t.Get("custom.title"), 40, 34, 38, wi::Color(255, 150, 40, 255), cmd);
    Rect(40, 80, 220, 4, kOrange, cmd);
    // compact rows: the visual slots and 10 performance slots fit the panel (D-071)
    const float x = 40, cw = W * 0.4f, ch = 24;
    // the list scrolls so the selected row stays between the title and the hint bar
    const float top = 96, bottom = H - 34 - 8, step = ch + 4;
    constexpr int kDrv = 4;
    const int vis_rows = 1 + kDrv + int(cat.slots.size());
    const float sel_y = top + step * float(custom_row_) + (custom_row_ >= vis_rows ? 24.0f : 0.0f);
    const float scroll = std::max(0.0f, sel_y + step * 2.0f - bottom);
    float y = top - scroll;
    auto row = [&](int i, const std::string& label, const std::string& value, bool stock) {
        if (y < top - 4 || y + ch > bottom)
        {
            y += step;
            return;
        }
        const bool sel = i == custom_row_;
        Rect(x, y, cw, ch, sel ? kOrange : kCard, cmd);
        if (!sel)
            Rect(x, y, 3, ch, XMFLOAT4(1, 1, 1, 0.15f), cmd);
        const wi::Color fg = sel ? wi::Color(20, 20, 30, 255) : wi::Color::White();
        Label(label, x + 12, y + 4, 15, fg, cmd);
        Label("<  " + value + "  >", x + cw - 12, y + 4, 15, fg, cmd, true);
        if (stock)
            Label(t.Get("custom.stock_mark"), x + cw * 0.46f, y + 6, 12, sel ? wi::Color(60, 40, 20, 255) : wi::Color(140, 140, 160, 255), cmd);
        y += ch + 4;
    };
    row(0, t.Get("custom.paint"), setup.paint < 0 ? t.Get("paint.stock") : t.Get(GetPaintPreset(setup.paint).key), setup.paint < 0);
    if (setup.paint >= 0)
    {
        const PaintPreset& c = GetPaintPreset(setup.paint);
        if (scroll <= 0.0f)
            Rect(x + cw * 0.46f, top + 5, 34, ch - 10, XMFLOAT4(c.r, c.g, c.b, 1), cmd);
    }
    // the driver character (D-076)
    const DriverLook look = ctx_.garage ? ctx_.garage->driver : DriverLook{};
    const float swatch_y[3] = { y + (ch + 4) * 1, y + (ch + 4) * 2, y + (ch + 4) * 3 };
    row(1, t.Get("driver.face"), t.Get("driver.face" + std::to_string(look.head)), look.head == 0);
    row(2, t.Get("driver.skin"), std::to_string(look.skin + 1) + " / " + std::to_string(kSkinTones), look.skin == 0);
    row(3, t.Get("driver.hair"), std::to_string(look.hair + 1) + " / " + std::to_string(kHairColours), look.hair == 0);
    row(4, t.Get("driver.jacket"), std::to_string(look.jacket + 1) + " / " + std::to_string(kJacketColours), look.jacket == 0);
    const DriverColour sw[3] = { SkinTone(look.skin), HairColour(look.hair), JacketColour(look.jacket) };
    for (int k = 0; k < 3; ++k)
        if (swatch_y[k] >= top && swatch_y[k] + ch <= bottom)
            Rect(x + cw * 0.62f, swatch_y[k] + 5, 28, ch - 10, XMFLOAT4(sw[k].r, sw[k].g, sw[k].b, 1), cmd);
    for (size_t i = 0; i < cat.slots.size(); ++i)
    {
        const std::string v = ChosenVariant(cat, setup, cat.slots[i].id);
        row(int(i) + 1 + kDrv, t.Get("slot." + cat.slots[i].id), t.Get("variant." + v), v == cat.slots[i].stock);
    }
    // performance section
    if (y >= top - 4 && y + 20 < bottom)
        Label(t.Get("custom.performance"), x, y + 2, 16, wi::Color(255, 150, 40, 255), cmd);
    y += 24;
    for (size_t i = 0; i < PerformanceSlots().size(); ++i)
    {
        const PerformanceSlot& ps = PerformanceSlots()[i];
        const std::string v = ChosenPerformance(setup, ps.id);
        row(int(cat.slots.size() + 1 + kDrv + i), t.Get("perfslot." + ps.id), t.Get("perf." + ps.id + "." + v), v == ps.levels.front().id);
    }
    const bool back_sel = custom_row_ == int(cat.slots.size() + PerformanceSlots().size()) + 1 + kDrv;
    if (y + ch <= bottom)
    {
        Rect(x, y, cw * 0.4f, ch, back_sel ? kOrange : kCard, cmd);
        Label(t.Get("custom.back"), x + 14, y + 4, 15, back_sel ? wi::Color(20, 20, 30, 255) : wi::Color::White(), cmd);
    }
    ComposeGarage(cmd);
    Rect(0, H - 34, W, 34, XMFLOAT4(0, 0, 0, 0.8f), cmd);
    Label(t.Get("custom.hint"), 40, H - 26, 15, wi::Color(210, 210, 220, 255), cmd);
}

void MenuPath::ComposeTitle(wi::graphics::CommandList cmd) const
{
    const Strings& t = Text();
    const float W = GetLogicalWidth(), H = GetLogicalHeight();
    Rect(0, 0, W * 0.42f, H, XMFLOAT4(0.02f, 0.02f, 0.07f, 0.55f), cmd);
    Rect(0, H * 0.7f, W, H * 0.3f, XMFLOAT4(0, 0, 0, 0.45f), cmd);
    const float x = W * 0.06f;
    Label(t.Get("title.name1"), x, H * 0.14f, 96, wi::Color::White(), cmd);
    Label(t.Get("title.name2"), x + 40, H * 0.14f + 88, 96, wi::Color(255, 140, 30, 255), cmd);
    Rect(x, H * 0.14f + 200, W * 0.3f, 8, kOrange, cmd);
    Rect(x, H * 0.14f + 214, W * 0.17f, 3, XMFLOAT4(1, 1, 1, 0.7f), cmd);
    Label(t.Get("title.subtitle"), x, H * 0.14f + 228, 22, wi::Color(200, 200, 215, 255), cmd);
    const float pulse = 0.55f + 0.45f * std::sin(time_ * 3.0f);
    Label(t.Get("title.press"), x, H * 0.8f, 28, wi::Color(255, 255, 255, uint8_t(255 * pulse)), cmd);
    Label(t.Get("title.version"), W - 30, H - 34, 15, wi::Color(160, 160, 180, 255), cmd, true);
}

void MenuPath::Compose(wi::graphics::CommandList cmd) const
{
    RenderPath3D::Compose(cmd);
    if (title_)
    {
        ComposeTitle(cmd);
        return;
    }
    if (settings_open_)
    {
        ComposeSettings(cmd);
        return;
    }
    if (custom_open_)
    {
        ComposeCustomize(cmd);
        return;
    }
    const Strings& t = Text();
    const float W = GetLogicalWidth(), H = GetLogicalHeight();
    Rect(0, 0, W * 0.5f, H, kPanel, cmd);
    Label(t.Get("title.name1") + " " + t.Get("title.name2"), 40, 16, 15, wi::Color(255, 150, 40, 255), cmd);
    Label(t.Get("menu.title"), 40, 34, 36, wi::Color::White(), cmd);
    Rect(40, 78, 200, 4, kOrange, cmd);

    // progress chips: medals, reputation, mastery (plan 2.12 duel gate)
    int region = 1, dummy = 0;
    if (!ctx_.events->events.empty())
        ParseEventId(ctx_.events->events[0].id, region, dummy);
    float cx = 40;
    auto chip = [&](const std::string& s) {
        const float w = 16 + 8.0f * float(s.size());
        Rect(cx, 90, w, 22, XMFLOAT4(1, 1, 1, 0.1f), cmd);
        Label(s, cx + 8, 93, 14, wi::Color(220, 220, 235, 255), cmd);
        cx += w + 8;
    };
    chip(std::to_string(RegionMedals(*ctx_.profile, region)) + "/35 " + t.Get("menu.medals"));
    chip("rep " + std::to_string(ctx_.profile->reputation));
    for (const MasteryTask& m : ctx_.events->mastery)
    {
        std::string v;
        if (ctx_.profile->mastery_done.count(m.id))
            v = "OK";
        else
        {
            const auto it = ctx_.profile->mastery_progress.find(m.id);
            v = std::to_string(int(it == ctx_.profile->mastery_progress.end() ? 0.0f : it->second)) + "/" + std::to_string(int(m.target));
        }
        chip(m.id.substr(4) + " " + v);
    }

    // event cards
    const float x = 40, cw = W * 0.44f, ch = 40;
    float y = 122;
    const std::string modes[] = { "mode.CombatRace", "mode.TimeTrial", "mode.CheckpointRun", "mode.Destruction", "mode.RivalDuel" };
    for (size_t i = 0; i < ctx_.events->events.size(); ++i)
    {
        const EventDefinition& e = ctx_.events->events[i];
        const auto rec = ctx_.profile->events.find(e.id);
        const int medals = rec == ctx_.profile->events.end() ? 0 : rec->second.Medals();
        const bool sel = int(i) == selected_;
        int er = 0, en = 0;
        ParseEventId(e.id, er, en);
        const bool locked = !e.available || (e.mode == EventMode::RivalDuel && !DuelUnlocked(*ctx_.profile, er));
        Rect(x, y, cw, ch, sel ? kOrange : kCard, cmd);
        Rect(x, y, 46, ch, sel ? XMFLOAT4(0.75f, 0.35f, 0.02f, 1) : XMFLOAT4(0.13f, 0.13f, 0.22f, 0.95f), cmd);
        const wi::Color fg = sel ? wi::Color(20, 20, 30, 255) : locked ? wi::Color(120, 120, 135, 255) : wi::Color::White();
        Label(e.id.substr(4), x + 8, y + 12, 15, sel ? wi::Color::White() : wi::Color(255, 170, 70, 255), cmd);
        Label(t.Get(e.name_key), x + 56, y + 4, 17, fg, cmd);
        Label(t.Get(modes[int(e.mode)]) + "  -  " + std::to_string(e.participants) + " " + t.Get("menu.racers") + ", " + std::to_string(e.laps) + " " +
                 t.Get("menu.laps"),
             x + 56, y + 23, 12, sel ? wi::Color(60, 35, 10, 255) : wi::Color(150, 150, 170, 255), cmd);
        for (int m = 0; m < 5; ++m) // medal pips
            Rect(x + cw - 76 + float(m) * 14, y + 15, 10, 10,
                 m < medals ? XMFLOAT4(1.0f, 0.82f, 0.2f, 1) : XMFLOAT4(sel ? 0.3f : 1.0f, sel ? 0.15f : 1.0f, sel ? 0.05f : 1.0f, 0.25f), cmd);
        y += ch + 5;
    }
    // detail box: objectives, or why the event is locked
    {
        const EventDefinition& e = SelectedEvent();
        int er = 0, en = 0;
        ParseEventId(e.id, er, en);
        const bool duel_locked = e.mode == EventMode::RivalDuel && !DuelUnlocked(*ctx_.profile, er);
        Rect(x, y + 4, cw, 30, XMFLOAT4(0, 0, 0, 0.45f), cmd);
        std::string line;
        wi::Color c(170, 220, 255, 255);
        if (!e.available || duel_locked)
        {
            line = t.Get(e.unavailable_reason_key);
            if (e.available && duel_locked)
            {
                int mastery = 0;
                for (int m = 1; m <= 3; ++m)
                    mastery += int(ctx_.profile->mastery_done.count("R0" + std::to_string(er) + "_M" + std::to_string(m)));
                line = t.Get("menu.duel_locked") + "  " + std::to_string(RegionMedals(*ctx_.profile, er)) + "/14 " + t.Get("menu.medals") + ",  " +
                       std::to_string(mastery) + "/2 " + t.Get("menu.mastery");
            }
            c = wi::Color(255, 120, 120, 255);
        }
        else
            line = "+ " + t.Get(e.objectives[0].text_key) + "     + " + t.Get(e.objectives[1].text_key);
        Label(line, x + 10, y + 11, 14, c, cmd);
    }

    ComposeGarage(cmd);
    if (!message_.empty())
    {
        Rect(W * 0.56f - 14, H * 0.66f - 44, W * 0.4f + 28, 28, XMFLOAT4(0.05f, 0.25f, 0.1f, 0.85f), cmd);
        Label(message_, W * 0.56f, H * 0.66f - 38, 14, wi::Color(150, 255, 170, 255), cmd);
    }
    Rect(0, H - 34, W, 34, XMFLOAT4(0, 0, 0, 0.8f), cmd);
    Label(t.Get("menu.start"), 40, H - 26, 15, wi::Color(210, 210, 220, 255), cmd);
}
