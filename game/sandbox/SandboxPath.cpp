#include "SandboxPath.h"
#include "racer/runtime/HandlingTrack.h"

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

using namespace racer;
using namespace racer::runtime;
using namespace wi::scene;

namespace
{
    constexpr float kWarmupSeconds = 5.0f;

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
}

SandboxPath::SandboxPath(Options options) : options_(std::move(options)) {}

SandboxPath::~SandboxPath() = default;

void SandboxPath::Start()
{
    RenderPath3D::Start();
    if (loaded_)
        return;
    loaded_ = true;
    Scene& s = *scene;

    std::ifstream f(options_.content_dir + "/vehicles/D01.json", std::ios::binary);
    std::stringstream text;
    text << f.rdbuf();
    VehicleDefinition def;
    std::vector<ValidationIssue> issues;
    if (!ParseVehicleDefinition(text.str(), def, issues))
    {
        error_ = "D01.json invalid: " + (issues.empty() ? std::string("unreadable") : issues[0].path + " " + issues[0].message);
        return;
    }

    BuildHandlingTrack(s, true);

    s.weather.ambient = XMFLOAT3(0.28f, 0.3f, 0.34f);
    s.weather.horizon = XMFLOAT3(0.62f, 0.68f, 0.76f);
    s.weather.zenith = XMFLOAT3(0.22f, 0.36f, 0.62f);
    wi::ecs::Entity sun = s.Entity_CreateLight("sun", XMFLOAT3(0, 50, 0), XMFLOAT3(1.0f, 0.94f, 0.85f), 6.0f, 1000.0f, LightComponent::DIRECTIONAL);
    if (TransformComponent* t = s.transforms.GetComponent(sun))
    {
        t->RotateRollPitchYaw(XMFLOAT3(XMConvertToRadians(55.0f), XMConvertToRadians(35.0f), 0));
        t->UpdateTransform();
    }
    if (LightComponent* l = s.lights.GetComponent(sun))
        l->SetCastShadow(true);

    stepper_ = std::make_unique<PhysicsStepper>(s);
    car_ = std::make_unique<VehicleRuntime>(s, def, XMFLOAT3(0, 0, 0), 0.0f);

    // Visual model: cooked from the GLB, attached under the physics body.
    const std::string model = options_.cooked_dir + "/vehicles/" + def.id + ".wiscene";
    // Load into a staging scene and merge, like wi::scene::LoadModel(fileName) does; the
    // Scene& overload serializes straight into the target scene and is meant for empty scenes.
    Scene staging;
    wi::ecs::Entity visual = LoadModel(staging, model, XMMatrixIdentity(), true);
    s.Merge(staging);
    if (visual == wi::ecs::INVALID_ENTITY || !s.transforms.Contains(visual))
    {
        error_ = "missing cooked vehicle model " + model;
        return;
    }
    TransformComponent& vt = *s.transforms.GetComponent(visual);
    vt.Translate(XMFLOAT3(0, -car_->BodyOriginHeight(), 0));
    vt.UpdateTransform();
    s.Component_Attach(visual, car_->Body(), true);
    auto wheel = [&](const char* n) { return s.Entity_FindByName(n, visual); };
    car_->SetWheelEntities(wheel("WHEEL_PIVOT_FL"), wheel("WHEEL_PIVOT_FR"), wheel("WHEEL_PIVOT_RL"), wheel("WHEEL_PIVOT_RR"));

    stepper_->Prepare();
    if (!car_->ApplyTuning())
        error_ = "vehicle tuning failed";
    wi::backlog::post("[racer] sandbox loaded: " + (error_.empty() ? std::string("ok") : error_));

    camera->zNearP = 0.1f;
    camera->zFarP = 3000.0f;
    camera->fov = XMConvertToRadians(65.0f);
}

VehicleCommand SandboxPath::ReadPlayerInput(float dt)
{
    using namespace wi::input;
    VehicleCommand c;
    auto key = [](char k) { return Down(static_cast<BUTTON>(k)); };
    const XMFLOAT4 stick = GetAnalog(GAMEPAD_ANALOG_THUMBSTICK_L);
    const XMFLOAT4 rt = GetAnalog(GAMEPAD_ANALOG_TRIGGER_R);
    const XMFLOAT4 lt = GetAnalog(GAMEPAD_ANALOG_TRIGGER_L);

    c.throttle = std::max((key('W') || Down(KEYBOARD_BUTTON_UP)) ? 1.0f : 0.0f, rt.x);
    c.brake = std::max((key('S') || Down(KEYBOARD_BUTTON_DOWN)) ? 1.0f : 0.0f, lt.x);
    float steer = (key('D') || Down(KEYBOARD_BUTTON_RIGHT) ? 1.0f : 0.0f) - (key('A') || Down(KEYBOARD_BUTTON_LEFT) ? 1.0f : 0.0f);
    if (std::fabs(stick.x) > 0.15f) // input deadzone
        steer = stick.x;
    c.steer = steer;
    c.handbrake = (Down(KEYBOARD_BUTTON_SPACE) || Down(GAMEPAD_BUTTON_XBOX_R1)) ? 1.0f : 0.0f;

    // Plan 2.9: reset needs a 1 s hold so it cannot be triggered by accident.
    if (key('F') || Down(GAMEPAD_BUTTON_XBOX_Y))
    {
        reset_hold_s_ += dt;
        if (reset_hold_s_ >= 1.0f)
        {
            c.events |= EVENT_RESET;
            reset_hold_s_ = -1000.0f; // once per hold
        }
    }
    else
    {
        reset_hold_s_ = 0;
    }
    if (Press(static_cast<BUTTON>('C')))
        c.events |= EVENT_CAMERA;
    return c;
}

VehicleCommand SandboxPath::AutotestInput(float t) const
{
    // Straight launch, a right sweep, braking, then a left sweep, repeated.
    const float phase = std::fmod(t, 24.0f);
    VehicleCommand c;
    if (phase < 8)
        c.throttle = 1;
    else if (phase < 11)
        c.throttle = 0.7f, c.steer = 0.5f;
    else if (phase < 14)
        c.brake = 1;
    else if (phase < 20)
        c.throttle = 0.8f, c.steer = -0.4f;
    else
        c.brake = 0.6f;
    return c;
}

void SandboxPath::Update(float dt)
{
    if (!error_.empty() || !car_)
    {
        RenderPath3D::Update(dt);
        return;
    }

    const bool autotest = options_.autotest_seconds > 0;
    // Losing focus pauses the race; resuming never replays the lost wall time.
    clock_.SetPaused(!autotest && !window_active_);

    commands_.Submit(autotest ? AutotestInput(elapsed_s_) : ReadPlayerInput(dt));
    const ClockAdvance advance = clock_.Advance(dt);
    capped_frames_ += advance.dropped_seconds > 0;
    for (uint32_t i = 0; i < advance.steps; ++i)
    {
        const VehicleCommand step = commands_.ForStep();
        if (step.events & EVENT_RESET)
        {
            // Manual reset (1 s hold): back to the last safe anchor, or upright in place.
            RecoveryRequest r;
            const VehicleTelemetry& t = car_->Telemetry();
            r.reason = RecoveryReason::Stuck;
            r.position[0] = t.position[0];
            r.position[1] = t.position[1];
            r.position[2] = t.position[2];
            r.heading_rad = t.heading_rad;
            if (recovery_.HasAnchor())
                r = recovery_.AnchorRequest(RecoveryReason::Stuck);
            RecoverTo(r);
        }
        if (step.events & EVENT_CAMERA)
            camera_mode_ = (camera_mode_ + 1) % 2;
        car_->PrePhysics(step, float(clock_.StepSeconds()));
        stepper_->Step();
        const VehicleTelemetry& t = car_->PostPhysics(++tick_);
        insane_steps_ += !IsTelemetrySane(t);
        max_speed_kmh_ = std::max(max_speed_kmh_, t.forward_speed_kmh);
        const RecoveryRequest r = recovery_.Update(t, float(clock_.StepSeconds()), step.throttle > 0.1f || step.brake > 0.1f);
        if (r.reason != RecoveryReason::None)
            RecoverTo(r);
    }

    car_->UpdateWheelVisuals(-car_->BodyOriginHeight());
    RenderPath3D::Update(dt); // Scene::Update: transform sync only, physics is disabled
    UpdateCamera(dt);
    if (autotest)
        RecordFrame(dt);
}

void SandboxPath::RecoverTo(const RecoveryRequest& r)
{
    // Anchors are body positions; Teleport takes the ground point under the body.
    const float ground_y = r.position[1] - car_->BodyOriginHeight() + 0.05f;
    car_->Teleport(XMFLOAT3(r.position[0], ground_y, r.position[2]), r.heading_rad);
    recovery_.Reset(r.position, r.heading_rad);
    ++recoveries_[int(r.reason)];
    cam_initialized_ = false; // snap the camera instead of sweeping across the map
    static const char* names[] = { "none", "fell", "upside-down", "stuck" };
    wi::backlog::post(std::string("[racer] recovery: ") + names[int(r.reason)]);
}

void SandboxPath::UpdateCamera(float dt)
{
    const TransformComponent* body = scene->transforms.GetComponent(car_->Body());
    if (!body)
        return;
    XMMATRIX w = XMLoadFloat4x4(&body->world);
    XMVECTOR pos = w.r[3];
    XMVECTOR fwd = XMVector3Normalize(XMVectorSetY(w.r[2], 0));
    const float back = camera_mode_ == 0 ? 6.0f : 9.0f;
    const float up = camera_mode_ == 0 ? 2.2f : 3.2f;
    XMFLOAT3 eye, at;
    XMStoreFloat3(&eye, pos - fwd * back + XMVectorSet(0, up, 0, 0));
    XMStoreFloat3(&at, pos + fwd * 4.0f + XMVectorSet(0, 0.8f, 0, 0));
    if (!cam_initialized_)
    {
        cam_eye_ = eye;
        cam_at_ = at;
        cam_initialized_ = true;
    }
    // Frame-rate independent smoothing; suspension jitter is filtered, not transmitted.
    const float k_eye = ExpDecay(dt, 8.0f), k_at = ExpDecay(dt, 14.0f);
    XMStoreFloat3(&cam_eye_, XMVectorLerp(XMLoadFloat3(&cam_eye_), XMLoadFloat3(&eye), k_eye));
    XMStoreFloat3(&cam_at_, XMVectorLerp(XMLoadFloat3(&cam_at_), XMLoadFloat3(&at), k_at));

    // Plan 2.14: the camera never ends up inside a wall. Sweep from a pivot above the roof to
    // the smoothed eye and stop short of the first hit (after smoothing, so it cannot lag in).
    const XMVECTOR pivot = pos + XMVectorSet(0, 1.2f, 0, 0);
    const XMVECTOR to_eye = XMLoadFloat3(&cam_eye_) - pivot;
    const float dist = XMVectorGetX(XMVector3Length(to_eye));
    if (dist > 0.01f)
    {
        XMFLOAT3 o, d;
        XMStoreFloat3(&o, pivot);
        XMStoreFloat3(&d, to_eye / dist);
        const wi::physics::RayIntersectionResult hit = wi::physics::Intersects(*scene, wi::primitive::Ray(o, d, 0.0f, dist + 0.3f));
        if (hit.entity != wi::ecs::INVALID_ENTITY && hit.entity != car_->Body())
        {
            const float clear = std::max(0.3f, XMVectorGetX(XMVector3Length(XMLoadFloat3(&hit.position) - pivot)) - 0.3f);
            if (clear < dist)
                XMStoreFloat3(&cam_eye_, pivot + XMLoadFloat3(&d) * clear);
        }
    }
    camera->Eye = cam_eye_;
    camera->At = XMFLOAT3(cam_at_.x - cam_eye_.x, cam_at_.y - cam_eye_.y, cam_at_.z - cam_eye_.z);
    camera->Up = XMFLOAT3(0, 1, 0);
    camera->SetDirty();
    camera->UpdateCamera();
}

void SandboxPath::RecordFrame(float dt)
{
    elapsed_s_ += dt;
    if (elapsed_s_ >= next_sample_s_)
    {
        memory_samples_.push_back({ elapsed_s_, PrivateBytes() / 1048576.0, wi::graphics::GetDevice()->GetMemoryUsage().usage / 1048576.0 });
        const VehicleTelemetry& t = car_->Telemetry();
        wi::backlog::post("[racer] t=" + std::to_string(int(elapsed_s_)) + " pos=(" + std::to_string(int(t.position[0])) + "," + std::to_string(int(t.position[1])) + "," + std::to_string(int(t.position[2])) + ") kmh=" + std::to_string(int(t.forward_speed_kmh)) + " ticks=" + std::to_string(tick_));
        next_sample_s_ += 5.0f;
    }
    if (elapsed_s_ >= kWarmupSeconds)
    {
        frame_ms_.push_back(dt * 1000.0f);
        if (ram_warm_ == 0)
        {
            ram_warm_ = PrivateBytes();
            vram_warm_ = wi::graphics::GetDevice()->GetMemoryUsage().usage;
        }
    }
    if (elapsed_s_ >= options_.autotest_seconds && !autotest_done_)
    {
        ram_end_ = PrivateBytes();
        vram_end_ = wi::graphics::GetDevice()->GetMemoryUsage().usage;
        autotest_done_ = true;
        wi::backlog::post("[racer] autotest finished after " + std::to_string(elapsed_s_) + " s, " + std::to_string(frame_ms_.size()) + " frames");
    }
}

void SandboxPath::WriteReport() const
{
    std::ofstream out(options_.report_path);
    wi::backlog::post("[racer] writing report to '" + options_.report_path + "': " + (out ? "open" : "FAILED to open"));
    float mean = 0;
    for (float v : frame_ms_)
        mean += v / std::max<size_t>(1, frame_ms_.size());
    const VehicleTelemetry& t = car_ ? car_->Telemetry() : VehicleTelemetry{};
    out << "{\n"
        << "  \"error\": \"" << error_ << "\",\n"
        << "  \"seconds\": " << elapsed_s_ << ",\n"
        << "  \"frames_measured\": " << frame_ms_.size() << ",\n"
        << "  \"frame_ms_mean\": " << mean << ",\n"
        << "  \"frame_ms_p95\": " << Percentile(frame_ms_, 0.95f) << ",\n"
        << "  \"frame_ms_p99\": " << Percentile(frame_ms_, 0.99f) << ",\n"
        << "  \"frames_hitting_step_cap\": " << capped_frames_ << ",\n"
        << "  \"sim_ticks\": " << tick_ << ",\n"
        << "  \"insane_steps\": " << insane_steps_ << ",\n"
        << "  \"recoveries_fell_upside_stuck\": [" << recoveries_[1] << ", " << recoveries_[2] << ", " << recoveries_[3] << "],\n"
        << "  \"max_speed_kmh\": " << max_speed_kmh_ << ",\n"
        << "  \"final_up_y\": " << t.up_y << ",\n"
        << "  \"ram_growth_mib\": " << (double(ram_end_) - double(ram_warm_)) / 1048576.0 << ",\n"
        << "  \"vram_growth_mib\": " << (double(vram_end_) - double(vram_warm_)) / 1048576.0 << ",\n"
        << "  \"ram_end_mib\": " << ram_end_ / 1048576.0 << ",\n"
        << "  \"vram_end_mib\": " << vram_end_ / 1048576.0 << ",\n"
        << "  \"memory_samples\": [";
    for (size_t i = 0; i < memory_samples_.size(); ++i)
        out << (i ? ", " : "") << "[" << memory_samples_[i].t << ", " << memory_samples_[i].ram_mib << ", " << memory_samples_[i].vram_mib << "]";
    out << "]\n}\n";
}

void SandboxPath::Compose(wi::graphics::CommandList cmd) const
{
    RenderPath3D::Compose(cmd);
    wi::font::Params p;
    p.posX = 24;
    p.posY = 130; // below Wicked's info display
    p.size = 22;
    p.shadowColor = wi::Color::Black();
    p.color = wi::Color::White();
    std::string text;
    if (!error_.empty())
    {
        text = "ERROR: " + error_;
        p.color = wi::Color(255, 90, 90, 255);
    }
    else if (car_)
    {
        const VehicleTelemetry& t = car_->Telemetry();
        char buf[512];
        std::snprintf(buf, sizeof(buf),
            "%.0f km/h   gear %d   %.0f rpm\n"
            "slip FL %.2f FR %.2f RL %.2f RR %.2f\n"
            "contact %d%d%d%d   tick %llu%s\n"
            "WASD drive, Space handbrake, hold F reset, C camera",
            t.forward_speed_kmh, t.gear, t.rpm,
            t.wheel_longitudinal_slip[0], t.wheel_longitudinal_slip[1], t.wheel_longitudinal_slip[2], t.wheel_longitudinal_slip[3],
            t.wheel_contact[0], t.wheel_contact[1], t.wheel_contact[2], t.wheel_contact[3],
            static_cast<unsigned long long>(t.tick), clock_.IsPaused() ? "   PAUSED" : "");
        text = buf;
    }
    wi::font::Draw(text, p, cmd);
}
