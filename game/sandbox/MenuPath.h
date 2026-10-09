#pragma once
#include "WickedEngine.h"
#include "racer/AudioRuntime.h"
#include "racer/Career.h"
#include "racer/Customization.h"
#include "racer/Event.h"
#include "racer/Localization.h"
#include "racer/Vehicle.h"

#include <map>
#include <string>
#include <vector>

// Region event list (plan 2.14 "Karyera xəritəsi / Event məlumatı" for the M2 slice):
// pick an event and a car, see medals, switch language. All text comes from string tables.
class MenuPath : public wi::RenderPath3D
{
public:
    struct Context
    {
        const racer::RegionEvents* events = nullptr;
        const racer::SaveProfile* profile = nullptr;
        const racer::Strings* az = nullptr;
        const racer::Strings* en = nullptr;
        std::vector<std::string> cars; // D01..D05
        std::map<std::string, std::string> car_names; // display names; id shown when absent
        std::map<std::string, racer::VehicleDefinition> car_defs; // garage comparison panel
        std::string* language = nullptr; // "az" / "en", owned by the settings
        racer::Settings* settings = nullptr;   // volumes edited on the settings screen (saved by the game)
        racer::AudioRuntime* audio = nullptr;  // UI sounds and live volume preview; null = silent
        std::map<std::string, racer::CarCustomization> catalogs; // customisable cars (D-055)
        racer::SaveProfile* garage = nullptr;   // the same profile, writable: customisation edits it
        std::string content_dir, cooked_dir;    // garage preview: catalogs and cooked car models
    };

    // The menu renders its own small 3D scene (the garage preview), never the race scene.
    explicit MenuPath(Context context) : ctx_(std::move(context)) { scene = &garage_scene_; camera = &garage_cam_; }

    void Update(float dt) override;
    void Compose(wi::graphics::CommandList cmd) const override;

    bool StartRequested() const { return start_requested_; }
    bool QuitRequested() const { return quit_requested_; }
    void ClearRequests() { start_requested_ = quit_requested_ = false; }
    const racer::EventDefinition& SelectedEvent() const { return ctx_.events->events[size_t(selected_)]; }
    const std::string& SelectedCar() const { return ctx_.cars[size_t(car_)]; }
    void SetMessage(std::string text) { message_ = std::move(text); }
    void SetAudio(racer::AudioRuntime* audio) { ctx_.audio = audio; } // the runtime is created after the menu
    // Menu autotest: one action per frame instead of the keyboard/gamepad.
    // U D L R = arrows, E = Enter/A, B = Esc/B, O = settings, G = language, C = customise.
    // Scripts start in the event list; a leading T keeps the title screen (then E leaves it).
    void SetScript(std::string actions) { title_ = !actions.empty() && actions[0] == 'T'; script_ = std::move(actions); }
    bool ScriptDone() const { return script_pos_ >= script_.size(); }
    bool SettingsOpen() const { return settings_open_; }
    bool CustomizeOpen() const { return custom_open_; }
    // True once after the garage changed: the game saves the profile.
    bool TakeGarageChanged() { const bool c = garage_changed_; garage_changed_ = false; return c; }
    // Autotest: choose programmatically.
    void Select(const std::string& event_id, const std::string& car);
    void SkipTitle() { title_ = false; }

private:
    const racer::Strings& Text() const { return (ctx_.language && *ctx_.language == "en") ? *ctx_.en : *ctx_.az; }
    void Ui(const char* sound) const;
    void UpdateSettings();
    void UpdateControls();
    void ComposeControls(wi::graphics::CommandList cmd) const;
    void ComposeSettings(wi::graphics::CommandList cmd) const;
    void ComposeGarage(wi::graphics::CommandList cmd) const;
    void UpdateCustomize();
    void ComposeCustomize(wi::graphics::CommandList cmd) const;
    void UpdatePreview(float dt);
    void ComposeTitle(wi::graphics::CommandList cmd) const;

    Context ctx_;
    int selected_ = 0;
    int car_ = 0;
    bool start_requested_ = false;
    bool quit_requested_ = false;
    std::string message_;
    bool settings_open_ = false;
    int settings_row_ = 0;
    bool controls_open_ = false, listening_ = false;
    int controls_row_ = 0;
    bool custom_open_ = false, garage_changed_ = false;
    int custom_row_ = 0;
    wi::scene::Scene garage_scene_;
    wi::scene::CameraComponent garage_cam_;
    std::string preview_key_;          // car + parts + paint currently shown
    wi::ecs::Entity preview_root_ = wi::ecs::INVALID_ENTITY;
    float spin_ = 0.6f;
    bool look_set_ = false;
    bool title_ = true;   // title screen first (D-056)
    float time_ = 0.0f;   // UI animation clock
    struct Actions { bool up = false, down = false, left = false, right = false, confirm = false, back = false, settings = false, language = false, customize = false; std::string key; };
    Actions ReadActions();
    Actions actions_;
    std::string script_;
    size_t script_pos_ = 0;
};
