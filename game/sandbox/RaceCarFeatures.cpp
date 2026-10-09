// Car features the player drives with (D-060..D-063, owner requests 2026-09-30): camera modes, look
// back, a turning steering wheel with the driver's hands on it, gauges, stalks, pedals, the gear
// lever and a real gear-change hand movement, handbrake, air freshener, lamps, horn, wipers, radio,
// the driver's torso sway and head turn, and the taunt out of the window.
#include "RacePath.h"
#include "KeyNames.h"

#include <algorithm>
#include <cmath>

using namespace wi::scene;
using wi::ecs::Entity;
using wi::ecs::INVALID_ENTITY;
using namespace racer;
using namespace racer::runtime;

namespace
{
    const char* kStations[] = { "", "harbor", "nightdrive", "rush" };
    const char* kStationKeys[] = { "radio.off", "radio.harbor", "radio.nightdrive", "radio.rush" };
    const char* kCameraKeys[] = { "camera.near", "camera.far", "camera.hood", "camera.cockpit" };

    TransformComponent* T(Scene& s, const RacePath::Part& p) { return p.ok() ? s.transforms.GetComponent(p.e) : nullptr; }
    // Rotation about the part's own local Y (the Blender pivot's Z: the axis it was authored to turn about).
    void TurnLocalY(Scene& s, const RacePath::Part& p, float angle)
    {
        if (TransformComponent* t = T(s, p))
        {
            XMStoreFloat4(&t->rotation_local, XMQuaternionMultiply(XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), angle), XMLoadFloat4(&p.rot)));
            t->SetDirty();
        }
    }
    // Rotation about an axis of the car body (X right, Y up, Z forward) after the authored pose.
    void TurnBody(Scene& s, const RacePath::Part& p, XMVECTOR q)
    {
        if (TransformComponent* t = T(s, p))
        {
            XMStoreFloat4(&t->rotation_local, XMQuaternionMultiply(XMLoadFloat4(&p.rot), q));
            t->SetDirty();
        }
    }
    void Show(Scene& s, const RacePath::Part& p, bool on)
    {
        if (TransformComponent* t = T(s, p))
        {
            const float k = on ? 1.0f : 0.0f;
            if (t->scale_local.x != k)
            {
                t->scale_local = XMFLOAT3(k, k, k);
                t->SetDirty();
            }
        }
    }
    void Place(Scene& s, const RacePath::Part& p, XMVECTOR pos, XMVECTOR rot)
    {
        if (TransformComponent* t = T(s, p))
        {
            XMStoreFloat3(&t->translation_local, pos);
            XMStoreFloat4(&t->rotation_local, rot);
            t->SetDirty();
        }
    }
    XMVECTOR Aim(XMVECTOR dir) // shortest rotation taking +Y onto dir
    {
        dir = XMVector3Normalize(dir);
        const XMVECTOR y = XMVectorSet(0, 1, 0, 0);
        const XMVECTOR axis = XMVector3Cross(y, dir);
        const float len = XMVectorGetX(XMVector3Length(axis));
        const float c = std::clamp(XMVectorGetX(XMVector3Dot(y, dir)), -1.0f, 1.0f);
        return len < 1e-5f ? (c > 0 ? XMQuaternionIdentity() : XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), XM_PI))
                           : XMQuaternionRotationAxis(axis / len, std::acos(c));
    }
    XMVECTOR ArcFromZ(XMVECTOR dir)
    {
        dir = XMVector3Normalize(dir);
        const XMVECTOR z = XMVectorSet(0, 0, 1, 0);
        const XMVECTOR axis = XMVector3Cross(z, dir);
        const float len = XMVectorGetX(XMVector3Length(axis));
        const float c = std::clamp(XMVectorGetX(XMVector3Dot(z, dir)), -1.0f, 1.0f);
        return len < 1e-5f ? (c > 0 ? XMQuaternionIdentity() : XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), XM_PI))
                           : XMQuaternionRotationAxis(axis / len, std::acos(c));
    }
    // Controls the driver reaches for (D-064): which hand, and the touch point in body space.
    enum Reach { kNone, kStalkL, kStalkR, kLights, kRadio, kWeapon, kHorn, kHazard, kDome, kVisor };
    float Smooth(float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
    void Spring(float& x, float& v, float target, float hz, float z, float dt)
    {
        const float w = XM_2PI * hz;
        v += (w * w * (target - x) - 2.0f * z * w * v) * dt;
        x += v * dt;
    }
    // H-pattern: gate -1/0/+1 across (1-2 / 3-4 / 5-6, reverse right-back), +1 forward / -1 back.
    void GatePos(int gear, float& across, float& fwd)
    {
        if (gear <= 0)
        {
            across = gear < 0 ? 1.0f : 0.0f;
            fwd = gear < 0 ? -1.0f : 0.0f;
            return;
        }
        across = gear <= 2 ? -1.0f : gear <= 4 ? 0.0f : 1.0f;
        fwd = (gear % 2) ? 1.0f : -1.0f;
    }
}

void RacePath::BuildRig(size_t car, Entity root, const std::vector<std::pair<Entity, std::string>>& materials, float origin_height)
{
    Scene& s = *scene;
    if (rigs_.size() <= car)
        rigs_.resize(car + 1);
    CarRig& r = rigs_[car];
    auto part = [&](const std::string& n) {
        Part p;
        p.e = s.Entity_FindByName(n, root);
        if (const TransformComponent* t = p.ok() ? s.transforms.GetComponent(p.e) : nullptr)
            p.pos = t->translation_local, p.rot = t->rotation_local;
        return p;
    };
    r.steer = part("STEER_PIVOT");
    r.needle_speed = part("NEEDLE_SPEED");
    r.needle_rev = part("NEEDLE_REV");
    r.needle_fuel = part("NEEDLE_FUEL");
    r.needle_temp = part("NEEDLE_TEMP");
    r.wiper_l = part("WIPER_L");
    r.wiper_r = part("WIPER_R");
    r.stalk_l = part("STALK_L");
    r.stalk_r = part("STALK_R");
    r.pedal_gas = part("PEDAL_GAS");
    r.pedal_brake = part("PEDAL_BRAKE");
    r.pedal_clutch = part("PEDAL_CLUTCH");
    r.handbrake = part("HANDBRAKE_LEVER");
    r.lever = part("GEAR_LEVER");
    r.freshener = part("FRESHENER");
    r.torso = part("DRIVER_TORSO");
    r.head = part("DRIVER_HEAD"); // the face AttachDriver kept (D-076)
    if (int(car) == player_ && r.head.ok())
    {
        // The player's own head sits on its own render layer: the cockpit camera leaves it out (it
        // would fill the view), the mirror cameras keep it, so the driver shows in the rear-view mirror.
        for (size_t i = 0; i < s.hierarchy.GetCount(); ++i)
        {
            const Entity e = s.hierarchy.GetEntity(i);
            for (Entity p = e; p != INVALID_ENTITY;)
            {
                if (p == r.head.e)
                {
                    if (s.objects.Contains(e))
                        (s.layers.Contains(e) ? *s.layers.GetComponent(e) : s.layers.Create(e)).layerMask = kPlayerHeadLayer;
                    break;
                }
                const HierarchyComponent* h = s.hierarchy.GetComponent(p);
                p = h ? h->parentID : INVALID_ENTITY;
            }
        }
    }
    r.shift_hand = part("SHIFT_HAND");
    r.window = part("DRIVER_WINDOW");
    r.window_drop = part("WINDOW_DROP");
    r.light_knob = part("LIGHT_KNOB");
    r.radio_button = part("RADIO_BUTTON");
    r.weapon_button = part("WEAPON_BUTTON");
    r.weapon_cover = part("WEAPON_COVER");
    r.point_hand[0] = part("POINT_HAND_L");
    r.point_hand[1] = part("POINT_HAND_R");
    Show(s, r.point_hand[0], false);
    Show(s, r.point_hand[1], false);
    r.taunt_hand = part("TAUNT_HAND");
    Show(s, r.shift_hand, false);
    Show(s, r.taunt_hand, false);
    if (const Part knob = part("GEAR_KNOB"); knob.ok())
        r.knob_local = knob.pos; // in the lever pivot's frame
    const char* side[2] = { "L", "R" };
    r.has_arms = r.steer.ok() && r.torso.ok();
    for (int k = 0; k < 2; ++k)
    {
        r.upper[k] = part(std::string("UPPER_ARM_") + side[k]);
        r.fore[k] = part(std::string("FOREARM_") + side[k]);
        r.hand[k] = part(std::string("HAND_") + side[k]);
        const Part grip = part(std::string("GRIP_") + side[k]);
        r.grip_local[k] = grip.pos; // in the steering pivot's frame
        r.has_arms = r.has_arms && r.upper[k].ok() && r.fore[k].ok() && grip.ok();
    }
    r.warn_ind[0] = part("WARN_IND_L");
    r.warn_ind[1] = part("WARN_IND_R");
    r.warn_handbrake = part("WARN_HANDBRAKE");
    r.warn_engine = part("WARN_ENGINE");
    r.warn_beam = part("WARN_BEAM");
    r.warn_hazard = part("WARN_HAZARD");
    r.hazard_button = part("HAZARD_BUTTON");
    r.dome_lit = part("DOME_LIT");
    r.dome_switch = part("DOME_SWITCH");
    r.visor = part("SUN_VISOR_L");
    for (int k = 0; k < 10; ++k)
        r.shift_led[k] = part("SHIFT_LED_" + std::to_string(k));
    if (int(car) == player_ && r.dome_lit.ok()) // the cabin lamp, off until switched on (D-079)
    {
        const Entity body = s.Entity_FindByName("BODY", root);
        r.dome_light = s.Entity_CreateLight("dome_light", r.dome_lit.pos, XMFLOAT3(1.0f, 0.85f, 0.6f), 0.0f, 1.6f, LightComponent::POINT);
        if (body != INVALID_ENTITY)
            s.Component_Attach(r.dome_light, body, true);
    }
    if (const Part g = part("HANDBRAKE_GRIP"); g.ok())
        r.hb_grip_local = g.pos, r.has_hb_grip = true;
    // legs: thigh / shin bones onto the pedals' foot targets (D-076)
    r.has_legs = true;
    for (int k = 0; k < 2; ++k)
    {
        r.thigh[k] = part(std::string("THIGH_") + side[k]);
        r.shin[k] = part(std::string("SHIN_") + side[k]);
        r.foot[k] = part(std::string("FOOT_") + side[k]);
        const Part target = part(std::string("FOOT_TARGET_") + side[k]);
        r.foot_target_local[k] = target.pos; // in the pedal pivot's frame
        r.has_legs = r.has_legs && r.thigh[k].ok() && r.shin[k].ok() && r.foot[k].ok() && target.ok();
        if (std::getenv("RACER_DRIVER_DEBUG"))
            wi::backlog::post(std::string("[drvdbg] legs ") + side[k] + " thigh " + std::to_string(r.thigh[k].ok()) + " shin " + std::to_string(r.shin[k].ok()) +
                              " foot " + std::to_string(r.foot[k].ok()) + " target " + std::to_string(target.ok()));
    }
    static const char* kMirrorNames[3] = { "MIRROR_L", "MIRROR_C", "MIRROR_R" };
    for (int m = 0; m < 3; ++m)
        if (const Part mk = part(kMirrorNames[m]); mk.ok())
            r.mirror_local[m] = XMFLOAT3(mk.pos.x, mk.pos.y - origin_height, mk.pos.z), r.has_mirror[m] = true;
    if (const Part eye = part("DRIVER_EYE"); eye.ok())
    {
        // DRIVER_EYE is a child of BODY, BODY sits at the root's origin; the root hangs
        // origin_height below the body origin.
        r.eye_local = XMFLOAT3(eye.pos.x, eye.pos.y - origin_height, eye.pos.z);
        r.has_eye = true;
    }
    for (const auto& [e, name] : materials)
    {
        const MaterialComponent* m = s.materials.GetComponent(e);
        if (!m)
            continue;
        const float strength = m->GetEmissiveStrength();
        if (name.find("toon_tail") != std::string::npos && name.find("dim") == std::string::npos)
            r.tail.push_back({ e, strength });
        else if (name.find("toon_ind_left") != std::string::npos)
            r.ind_l.push_back({ e, strength });
        else if (name.find("toon_ind_right") != std::string::npos)
            r.ind_r.push_back({ e, strength });
        else if (name.find("toon_lamp") != std::string::npos)
            r.lamps.push_back({ e, strength });
        else if (name.find("mirror_view_") != std::string::npos)
            r.mirror_mats.push_back({ e, name.find("_L") != std::string::npos ? 0 : name.find("_C") != std::string::npos ? 1 : 2 });
    }
}

// Two-bone IK in the car body's frame: shoulder -> elbow -> wrist, the elbow bending down and out.
// Bones are authored along their local +Y.
void RacePath::SolveArm(CarRig& r, int k, const XMVECTOR& S, const XMVECTOR& target, float hide)
{
    const float side = k == 0 ? -1.0f : 1.0f;
    SolveLimb(r.upper[k], r.fore[k], S, target, 0.29f, 0.28f, XMVectorSet(side * 0.35f, -0.45f, 0.05f, 0), hide);
}

// Two-bone IK: a -> joint -> end, the joint bending towards `pole`. Returns the end point reached
// (short of `target` when it is out of reach).
XMVECTOR RacePath::SolveLimb(const Part& a, const Part& b, const XMVECTOR& S, const XMVECTOR& target, float l1, float l2, const XMVECTOR& pole, float hide)
{
    Scene& s = *scene;
    TransformComponent* up = T(s, a);
    TransformComponent* fo = T(s, b);
    const XMVECTOR to = target - S;
    const float dist = std::clamp(XMVectorGetX(XMVector3Length(to)), 0.08f, l1 + l2 - 0.002f);
    const XMVECTOR dir = XMVector3Normalize(to);
    if (!up || !fo)
        return S + dir * dist;
    const XMVECTOR n = XMVector3Normalize(pole - dir * XMVectorGetX(XMVector3Dot(pole, dir)));
    const float cos_a = std::clamp((l1 * l1 + dist * dist - l2 * l2) / (2.0f * l1 * dist), -1.0f, 1.0f);
    const float sin_a = std::sqrt(1.0f - cos_a * cos_a);
    const XMVECTOR E = S + dir * (l1 * cos_a) + n * (l1 * sin_a);
    XMStoreFloat3(&up->translation_local, S);
    XMStoreFloat4(&up->rotation_local, Aim(E - S));
    XMStoreFloat3(&fo->translation_local, E);
    XMStoreFloat4(&fo->rotation_local, Aim(S + dir * dist - E));
    up->scale_local = fo->scale_local = XMFLOAT3(hide, hide, hide);
    up->SetDirty();
    fo->SetDirty();
    return S + dir * dist;
}

void RacePath::ReadCarFeatureInput()
{
    using namespace wi::input;
    auto pressed = [&](const char* action) {
        const auto it = options_.bindings.find(action);
        const BUTTON b = it != options_.bindings.end() ? KeyButton(it->second) : BUTTON_NONE;
        return b != BUTTON_NONE && Press(b);
    };
    auto held = [&](const char* action) {
        const auto it = options_.bindings.find(action);
        const BUTTON b = it != options_.bindings.end() ? KeyButton(it->second) : BUTTON_NONE;
        return b != BUTTON_NONE && Down(b);
    };
    const Strings* text = options_.text;
    auto toast = [&](const char* key) {
        toast_ = text ? text->Get(key) : std::string(key);
        toast_t_ = 1.6f;
    };
    look_back_ = held("look_back") || Down(GAMEPAD_BUTTON_XBOX_L1);
    if (pressed("camera") || Press(GAMEPAD_BUTTON_XBOX_BACK))
    {
        cam_mode_ = CamMode((int(cam_mode_) + 1) % 4);
        toast(kCameraKeys[int(cam_mode_)]);
    }
    if (pressed("horn"))
    {
        if (options_.audio)
            options_.audio->PlaySfx("horn", 0.7f);
        pending_reach_ = kHorn;
    }
    if (pressed("lights"))
        lights_on_ = !lights_on_, pending_reach_ = kLights;
    if (pressed("hazard"))
        hazard_on_ = !hazard_on_, pending_reach_ = kHazard, toast(hazard_on_ ? "cabin.hazard_on" : "cabin.hazard_off");
    if (pressed("dome"))
        dome_on_ = !dome_on_, pending_reach_ = kDome;
    if (pressed("visor"))
        visor_down_ = !visor_down_, pending_reach_ = kVisor;
    if (pressed("indicator_left"))
        indicator_ = indicator_ == -1 ? 0 : -1, pending_reach_ = kStalkL;
    if (pressed("indicator_right"))
        indicator_ = indicator_ == 1 ? 0 : 1, pending_reach_ = kStalkL;
    if (pressed("wipers"))
        wipers_on_ = !wipers_on_, pending_reach_ = kStalkR;
    if (pressed("radio"))
    {
        pending_reach_ = kRadio;
        radio_ = (radio_ + 1) % 4;
        if (options_.audio)
        {
            options_.audio->StopMusic();
            if (radio_ > 0)
                options_.audio->StartMusic(kStations[radio_]);
        }
        toast(kStationKeys[radio_]);
    }
    taunt_held_ = held("taunt");
}

void RacePath::UpdateCarRigs(float dt)
{
    Scene& s = *scene;
    if (!options_.features.empty()) // autotest screenshots of the car features
    {
        const std::string& f = options_.features;
        auto has = [&](const char* key) { return f.find(key) != std::string::npos; };
        cam_mode_ = has("cockpit") ? CamMode::Cockpit : has("hood") ? CamMode::Hood : has("far") ? CamMode::Far : CamMode::Near;
        look_back_ = has("back");
        taunt_held_ = has("taunt");
        indicator_ = has("left") ? -1 : has("right") ? 1 : 0;
        wipers_on_ = has("wipers");
        if (has("lights"))
            lights_on_ = true;
        if (has("hazard")) hazard_on_ = true;
        if (has("dome")) dome_on_ = true;
        if (has("visor")) visor_down_ = true;
        if (has("mirrorL")) glance_ = 1;
        if (has("mirrorC")) glance_ = 2;
        if (has("mirrorR")) glance_ = 3;
        // press a control every 1.2 s (screenshot tests of the reach animation)
        static const std::pair<const char*, int> presses[] = { { "pressweapon", kWeapon }, { "pressradio", kRadio }, { "presslights", kLights }, { "presshorn", kHorn },
                                                                       { "presshazard", kHazard }, { "pressdome", kDome }, { "pressvisor", kVisor } };
        for (const auto& [key, ctl] : presses)
            if (has(key) && std::fmod(blink_t_, 1.2f) < dt)
                pending_reach_ = ctl;
    }
    blink_t_ += dt;
    const bool blink_on = std::fmod(blink_t_, 0.75f) < 0.4f; // ~1.3 Hz relay
    if (blink_on != blink_was_on_ && (indicator_ != 0 || hazard_on_) && options_.audio)
        options_.audio->PlaySfx("indicator", 0.35f);
    blink_was_on_ = blink_on;
    wiper_t_ += wipers_on_ ? dt : 0.0f;
    taunt_k_ += ((taunt_held_ ? 1.0f : 0.0f) - taunt_k_) * std::min(1.0f, dt * 8.0f);
    toast_t_ = std::max(0.0f, toast_t_ - dt);
    const float step = std::clamp(dt, 1e-4f, 1.0f / 30.0f);

    for (size_t i = 0; i < rigs_.size() && i < session_->CarCount(); ++i)
    {
        CarRig& r = rigs_[i];
        VehicleRuntime& car = session_->Car(i);
        const VehicleTelemetry& t = car.Telemetry();
        const DriverInput& in = car.LastInput();
        const bool player = int(i) == player_;
        r.time += step;
        const float speed = t.forward_speed_kmh / 3.6f;
        const float accel = (speed - r.last_speed) / step;
        r.last_speed = speed;
        const float lateral = speed * t.yaw_rate;

        // --- steering wheel (~14:1 from the road wheels, clockwise for a right steer) and hands
        const float wheel_angle = -in.steer * 2.0f;
        TurnLocalY(s, r.steer, wheel_angle);
        for (int k = 0; k < 2; ++k)
            TurnLocalY(s, r.hand[k], wheel_angle);
        const XMVECTOR q_wheel = XMQuaternionMultiply(XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), wheel_angle), XMLoadFloat4(&r.steer.rot));
        const XMVECTOR wheel_pos = XMLoadFloat3(&r.steer.pos);

        // --- gauges: speed 0-260, revs 0-8000 over 250 degrees, fuel slowly down, temp warms up
        auto needle = [&](const Part& p, float k) { TurnLocalY(s, p, 2.18f - 4.36f * std::clamp(k, 0.0f, 1.0f)); };
        needle(r.needle_speed, std::fabs(t.forward_speed_kmh) / 260.0f);
        needle(r.needle_rev, t.rpm / 8000.0f);
        needle(r.needle_fuel, 0.8f - r.time / 1200.0f);
        needle(r.needle_temp, std::min(0.55f, 0.2f + r.time / 90.0f));

        // --- gear change: clutch in, right hand to the knob, lever through neutral into the new
        // gate, hand back to the rim (0.48 s)
        if (t.gear != r.gear && r.shift_t < 0.0f)
        {
            r.gear_from = r.gear;
            r.gear_to = t.gear;
            r.shift_t = 0.0f;
        }
        r.gear = t.gear;
        float lever_k = 1.0f, hand_k = 0.0f;
        if (r.shift_t >= 0.0f)
        {
            r.shift_t += step;
            const float a = r.shift_t;
            hand_k = a < 0.12f ? Smooth(a / 0.12f) : a < 0.34f ? 1.0f : 1.0f - Smooth((a - 0.34f) / 0.14f);
            lever_k = a < 0.12f ? 0.0f : Smooth((a - 0.12f) / 0.2f);
            if (a > 0.48f)
                r.shift_t = -1.0f, hand_k = 0.0f, lever_k = 1.0f;
        }
        r.clutch += ((r.shift_t >= 0.0f && r.shift_t < 0.36f ? 1.0f : 0.0f) - r.clutch) * std::min(1.0f, step * 18.0f);
        float fa = 0, ff = 0, ta = 0, tf = 0;
        GatePos(r.shift_t >= 0.0f ? r.gear_from : r.gear, fa, ff);
        GatePos(r.shift_t >= 0.0f ? r.gear_to : r.gear, ta, tf);
        const float across = fa + (ta - fa) * Smooth((lever_k - 0.3f) / 0.4f);
        const float fwd = lever_k < 0.5f ? ff * (1.0f - Smooth(lever_k / 0.3f)) : tf * Smooth((lever_k - 0.7f) / 0.3f);
        const XMVECTOR q_lever = XMQuaternionMultiply(XMLoadFloat4(&r.lever.rot), Aim(XMVectorSet(across * 0.3f, 1.0f, fwd * 0.33f, 0)));
        if (TransformComponent* lt = T(s, r.lever))
        {
            XMStoreFloat4(&lt->rotation_local, q_lever);
            lt->SetDirty();
        }
        const XMVECTOR knob = XMLoadFloat3(&r.lever.pos) + XMVector3Rotate(XMLoadFloat3(&r.knob_local), q_lever);

        // --- pedals, handbrake, stalks
        TurnLocalY(s, r.pedal_gas, 0.32f * std::max(0.0f, in.forward));
        TurnLocalY(s, r.pedal_brake, 0.28f * in.brake);
        TurnLocalY(s, r.pedal_clutch, 0.4f * r.clutch);
        // legs follow the pedals they rest on: left on the clutch, right on the gas (D-076)
        if (r.has_legs)
            for (int k = 0; k < 2; ++k)
            {
                const Part& pedal = k == 0 ? r.pedal_clutch : r.pedal_gas;
                const TransformComponent* pt = T(s, pedal);
                const XMVECTOR q = pt ? XMLoadFloat4(&pt->rotation_local) : XMLoadFloat4(&pedal.rot);
                const XMVECTOR target = XMLoadFloat3(&pedal.pos) + XMVector3Rotate(XMLoadFloat3(&r.foot_target_local[k]), q);
                const float side = k == 0 ? -1.0f : 1.0f;
                const XMVECTOR ankle = SolveLimb(r.thigh[k], r.shin[k], XMLoadFloat3(&r.thigh[k].pos), target, 0.46f, 0.46f,
                                                 XMVectorSet(side * 0.15f, 1.0f, 0.3f, 0), 1.0f);
                Place(s, r.foot[k], ankle, XMLoadFloat4(&r.foot[k].rot));
                static int leg_logs = 0;
                if (std::getenv("RACER_DRIVER_DEBUG") && leg_logs++ < 2)
                    wi::backlog::post("[drvdbg] leg " + std::to_string(k) + " short of the pedal by " +
                                      std::to_string(XMVectorGetX(XMVector3Length(target - ankle))) + " m, hip-target " +
                                      std::to_string(XMVectorGetX(XMVector3Length(target - XMLoadFloat3(&r.thigh[k].pos)))));
            }
        // handbrake (D-078): the right hand leaves the wheel for the lever first, then pulls it up
        const float hb_in = player && options_.features.find("handbrake") != std::string::npos ? 1.0f : in.handbrake; // autotest flag
        r.hb_k = std::clamp(r.hb_k + (hb_in > 0.1f && r.has_hb_grip ? 8.0f : -5.0f) * step, 0.0f, 1.0f);
        TurnLocalY(s, r.handbrake, -0.45f * hb_in * (r.has_hb_grip ? Smooth(std::clamp((r.hb_k - 0.5f) * 2.0f, 0.0f, 1.0f)) : 1.0f));
        const int ind = player ? indicator_ : 0;
        r.stalk_l_k += (float(-ind) - r.stalk_l_k) * std::min(1.0f, step * 20.0f);
        r.stalk_r_k += ((player && wipers_on_ ? 1.0f : 0.0f) - r.stalk_r_k) * std::min(1.0f, step * 20.0f);
        TurnBody(s, r.stalk_l, XMQuaternionRotationAxis(XMVectorSet(0, 0, 1, 0), 0.22f * r.stalk_l_k));
        TurnBody(s, r.stalk_r, XMQuaternionRotationAxis(XMVectorSet(0, 0, 1, 0), -0.22f * r.stalk_r_k));

        // --- air freshener: a pendulum pushed by the car's accelerations
        Spring(r.fresh_x, r.fresh_vx, std::clamp(-lateral * 0.06f, -0.9f, 0.9f), 1.1f, 0.12f, step);
        Spring(r.fresh_z, r.fresh_vz, std::clamp(accel * 0.05f, -0.7f, 0.7f), 1.1f, 0.12f, step);
        TurnBody(s, r.freshener, XMQuaternionRotationRollPitchYaw(r.fresh_z, 0.0f, r.fresh_x));

        // --- driver: torso leans out of the turn, head looks into it (and back over the shoulder)
        Spring(r.roll, r.roll_v, std::clamp(-lateral * 0.011f, -0.13f, 0.13f), 1.6f, 0.45f, step);
        const float yaw_target = std::clamp(in.steer * 0.45f, -0.5f, 0.5f) + (player && look_back_ && cam_mode_ != CamMode::Cockpit ? 1.25f : 0.0f);
        r.head_yaw += (yaw_target - r.head_yaw) * std::min(1.0f, step * 6.0f);
        r.head_nod += (std::clamp(-accel * 0.012f, -0.12f, 0.12f) - r.head_nod) * std::min(1.0f, step * 5.0f);
        const XMVECTOR q_roll = XMQuaternionRotationAxis(XMVectorSet(0, 0, 1, 0), r.roll);
        const XMVECTOR hip = XMLoadFloat3(&r.torso.pos);
        auto swayed = [&](const XMFLOAT3& p) { return hip + XMVector3Rotate(XMLoadFloat3(&p) - hip, q_roll); };
        TurnBody(s, r.torso, q_roll);
        if (r.head.ok())
        {
            const XMVECTOR q_head = XMQuaternionMultiply(XMQuaternionMultiply(XMLoadFloat4(&r.head.rot), XMQuaternionRotationRollPitchYaw(r.head_nod, r.head_yaw, 0.0f)), q_roll);
            Place(s, r.head, swayed(r.head.pos), q_head);
        }

        // --- driver's window winds down before the arm goes out, and back up after (D-064)
        const bool want_window = player && (taunt_held_ || taunt_k_ > 0.05f);
        r.window_k = std::clamp(r.window_k + (want_window ? 2.5f : -1.6f) * step, 0.0f, 1.0f);
        if (r.window.ok())
        {
            // down its own height into the door (per car, from the model's WINDOW_DROP marker)
            const XMVECTOR up_pos = XMLoadFloat3(&r.window.pos);
            const XMVECTOR down_pos = r.window_drop.ok() ? XMLoadFloat3(&r.window_drop.pos) : up_pos - XMVectorSet(0.0f, 0.4f, 0.0f, 0);
            Place(s, r.window, XMVectorLerp(up_pos, down_pos, Smooth(r.window_k)), XMLoadFloat4(&r.window.rot));
        }

        // --- the driver presses the control the player used (hand leaves the rim, finger pushes)
        if (player && pending_reach_ != kNone)
        {
            const int hand = (pending_reach_ == kStalkL || pending_reach_ == kLights || pending_reach_ == kVisor) ? 0 : 1;
            const bool busy = r.reach_t >= 0.0f || (hand == 1 && r.shift_t >= 0.0f) || (hand == 0 && taunt_k_ > 0.05f);
            if (!busy)
                r.reach_ctl = pending_reach_, r.reach_t = 0.0f;
            pending_reach_ = kNone;
        }
        float reach_k = 0.0f, press = 0.0f;
        int reach_hand = -1;
        XMVECTOR touch = XMVectorZero();
        if (r.reach_t >= 0.0f)
        {
            r.reach_t += step;
            const float a = r.reach_t;
            reach_k = a < 0.16f ? Smooth(a / 0.16f) : a < 0.3f ? 1.0f : 1.0f - Smooth((a - 0.3f) / 0.18f);
            press = (a > 0.16f && a < 0.3f) ? std::sin(XM_PI * (a - 0.16f) / 0.14f) : 0.0f;
            reach_hand = (r.reach_ctl == kStalkL || r.reach_ctl == kLights || r.reach_ctl == kVisor) ? 0 : 1;
            switch (r.reach_ctl)
            {
            case kStalkL: touch = XMLoadFloat3(&r.stalk_l.pos) + XMVectorSet(-0.13f, 0.012f, -0.02f, 0); break;
            case kStalkR: touch = XMLoadFloat3(&r.stalk_r.pos) + XMVectorSet(0.13f, 0.012f, -0.02f, 0); break;
            case kLights: touch = XMLoadFloat3(&r.light_knob.pos); break;
            case kRadio: touch = XMLoadFloat3(&r.radio_button.pos); break;
            case kWeapon: touch = XMLoadFloat3(&r.weapon_button.pos) + XMVectorSet(0, 0.012f, 0, 0); break;
            case kHazard: touch = XMLoadFloat3(&r.hazard_button.pos); break;
            case kDome: touch = XMLoadFloat3(&r.dome_switch.pos); break;
            case kVisor: touch = XMLoadFloat3(&r.visor.pos) + XMVectorSet(0.0f, -0.06f, -0.08f, 0); break;
            default: touch = XMLoadFloat3(&r.steer.pos); break;
            }
            if (a > 0.48f)
                r.reach_t = -1.0f, reach_k = 0.0f, reach_hand = -1;
        }
        // the weapon's safety cover flips up while the hand is on its way, the button goes in
        const bool weapon_reach = reach_hand == 1 && r.reach_ctl == kWeapon;
        r.cover_k = std::clamp(r.cover_k + (weapon_reach ? 6.0f : -3.0f) * step, 0.0f, 1.0f);
        TurnLocalY(s, r.weapon_cover, -1.9f * Smooth(r.cover_k));
        auto push = [&](const Part& p, bool on) {
            if (p.ok())
                Place(s, p, XMLoadFloat3(&p.pos) - XMVectorSet(0, 0.009f * (on ? press : 0.0f), 0.0f, 0), XMLoadFloat4(&p.rot));
        };
        push(r.weapon_button, weapon_reach);
        push(r.radio_button, reach_hand == 1 && r.reach_ctl == kRadio);
        push(r.hazard_button, reach_hand == 1 && r.reach_ctl == kHazard);
        push(r.dome_switch, reach_hand == 1 && r.reach_ctl == kDome);
        // the sun visor swings down on its hinge once the hand has it
        r.visor_k = std::clamp(r.visor_k + ((player && visor_down_) ? 3.0f : -3.0f) * step, 0.0f, 1.0f);
        TurnLocalY(s, r.visor, -0.6f * Smooth(r.visor_k)); // a half flip: cuts glare, keeps the road
        r.light_k += ((player ? (lights_on_ ? 1.0f : 0.0f) : (lamps_on_ ? 1.0f : 0.0f)) - r.light_k) * std::min(1.0f, step * 10.0f);
        TurnLocalY(s, r.light_knob, 0.9f * r.light_k);

        // --- arms: left on the rim or out of the window, right on the rim or on the gear knob
        const bool taunting = player && taunt_k_ > 0.02f;
        const float tk = player ? std::min(Smooth(taunt_k_), Smooth(r.window_k)) : 0.0f; // the glass first
        if (r.has_arms)
        {
            const XMVECTOR knob_off = XMVectorSet(0.0f, 0.06f, -0.07f, 0); // wrist behind and above the knob
            for (int k = 0; k < 2; ++k)
            {
                const XMVECTOR S = swayed(r.upper[k].pos);
                XMVECTOR wrist = wheel_pos + XMVector3Rotate(XMLoadFloat3(&r.grip_local[k]), q_wheel);
                if (k == 0 && taunting)
                {
                    const float shake = 0.015f * std::sin(r.time * 16.0f);
                    // fist out through the middle of the open door glass, clear of the frame (the old
                    // shoulder-relative spot sat at the B-pillar and the arm went through the glass)
                    XMVECTOR window = S + XMVectorSet(-0.4f, 0.22f + shake, 0.3f, 0);
                    if (r.window.ok() && r.window_drop.ok())
                    {
                        const XMFLOAT3 wp = r.window.pos, dp = r.window_drop.pos;
                        const float h = std::max(0.2f, wp.y - dp.y - 0.03f);
                        const float out = wp.x < 0.0f ? -1.0f : 1.0f;
                        // out past the glass line, in the middle of the opening (shoulder + 0.25 m forward,
                        // between the pillars) and within reach: the arm stays bent (~0.45 of 0.57 m)
                        const XMFLOAT3 sh = XMFLOAT3(XMVectorGetX(S), XMVectorGetY(S), XMVectorGetZ(S));
                        // clearly outside the door (a third of a metre past the glass line), just above the
                        // sill, then pulled in to what the arm reaches (0.55 of its 0.57 m), so the hand
                        // stays on the forearm
                        window = XMVectorSet(wp.x + out * 0.33f, wp.y + 0.14f + shake, sh.z + 0.22f, 0);
                        const XMVECTOR reach = window - S;
                        const float len = XMVectorGetX(XMVector3Length(reach));
                        if (len > 0.55f)
                            window = S + reach * (0.55f / len);
                    }
                    wrist = XMVectorLerp(wrist, window, tk);
                    Place(s, r.taunt_hand, wrist, XMQuaternionRotationRollPitchYaw(0.1f * std::sin(r.time * 16.0f), 0.0f, 0.25f));
                }
                if (k == 1 && r.hb_k > 0.0f && r.hb_k >= hand_k)
                {
                    // the fist round the handbrake grip, riding up with the lever
                    const TransformComponent* lt = T(s, r.handbrake);
                    const XMVECTOR q = lt ? XMLoadFloat4(&lt->rotation_local) : XMLoadFloat4(&r.handbrake.rot);
                    const XMVECTOR grip = XMLoadFloat3(&r.handbrake.pos) + XMVector3Rotate(XMLoadFloat3(&r.hb_grip_local), q);
                    wrist = XMVectorLerp(wrist, grip + knob_off, Smooth(r.hb_k));
                    Place(s, r.shift_hand, wrist - knob_off, XMLoadFloat4(&r.shift_hand.rot));
                }
                else if (k == 1 && hand_k > 0.0f)
                {
                    wrist = XMVectorLerp(wrist, knob + knob_off, hand_k);
                    Place(s, r.shift_hand, wrist - knob_off, XMLoadFloat4(&r.shift_hand.rot)); // pivot authored at the knob
                }
                if (k == reach_hand)
                {
                    // index finger to the control: fingertip on the touch point, pushing on "press"
                    const XMVECTOR dir = XMVector3Normalize(touch - S);
                    const XMVECTOR tip = touch + dir * (0.012f * press - 0.004f);
                    wrist = XMVectorLerp(wrist, tip - dir * 0.17f, reach_k);
                    Place(s, r.point_hand[k], wrist + dir * 0.17f, ArcFromZ(dir));
                }
                SolveArm(r, k, S, wrist, 1.0f);
            }
            for (int k = 0; k < 2; ++k)
                Show(s, r.point_hand[k], k == reach_hand && reach_k > 0.35f);
            Show(s, r.hand[0], !(taunting && tk > 0.35f) && !(reach_hand == 0 && reach_k > 0.35f));
            Show(s, r.taunt_hand, taunting && tk > 0.35f);
            const float right_k = std::max(hand_k, r.hb_k);
            Show(s, r.hand[1], right_k < 0.4f && !(reach_hand == 1 && reach_k > 0.35f));
            Show(s, r.shift_hand, right_k >= 0.4f);
        }

        // --- lamps: brake lights on every car, the player's indicators, headlights
        for (const auto& [e, base] : r.tail)
            if (MaterialComponent* m = s.materials.GetComponent(e))
                m->SetEmissiveStrength(base * (in.brake > 0.1f ? 2.8f : 1.0f));
        const bool hazard = player && hazard_on_;
        const bool ind_l_on = (ind == -1 || hazard) && blink_on, ind_r_on = (ind == 1 || hazard) && blink_on;
        for (const auto& [e, base] : r.ind_l)
            if (MaterialComponent* m = s.materials.GetComponent(e))
                m->SetEmissiveStrength(ind_l_on ? base * 3.0f : 0.0f);
        for (const auto& [e, base] : r.ind_r)
            if (MaterialComponent* m = s.materials.GetComponent(e))
                m->SetEmissiveStrength(ind_r_on ? base * 3.0f : 0.0f);
        const bool lights = player ? lights_on_ : lamps_on_;
        for (const auto& [e, base] : r.lamps)
            if (MaterialComponent* m = s.materials.GetComponent(e))
                m->SetEmissiveStrength(lights ? base : base * 0.15f);
        if (LightComponent* l = r.headlight != INVALID_ENTITY ? s.lights.GetComponent(r.headlight) : nullptr)
            l->intensity = lights ? (night_glow_ ? 30.0f : 10.0f) : 0.0f; // 45 blew a car ahead into a white disc; by day a third
        if (!player)
            continue;
        // --- cabin lamps (D-079): cluster arrows, handbrake, engine at low health, high beam, hazard;
        // shift lights climbing with the revs; the dome light
        Show(s, r.warn_ind[0], ind_l_on);
        Show(s, r.warn_ind[1], ind_r_on);
        Show(s, r.warn_handbrake, in.handbrake > 0.1f || r.hb_k > 0.5f); // the lever is up
        const Combatant& me = session_->Combat().Get(int(i));
        const bool low = me.health < me.max_health * 0.3f;
        Show(s, r.warn_engine, low && std::fmod(blink_t_, 0.5f) < 0.3f);
        Show(s, r.warn_beam, lights);
        Show(s, r.warn_hazard, hazard && blink_on);
        Show(s, r.hazard_button, true);
        {
            const VehicleDefinition& d = car.Definition();
            const float rev = std::clamp((t.rpm - d.shift_up_rpm * 0.55f) / (d.shift_up_rpm * 0.45f), 0.0f, 1.0f);
            const bool flash = t.rpm > d.shift_up_rpm * 0.97f && std::fmod(blink_t_, 0.12f) < 0.06f; // shift now!
            for (int k = 0; k < 10; ++k)
                Show(s, r.shift_led[k], flash ? false : rev * 10.0f > float(k) + 0.5f);
        }
        Show(s, r.dome_lit, dome_on_);
        if (LightComponent* dl = r.dome_light != INVALID_ENTITY ? s.lights.GetComponent(r.dome_light) : nullptr)
            dl->intensity = dome_on_ ? 0.5f : 0.0f;
        // Wipers sweep 0 -> 100 degrees and back, once a second.
        const float sweep = wipers_on_ ? 0.87f * (1.0f - std::cos(wiper_t_ * XM_2PI)) : 0.0f;
        TurnLocalY(s, r.wiper_l, sweep);
        TurnLocalY(s, r.wiper_r, sweep);
    }
}

// Live mirrors (D-064). In the cockpit view each mirror glass shows a small second render of the
// scene from the mirror, flipped like a real mirror; outside the cockpit they are plain glass and
// cost nothing. The mouse (or right stick) glances at the left, centre or right mirror.
void RacePath::UpdateMirrors(float dt)
{
    using namespace wi::input;
    const bool cockpit = cam_mode_ == CamMode::Cockpit && session_ && size_t(player_) < rigs_.size();
    // --- gaze: a flick of the mouse picks the mirror, a flick down (or 2.5 s of rest) looks ahead
    const XMFLOAT4 ptr = GetPointer();
    if (mouse_prev_.x >= 0.0f && cockpit)
    {
        mouse_acc_x_ += ptr.x - mouse_prev_.x;
        mouse_acc_y_ += ptr.y - mouse_prev_.y;
    }
    mouse_prev_ = XMFLOAT2(ptr.x, ptr.y);
    const XMFLOAT4 stick = GetAnalog(GAMEPAD_ANALOG_THUMBSTICK_R);
    mouse_acc_x_ += stick.x * 40.0f * dt * 60.0f;
    mouse_acc_y_ -= stick.y * 40.0f * dt * 60.0f;
    mouse_acc_x_ *= std::pow(0.02f, dt); // the flick has to be quick: old movement fades out
    mouse_acc_y_ *= std::pow(0.02f, dt);
    const float kFlick = 60.0f;
    if (cockpit && options_.features.empty())
    {
        const int before = glance_;
        if (mouse_acc_x_ < -kFlick)
            glance_ = glance_ == 3 ? 0 : 1;
        else if (mouse_acc_x_ > kFlick)
            glance_ = glance_ == 1 ? 0 : 3;
        else if (mouse_acc_y_ < -kFlick)
            glance_ = 2;
        else if (mouse_acc_y_ > kFlick)
            glance_ = 0;
        if (glance_ != before)
            mouse_acc_x_ = mouse_acc_y_ = 0.0f, glance_idle_ = 0.0f;
        glance_idle_ += dt;
        if (glance_ != 0 && glance_idle_ > 2.5f && std::fabs(mouse_acc_x_) + std::fabs(mouse_acc_y_) < 5.0f)
            glance_ = 0;
    }
    if (!cockpit && options_.features.find("mirror") == std::string::npos)
        glance_ = 0;
    if (glance_ != 0)
        glance_last_ = glance_;
    glance_k_ += ((glance_ != 0 ? 1.0f : 0.0f) - glance_k_) * std::min(1.0f, dt * 9.0f);

    // --- the mirror renders
    Scene& s = *scene;
    const CarRig* rig = cockpit ? &rigs_[size_t(player_)] : nullptr;
    const TransformComponent* body = session_ ? s.transforms.GetComponent(session_->Car(size_t(player_)).Body()) : nullptr;
    for (int m = 0; m < 3; ++m)
    {
        const bool want = rig && body && rig->has_eye && !rig->mirror_mats.empty();
        if (want && !mirrors_[m])
        {
            mirrors_[m] = std::make_unique<Mirror>();
            Mirror& mr = *mirrors_[m];
            mr.path.init(m == 1 ? 480u : 320u, m == 1 ? 140u : 200u, 96.0f);
            mr.path.scene = scene;
            mr.path.camera = &mr.cam;
            mr.path.setSceneUpdateEnabled(false);
            mr.path.setAO(AO_DISABLED);
            // mirror cost (D-070): three full renders took the cockpit from 8.6 to 17.3 ms; a small
            // picture does not need shadows, lens flare or sharpening
            mr.path.setShadowsEnabled(false);
            mr.path.setLensFlareEnabled(false);
            mr.path.setSharpenFilterEnabled(false);
            mr.path.setBloomEnabled(false);
            mr.path.setMotionBlurEnabled(false);
            mr.path.setFXAAEnabled(false);
            mr.path.setReflectionsEnabled(false);
            mr.path.setOutlineEnabled(true);
            mr.path.setOutlineThickness(1.0f);
            mr.path.setOutlineThreshold(0.1f);
            mr.path.setOutlineColor(XMFLOAT4(0.05f, 0.05f, 0.07f, 1.0f));
            mr.path.setExposure(getExposure());
            mr.path.setTonemap(wi::renderer::Tonemap::ACES);
            mr.path.setContrast(getContrast());
            mr.path.setSaturation(getSaturation());
            mr.path.Start();
        }
        if (!mirrors_[m])
            continue;
        Mirror& mr = *mirrors_[m];
        mr.active = want;
        if (!want)
            continue;
    }
    mirror_dt_ = dt;
    // mirror glass materials: the live picture (flipped horizontally) or plain glass
    if (size_t(player_) < rigs_.size())
        for (const auto& [e, m] : rigs_[size_t(player_)].mirror_mats)
            if (MaterialComponent* mat = s.materials.GetComponent(e))
            {
                const bool live = mirrors_[m] && mirrors_[m]->active;
                mat->shaderType = live ? MaterialComponent::SHADERTYPE_UNLIT : MaterialComponent::SHADERTYPE_CARTOON;
                if (live)
                    mat->textures[MaterialComponent::BASECOLORMAP].resource.SetTexture(mirrors_[m]->path.GetRenderResult3D());
                else
                    mat->textures[MaterialComponent::BASECOLORMAP].resource = {};
                mat->texMulAdd = live ? XMFLOAT4(-1.0f, 1.0f, 1.0f, 0.0f) : XMFLOAT4(1, 1, 0, 0);
                mat->SetBaseColor(live ? XMFLOAT4(1, 1, 1, 1) : XMFLOAT4(0.75f, 0.8f, 0.85f, 1));
                mat->SetDirty();
            }
}

// Mirror cameras are placed after the frame's scene update (from PreRender), so they ride on this
// frame's body transform: placed during Update they lagged one frame behind the car, and at speed the
// own cabin and the cars behind jumped around in the glass.
void RacePath::PlaceMirrorCameras()
{
    if (!scene || !session_ || size_t(player_) >= rigs_.size())
        return;
    Scene& s = *scene;
    const CarRig* rig = &rigs_[size_t(player_)];
    const TransformComponent* body = s.transforms.GetComponent(session_->Car(size_t(player_)).Body());
    if (!body)
        return;
    for (int m = 0; m < 3; ++m)
    {
        if (!mirrors_[m] || !mirrors_[m]->active)
            continue;
        Mirror& mr = *mirrors_[m];
        // from the glass itself (a real mirror's point of view), aimed back along the flank / the road
        static const XMFLOAT3 kDir[3] = { { -0.1f, -0.03f, -1.0f }, { 0.0f, -0.01f, -1.0f }, { 0.1f, -0.03f, -1.0f } };
        const XMMATRIX w = XMLoadFloat4x4(&body->world);
        const XMFLOAT3 mp = rig->has_mirror[m] ? rig->mirror_local[m] : rig->eye_local;
        const XMVECTOR pos = XMVector3Transform(XMVectorSet(mp.x, mp.y, mp.z - 0.06f, 1), w);
        const XMVECTOR dir = XMVector3Normalize(XMVector3TransformNormal(XMLoadFloat3(&kDir[m]), w));
        mr.cam.CreatePerspective(float(mr.path.GetPhysicalWidth()), float(mr.path.GetPhysicalHeight()), m == 1 ? 0.12f : 0.3f, 900.0f, XMConvertToRadians(m == 1 ? 30.0f : 30.0f));
        XMStoreFloat3(&mr.cam.Eye, pos);
        XMStoreFloat3(&mr.cam.At, dir);
        XMStoreFloat3(&mr.cam.Up, XMVector3Normalize(w.r[1]));
        mr.cam.SetDirty();
        mr.cam.UpdateCamera();
        mr.path.PreUpdate();
        mr.path.Update(mirror_dt_);
        mr.path.PostUpdate();
    }
}

void RacePath::PreRender()
{
    PlaceMirrorCameras();
    // the centre mirror every frame, the side mirrors on alternate frames (each at half the frame rate)
    ++mirror_frame_;
    for (int m = 0; m < 3; ++m)
        mirror_due_[m] = mirrors_[m] && mirrors_[m]->active && (m == 1 || (mirror_frame_ & 1) == (m == 0 ? 0u : 1u));
    for (int m = 0; m < 3; ++m)
        if (mirror_due_[m])
            mirrors_[m]->path.PreRender();
    RenderPath3D::PreRender();
}

void RacePath::Render() const
{
    for (int m = 0; m < 3; ++m)
        if (mirror_due_[m])
            mirrors_[m]->path.Render();
    RenderPath3D::Render();
}
