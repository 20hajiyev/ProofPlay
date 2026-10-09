#pragma once
#include "WickedEngine.h"
#include "racer/Recovery.h"
#include "racer/SimClock.h"
#include "racer/Vehicle.h"
#include "racer/runtime/PhysicsStepper.h"
#include "racer/runtime/VehicleRuntime.h"

#include <memory>
#include <string>
#include <vector>

// M1 handling sandbox: one car on the greybox tuning ground, chase camera, telemetry HUD.
class SandboxPath : public wi::RenderPath3D
{
public:
    struct Options
    {
        std::string content_dir;     // content/ (JSON definitions)
        std::string cooked_dir;      // cooked .wiscene models
        float autotest_seconds = 0;  // > 0: scripted drive, then report and quit
        std::string report_path;
        std::string screenshot_path;
    };

    explicit SandboxPath(Options options);
    ~SandboxPath() override;

    // wi::Application::ActivatePath calls Start(), not Load() (Load is for loading screens).
    void Start() override;
    void Update(float dt) override;
    void Compose(wi::graphics::CommandList cmd) const override;

    bool AutotestFinished() const { return autotest_done_; }
    bool Failed() const { return !error_.empty(); }
    void MarkFailed(const std::string& reason) { if (error_.empty()) error_ = reason; }
    void SetWindowActive(bool active) { window_active_ = active; }
    void WriteReport() const;

private:
    racer::VehicleCommand ReadPlayerInput(float dt);
    racer::VehicleCommand AutotestInput(float t) const;
    void UpdateCamera(float dt);
    void RecordFrame(float dt);

    Options options_;
    std::string error_;
    bool loaded_ = false;
    racer::SimClock clock_;
    racer::CommandBuffer commands_;
    std::unique_ptr<racer::runtime::PhysicsStepper> stepper_;
    std::unique_ptr<racer::runtime::VehicleRuntime> car_;
    uint64_t tick_ = 0;
    uint32_t insane_steps_ = 0;
    uint32_t capped_frames_ = 0;
    racer::RecoveryMonitor recovery_;
    uint32_t recoveries_[4] = {}; // by RecoveryReason
    void RecoverTo(const racer::RecoveryRequest& r);
    bool window_active_ = true;
    float reset_hold_s_ = 0;
    int camera_mode_ = 0;
    XMFLOAT3 cam_eye_ = {}, cam_at_ = {};
    bool cam_initialized_ = false;

    // Autotest measurements.
    float elapsed_s_ = 0;
    bool autotest_done_ = false;
    std::vector<float> frame_ms_;
    uint64_t ram_warm_ = 0, vram_warm_ = 0, ram_end_ = 0, vram_end_ = 0;
    float max_speed_kmh_ = 0;
    struct MemorySample { float t; double ram_mib, vram_mib; };
    std::vector<MemorySample> memory_samples_;
    float next_sample_s_ = 0;
};
