#pragma once
#include "racer/CameraFeel.h"
#include "racer/Damage.h"
#include "racer/Weather.h"
#include "racer/Localization.h"
#include "WickedEngine.h"
#include "racer/AI.h"
#include "racer/AICombat.h"
#include "racer/Audio.h"
#include "racer/AudioRuntime.h"
#include "racer/BodyMotion.h"
#include "racer/Career.h"
#include "racer/SimClock.h"
#include "racer/runtime/RaceSession.h"
#include "Recorder.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

// M2 playable race: the player against AI opponents with all eight abilities, on the
// temporary pad route (until the Harbor tracks exist).
class RacePath : public wi::RenderPath3D
{
public:
    struct Options
    {
        std::string content_dir;
        std::string cooked_dir;
        int opponents = 11;           // plan E01: 12 participants
        int laps = 3;
        float autotest_seconds = 0;   // > 0: the player's car is driven by the AI; report and quit
        float budget_mean_ms = 0, budget_p99_ms = 0, budget_ram_mib = 0; // --perf-budget (D-092): fail the run past these
        std::string report_path;
        std::string track = "S01";    // "S01" Dock Loop, or "pad" for the greybox tuning route
        std::string car = "D01";      // player's car id
        int field_offset = -1;        // which slice of the local cars races; -1 = varies per race
        racer::AIDifficulty difficulty = racer::AIDifficulty::Normal;
        racer::EventMode mode = racer::EventMode::CombatRace;
        float time_limit_s = 0.0f;                // Destruction: 90 s (plan 2.12)
        bool restrict_abilities = false;          // apply allowed_abilities (event rules)
        std::vector<racer::Ability> allowed_abilities;
        std::string title;                        // localised event name for the HUD
        std::string text_continue = "Enter: continue";
        std::string text_abandon = "Q: abandon race";
        racer::AudioRuntime* audio = nullptr;     // owned by the game; null = silent
        float volume_master = 1.0f, volume_music = 0.8f, volume_sfx = 1.0f;
        std::map<std::string, std::string> bindings = racer::DefaultBindings(); // keyboard remap
        float stick_deadzone = 0.15f;
        float ui_scale = 1.0f;        // HUD text scale (0.8-1.5)
        bool high_contrast = false;
        bool motion_blur = true;
        bool reduce_shake = false;    // accessibility: calmer camera, no speed lines (D-058)
        std::string time_of_day;      // "day", "sunset", "night"; empty = the track's own (D-052)
        racer::WeatherState weather;  // D-083: from the event, or --weather=rain[:0.6]
        std::map<std::string, std::string> player_parts; // customisation slot -> variant (D-053); missing = stock
        racer::DriverLook driver_look;                   // the player's driver character (D-076)
        XMFLOAT4 player_paint = XMFLOAT4(1.0f, 0.45f, 0.0f, 1.0f); // garage paint (D-055); default: signature orange
        const racer::Strings* text = nullptr; // HUD toasts (camera, radio); the chosen language
        std::string features;         // test hook: "cockpit,hood,far,back,taunt,left,right,wipers,lights" (D-060)
    };

    explicit RacePath(Options options);

    // Leaving the race: after the finish (Enter) the result counts; abandoning from the pause
    // menu (Q) does not (plan 2.16: quitting mid-race never completes the event).
    bool ExitRequested() const { return exit_requested_; }
    racer::RaceSummary PlayerSummary() const;
    void RequestExit() { exit_requested_ = true; }
    ~RacePath() override;

    void Start() override;
    void Update(float dt) override;
    void PreRender() override;
    void Render() const override;
    void Compose(wi::graphics::CommandList cmd) const override;

    bool AutotestFinished() const { return autotest_done_; }
    bool Failed() const { return !error_.empty(); }
    void MarkFailed(const std::string& reason) { if (error_.empty()) error_ = reason; }
    void SetWindowActive(bool active) { window_active_ = active; }
    void WriteReport() const;

private:
    struct CombatVisuals;

    void ReadPlayerInput(float dt, racer::VehicleCommand& drive, racer::CombatCommand& combat);
    void BuildTrackDressing();
    void ApplyHarborLook(wi::scene::Scene& s);
    void AddStreetLights(wi::scene::Scene& s);
    wi::ecs::Entity AddHeadlight(wi::scene::Scene& s, wi::ecs::Entity root, float length_m);
    // Car features (D-060): per-car animated parts and lamp materials; the player's switches.
public:
    // One animated part: its node and its authored local pose.
    struct Part
    {
        wi::ecs::Entity e = wi::ecs::INVALID_ENTITY;
        XMFLOAT3 pos{};
        XMFLOAT4 rot{ 0, 0, 0, 1 };
        bool ok() const { return e != wi::ecs::INVALID_ENTITY; }
    };
private:
    struct CarRig
    {
        // D-060..D-063: wheel, gauges, wipers, stalks, pedals, gear lever, handbrake, freshener, and
        // the driver: torso sway, head turn, two-bone IK arms, gear-change and taunt hands.
        Part steer, needle_speed, needle_rev, needle_fuel, needle_temp, wiper_l, wiper_r, stalk_l, stalk_r;
        Part pedal_gas, pedal_brake, pedal_clutch, handbrake, lever, freshener;
        Part torso, head, upper[2], fore[2], hand[2], shift_hand, taunt_hand;
        Part thigh[2], shin[2], foot[2];   // legs, IK onto the pedals (D-076); 0 left (clutch), 1 right (gas)
        XMFLOAT3 foot_target_local[2] = {}; // ankle on the pedal pad, in the pedal pivot's frame
        bool has_legs = false;
        XMFLOAT3 hb_grip_local{};           // handbrake grip in the lever pivot's frame (D-078)
        float hb_k = 0.0f;                  // 0 hand on the wheel .. 1 hand on the handbrake
        bool has_hb_grip = false;
        // cabin functions (D-079): cluster lamps, shift lights, hazard button, dome light, sun visor
        Part warn_ind[2], warn_handbrake, warn_engine, warn_beam, warn_hazard, hazard_button, dome_lit, dome_switch, visor;
        Part shift_led[10];
        wi::ecs::Entity dome_light = wi::ecs::INVALID_ENTITY;
        float visor_k = 0.0f;
        Part window_drop; // WINDOW_DROP marker: the glass pivot when fully wound down
        Part window, light_knob, radio_button, weapon_button, weapon_cover, point_hand[2]; // D-064
        std::vector<std::pair<wi::ecs::Entity, int>> mirror_mats; // mirror glass materials, 0 L / 1 C / 2 R
        XMFLOAT3 mirror_local[3] = {}; // mirror glass centres in the car body frame
        bool has_mirror[3] = {};
        wi::ecs::Entity headlight = wi::ecs::INVALID_ENTITY;
        XMFLOAT3 grip_local[2] = {}; // wrist target in the steering pivot's frame
        XMFLOAT3 knob_local{};       // gear knob in the lever pivot's frame
        bool has_arms = false;
        XMFLOAT3 eye_local{};
        bool has_eye = false;
        std::vector<std::pair<wi::ecs::Entity, float>> tail, ind_l, ind_r, lamps; // material + authored emissive strength
        // animation state
        int gear = 1, gear_from = 1, gear_to = 1;
        float shift_t = -1.0f;       // < 0: hands on the wheel
        float roll = 0.0f, roll_v = 0.0f, head_yaw = 0.0f, head_nod = 0.0f;
        float window_k = 0.0f, cover_k = 0.0f, light_k = 0.0f;
        int reach_ctl = 0;      // control the driver reaches for (Reach enum in RaceCarFeatures.cpp)
        float reach_t = -1.0f;
        float fresh_x = 0.0f, fresh_vx = 0.0f, fresh_z = 0.0f, fresh_vz = 0.0f;
        float last_speed = 0.0f, clutch = 0.0f, stalk_l_k = 0.0f, stalk_r_k = 0.0f, time = 0.0f;
    };
    enum class CamMode { Near, Far, Hood, Cockpit };
    void BuildRig(size_t car, wi::ecs::Entity root, const std::vector<std::pair<wi::ecs::Entity, std::string>>& materials, float origin_height);
    void ReadCarFeatureInput();
    int pending_reach_ = 0;
    bool hazard_on_ = false, dome_on_ = false, visor_down_ = false; // cabin functions (D-079)       // a control the player just used: the driver presses it (D-064)
    // Live mirrors (D-064): a small second render of the scene per mirror glass, cockpit view only.
    struct Mirror
    {
        wi::RenderPath3D path;
        wi::scene::CameraComponent cam;
        bool active = false;
    };
    std::unique_ptr<Mirror> mirrors_[3];
    void UpdateMirrors(float dt);
    void PlaceMirrorCameras();
    static constexpr uint32_t kPlayerHeadLayer = 1u << 7; // the player's head: hidden from the cockpit camera only
    float mirror_dt_ = 0.0f;
    uint32_t mirror_frame_ = 0;
    bool mirror_due_[3] = {};
    int glance_ = 0;              // cockpit gaze: 0 road, 1 left mirror, 2 centre mirror, 3 right mirror
    float glance_yaw_ = 0.0f, glance_pitch_ = 0.0f, mouse_acc_x_ = 0.0f, mouse_acc_y_ = 0.0f, glance_idle_ = 0.0f;
    float glance_k_ = 0.0f;       // 0 road .. 1 looking into the chosen mirror
    int glance_last_ = 2;         // the mirror the gaze is on / returning from
    XMFLOAT2 mouse_prev_{ -1.0f, -1.0f };
    void UpdateCarRigs(float dt);
    void SolveArm(CarRig& r, int side, const XMVECTOR& shoulder, const XMVECTOR& wrist, float hide);
    XMVECTOR SolveLimb(const Part& a, const Part& b, const XMVECTOR& S, const XMVECTOR& target, float l1, float l2, const XMVECTOR& pole, float hide);
    std::vector<CarRig> rigs_;
    CamMode cam_mode_ = CamMode::Near;
    bool look_back_ = false, lights_on_ = false, wipers_on_ = false, taunt_held_ = false, blink_was_on_ = false;
    int indicator_ = 0;    // -1 left, +1 right
    int radio_ = 1;        // 0 off, then the stations
    float blink_t_ = 0.0f, wiper_t_ = 0.0f, taunt_k_ = 0.0f;
    std::string toast_;    // a short HUD line (camera or station name)
    float toast_t_ = 0.0f;
    bool lamps_on_ = false; // dusk/night: street lights and headlights (D-052)
    // Toon sky (D-057): clouds on a ring that drifts with the wind, sun or moon and stars; all
    // follow the camera so they stay at the horizon.
    void BuildSky(wi::scene::Scene& s, const std::string& time_of_day, const XMFLOAT3& sun_dir);
    void UpdateSky(float dt);
    wi::ecs::Entity sky_clouds_ = wi::ecs::INVALID_ENTITY, sky_fixed_ = wi::ecs::INVALID_ENTITY;
    float sky_drift_ = 0.0f;
    // weather look (D-083): the grey of the sky, and the storm's lightning
    float weather_grey_ = 0.0f, lightning_next_ = 4.0f, lightning_left_ = 0.0f, base_exposure_ = 1.0f;
    XMFLOAT3 base_ambient_ = {};
    wi::ecs::Entity rain_emitter_ = wi::ecs::INVALID_ENTITY; // own rain streaks (D-083), follows the camera
    bool rain_filled_ = false;
    // visible damage (D-085): each car's body meshes as built, re-deformed when a dent grows; smoke
    struct CarDamage
    {
        struct Piece { wi::ecs::Entity mesh; std::vector<XMFLOAT3> original; XMFLOAT4X4 to_car, from_car; };
        std::vector<Piece> meshes; // undamaged positions, in the mesh's own frame and the car's
        racer::BodyBounds bounds = {};
        racer::DentState shown;
        wi::ecs::Entity smoke = wi::ecs::INVALID_ENTITY;
    };
    std::vector<CarDamage> damage_;
    void PrepareDamage(size_t car, wi::ecs::Entity root);
    void UpdateDamage();                                 // the box filled once, on the first frame
    std::vector<wi::ecs::Entity> spray_;                       // tyre spray per car on a wet road
    bool night_glow_ = false;                                  // the hour's own lamps (not the rain's)
    float wetness_ = 0.0f;
    void TextureParticles(wi::ecs::Entity material, const char* file);
    void BuildPuddles(wi::scene::Scene& s);
    void BuildRain(wi::scene::Scene& s, float amount, float wind_ms, bool dark);
    void UpdateWeather(float dt);
    wi::ecs::Entity LoadCarVisual(size_t car, const XMFLOAT4& paint);
    void SyncCombatVisuals(float dt);
    void UpdateCamera(float dt);
    void RecordFrame(float dt);
    void UpdateAudio(float dt);
    bool PlayerDone() const;

    Options options_;
    wi::scene::Scene own_scene_; // each race owns its world; destroyed with the path
    std::string error_;
    bool loaded_ = false;
    bool exit_requested_ = false;
    float finished_for_s_ = 0;
    bool window_active_ = true;
    bool paused_ = false;
    racer::SimClock clock_;
    std::unique_ptr<racer::runtime::RaceSession> session_;
    int player_ = 0;
    racer::CombatCommand pending_combat_;
    proofplay::Recorder proofplay_;  // records the race for the ProofPlay coach (hackathon layer)
    bool reset_pending_ = false;     // player reset since the last recorded step
    std::vector<wi::ecs::Entity> visual_roots_;
    std::vector<float> visual_root_y_;
    // Toon body animation (D-050): one spring model per car, applied to its BODY mesh.
    struct BodyAnim { wi::ecs::Entity body = wi::ecs::INVALID_ENTITY; XMFLOAT4 base_rotation{ 0, 0, 0, 1 }; XMFLOAT3 base_scale{ 1, 1, 1 }; racer::BodyMotion motion; };
    std::vector<BodyAnim> body_anim_;
    void AnimateBodies(float dt);   // model root offset below the body origin, per car
    bool visuals_aligned_ = false;

    // Autotest autopilot for the player's car.
    std::unique_ptr<racer::AIDriver> autopilot_;
    std::unique_ptr<racer::AICombatPlanner> autopilot_combat_;

    std::unique_ptr<CombatVisuals> fx_;

    // Audio: engine handles per car, combat events gathered over this frame's physics steps.
    std::vector<int> engine_handles_;
    // the player's performance-part sounds (D-072): turbo whistle and blow-off, exhaust pops
    std::unique_ptr<racer::TurboAudioModel> turbo_audio_;
    std::unique_ptr<racer::BackfireModel> backfire_audio_;
    float exhaust_gain_ = 1.0f;
    int pop_count_ = 0, blowoff_count_ = 0;
    float part_boost_ = 0.0f;
    int part_blowoffs_ = 0, part_pops_ = 0; // this frame
    void UpdatePartSounds(float dt);
    std::vector<racer::CombatEvent> frame_events_;
    std::vector<uint32_t> warned_projectiles_;
    int last_countdown_ = -1;
    racer::RacePhase last_phase_ = racer::RacePhase::Loading;
    float intensity_hold_s_ = 0;
    XMFLOAT3 prev_eye_ = {};
    wi::ecs::Entity reflection_probe_ = wi::ecs::INVALID_ENTITY; // follows the player's car
    float time_ = 0;
    XMFLOAT3 cam_eye_ = {}, cam_at_ = {};
    XMFLOAT3 cam_off_eye_ = {}, cam_off_at_ = {};
    float cam_clear_ = 100.0f; // chase camera distance free of walls, smoothed out // chase camera offsets from the car, smoothed (D-067)
    // Render interpolation (D-067): the 120 Hz physics poses of every car before and after the last
    // step; the frame shows the blend at the clock's leftover fraction, restored before stepping.
    struct SimPose { XMFLOAT3 p{}; XMFLOAT4 q{ 0, 0, 0, 1 }; };
    std::vector<SimPose> sim_prev_, sim_curr_;
    bool sim_blended_ = false;
    void ReadSimPoses(std::vector<SimPose>& out);
    void WriteSimPoses(const std::vector<SimPose>& a, const std::vector<SimPose>& b, float t);
    bool cam_initialized_ = false;
    racer::CameraFeel cam_feel_;   // speed FOV, swing, roll, shake, speed lines (D-058)
    float cam_prev_speed_ = 0.0f;
    bool cam_prev_grounded_ = true;
    float cam_fall_speed_ = 0.0f;

    // Measurements.
    float elapsed_s_ = 0;
    bool autotest_done_ = false;
    std::vector<float> frame_ms_;
    struct MemorySample { float t; double ram_mib, vram_mib; };
    std::vector<MemorySample> memory_samples_;
    float next_sample_s_ = 0;
    float max_speed_kmh_ = 0;   // player's top measured speed (report)
    float max_roll_ = 0, max_squash_dip_ = 0; int turn_frames_ = 0, outward_frames_ = 0; // body animation (report)
};
