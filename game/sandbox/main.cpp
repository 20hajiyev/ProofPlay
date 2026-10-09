#include "MenuPath.h"
#include "RacePath.h"
#include "SandboxPath.h"
#include "racer/Career.h"
#include "racer/Customization.h"
#include "racer/Event.h"
#include "racer/Localization.h"
#include "racer/Performance.h"
#include "racer/Vehicle.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <windows.h>
#include <psapi.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>

namespace
{
    class SandboxApplication : public wi::Application
    {
    public:
        void SetAlwaysActive(bool value) { alwaysactive = value; }
    };
    SandboxApplication application;

    std::string Arg(const std::wstring& cmd, const wchar_t* key)
    {
        const size_t at = cmd.find(key);
        if (at == std::wstring::npos)
            return {};
        size_t start = at + wcslen(key);
        size_t end = cmd.find(L' ', start);
        const std::wstring v = cmd.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        // UTF-8, so paths with non-ASCII characters (e.g. Azerbaijani letters) survive.
        const int n = WideCharToMultiByte(CP_UTF8, 0, v.c_str(), int(v.size()), nullptr, 0, nullptr, nullptr);
        std::string out(size_t(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, v.c_str(), int(v.size()), out.data(), n, nullptr, nullptr);
        return out;
    }

    // Shared main loop: activation after engine init, focus handling, autotest watchdog, report.
    template <typename Path>
    int Run(HWND hwnd, Path& path, float autotest_seconds, const std::string& screenshot_path, bool has_report)
    {
        const float capture_every = float(std::atof(Arg(GetCommandLineW(), L"--capture-every=").c_str()));
        double next_capture = 0.0;
        int frames_captured = 0;
        bool activated = false;
        wi::Timer watchdog;
        int exit_code = 0;
        MSG msg = {};
        while (msg.message != WM_QUIT)
        {
            if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
                continue;
            }
            if (!activated && wi::initializer::IsInitializeFinished())
            {
                // After engine init (which initializes physics itself), before the first scene update.
                racer::runtime::InitializePhysicsRuntime();
                // Wicked stops updating an unfocused window; a measurement run must not depend on
                // focus. Set after init, which overwrites the flag from command-line arguments.
                if (autotest_seconds > 0)
                    application.SetAlwaysActive(true);
                application.ActivatePath(&path);
                activated = true;
            }
            path.SetWindowActive(application.is_window_active);
            application.Run();
            // --capture-every=<s>: a frame sequence next to the screenshot (<name>_000.png ...), for
            // flicker and popping bugs one still frame cannot show
            if (capture_every > 0 && !screenshot_path.empty() && activated && watchdog.elapsed_seconds() >= next_capture && frames_captured < 120)
            {
                char suffix[16];
                std::snprintf(suffix, sizeof suffix, "_%03d.png", frames_captured++);
                wi::helper::saveTextureToFile(wi::graphics::GetDevice()->GetBackBuffer(&application.swapChain), screenshot_path.substr(0, screenshot_path.size() - 4) + suffix);
                next_capture = watchdog.elapsed_seconds() + capture_every;
            }

            // Watchdog: an autotest must finish within its duration plus shader-compile/startup slack.
            if (autotest_seconds > 0 && watchdog.elapsed_seconds() > autotest_seconds + 240.0)
                path.MarkFailed("autotest watchdog: did not finish in time");
            if (autotest_seconds > 0 && (path.AutotestFinished() || path.Failed()))
            {
                // Not wi::helper::screenshot(swapchain, name): in v0.72.106 that overload drops the
                // name and also overwrites the user's clipboard.
                if (!screenshot_path.empty())
                    wi::helper::saveTextureToFile(wi::graphics::GetDevice()->GetBackBuffer(&application.swapChain), screenshot_path);
                if (has_report)
                    path.WriteReport();
                exit_code = path.Failed() ? 1 : 0;
                DestroyWindow(hwnd);
            }
        }
        return exit_code;
    }
}

namespace
{
    std::string ReadText(const std::string& path)
    {
        std::ifstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
        std::stringstream s;
        s << f.rdbuf();
        return s.str();
    }

    bool WriteText(const std::string& path, const std::string& text)
    {
        std::ofstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary | std::ios::trunc);
        return bool(f << text);
    }

    std::string LocalDataDir()
    {
        const wchar_t* base = _wgetenv(L"LOCALAPPDATA");
        const std::filesystem::path dir = std::filesystem::path(base ? base : L".") / L"Racer";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        const std::u8string u = dir.u8string();
        return std::string(u.begin(), u.end());
    }

    uint64_t PrivateMiB()
    {
        PROCESS_MEMORY_COUNTERS_EX c{};
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&c), sizeof(c));
        return c.PrivateUsage / (1024 * 1024);
    }

    // Menu -> race -> results -> save -> menu (plan 2.1 core loop). With cycles > 0 it drives
    // itself: E01 with the autopilot for 20 s, abandon, back to the menu, measuring memory.
    int RunGameFlow(HWND hwnd, int cycles, const std::string& report_path, const std::string& menu_script = {}, const std::string& screenshot_path = {})
    {
        const float flow_capture = float(std::atof(Arg(GetCommandLineW(), L"--capture-every=").c_str()));
        wi::Timer flow_timer;
        double flow_next = 0.0;
        int flow_frames = 0;
        using namespace racer;
        const std::string content = RACER_CONTENT_DIR;
        RegionEvents events;
        std::vector<ValidationIssue> issues;
        Strings az, en;
        std::string err;
        if (!ParseRegionEvents(ReadText(content + "/events/R01.json"), events, issues) || !az.Load(ReadText(content + "/text/az.json"), err) ||
            !en.Load(ReadText(content + "/text/en.json"), err))
        {
            MessageBoxA(hwnd, ("Content invalid: " + (issues.empty() ? err : issues[0].path + " " + issues[0].message)).c_str(), "Racer", MB_OK);
            return 1;
        }

        const std::string data = LocalDataDir();
        const std::string save_path = data + "/career.json";
        const std::string settings_path = data + "/settings.json";
        Settings settings;
        ParseSettings(ReadText(settings_path), settings, err); // missing/invalid -> defaults
        SaveProfile profile;
        SaveIO::LoadSource source;
        bool save_blocked = false;
        std::string startup_message;
        if (!SaveIO::Load(save_path, profile, source, err))
        {
            // Damaged with no backup: play on a fresh profile but never overwrite the file.
            save_blocked = true;
            profile = SaveProfile{};
            startup_message = "Save damaged, not overwritten: " + err;
        }
        else if (source == SaveIO::LoadSource::Backup)
            startup_message = "Save restored from backup (" + err + ")";

        MenuPath::Context ctx;
        ctx.events = &events;
        ctx.profile = &profile;
        ctx.az = &az;
        ctx.en = &en;
        const std::vector<std::string> own_cars = { "D01", "D02", "D03", "D04", "D05" };
        ctx.cars = own_cars; // the local real-brand cars (D-035) were removed on the owner's request (D-091)
        // Garage comparison: definitions of every selectable car.
        for (const std::string& id : ctx.cars)
        {
            const std::string file = content + "/vehicles/" + id + ".json";
            racer::VehicleDefinition def;
            std::vector<racer::ValidationIssue> issues;
            if (racer::ParseVehicleDefinition(ReadText(file), def, issues))
                ctx.car_defs[id] = def;
        }
        ctx.language = &settings.language;
        ctx.settings = &settings;
        // Customisation catalogs of the own cars (D-055), written by tools/generate_tuner_cars.py.
        for (const std::string& id : own_cars)
        {
            racer::CarCustomization cat;
            std::string cat_error;
            if (racer::ParseCustomization(ReadText(content + "/customization/" + id + ".json"), cat, cat_error))
                ctx.catalogs[id] = cat;
        }
        ctx.garage = &profile;
        ctx.content_dir = content;
        ctx.cooked_dir = RACER_COOKED_DIR;
        MenuPath menu(ctx);
        menu.SetMessage(startup_message);

        // Declared before the races so it outlives them (they remove their engines on destruction).
        // No device or missing assets: the game runs silent.
        AudioRuntime audio;
        if (!audio.Init({ RACER_AUDIO_DIR, false }))
            wi::backlog::post("[racer] audio unavailable, running silent", wi::backlog::LogLevel::Warning);
        audio.SetVolumes(settings.master_volume, settings.music_volume, settings.sfx_volume);
        menu.SetAudio(&audio);

        std::unique_ptr<RacePath> race, retired;
        const EventDefinition* running = nullptr;
        int retire_countdown = 0;
        int settle_frames = 0; // flow autotest: frames to wait in the menu before sampling memory
        bool activated = false;
        int cycles_done = 0;
        // Menu autotest (--menu-script): scripted actions, then a screenshot and a state report.
        const bool scripted = !menu_script.empty();
        if (scripted)
            menu.SetScript(menu_script);
        if (cycles > 0)
            menu.SkipTitle(); // flow soak: straight to the event list
        int script_done_frames = 0, races_started = 0, races_failed = 0;
        std::string last_track;
        std::vector<std::pair<uint64_t, uint64_t>> memory; // (RAM MiB, VRAM MiB) back in the menu
        auto text = [&]() -> const Strings& { return settings.language == "en" ? en : az; };

        MSG msg = {};
        while (msg.message != WM_QUIT)
        {
            if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
                continue;
            }
            if (!activated && wi::initializer::IsInitializeFinished())
            {
                racer::runtime::InitializePhysicsRuntime();
                if (cycles > 0)
                    application.SetAlwaysActive(true);
                application.ActivatePath(&menu);
                activated = true;
            }
            if (race)
                race->SetWindowActive(application.is_window_active);
            application.Run();
            if (!activated)
                continue;
            // --capture-every=<s> also in the menu flow: frames only while a race runs (race<n>_<k>.png)
            if (flow_capture > 0 && race && !screenshot_path.empty() && flow_timer.elapsed_seconds() >= flow_next && flow_frames < 400)
            {
                char suffix[32];
                std::snprintf(suffix, sizeof suffix, "_race%d_%03d.png", races_started, flow_frames++);
                wi::helper::saveTextureToFile(wi::graphics::GetDevice()->GetBackBuffer(&application.swapChain), screenshot_path.substr(0, screenshot_path.size() - 4) + suffix);
                flow_next = flow_timer.elapsed_seconds() + flow_capture;
            }

            // A retired race is destroyed a few frames after the switch, once no frame uses it.
            if (retired && --retire_countdown <= 0)
            {
                retired.reset();
                // GPU deletions are deferred for a few frames and 12 imported cars are ~2 GB: sampling
                // right after reset measured the previous race, not a leak (D-031 follow-up).
                if (cycles > 0)
                    settle_frames = 30;
            }
            if (settle_frames > 0 && --settle_frames == 0)
                memory.emplace_back(PrivateMiB(), wi::graphics::GetDevice()->GetMemoryUsage().usage / (1024 * 1024));

            bool auto_start = false;
            if (!race && !retired && settle_frames == 0 && cycles > 0 && cycles_done < cycles)
            {
                // Flow autotest: pick E01 with a different car each cycle, as if Enter was pressed.
                menu.Select("R01_E01", own_cars[size_t(cycles_done) % own_cars.size()]);
                auto_start = true;
            }
            if (!race && (menu.StartRequested() || auto_start))
            {
                running = &menu.SelectedEvent();
                RacePath::Options o;
                o.content_dir = content;
                o.cooked_dir = RACER_COOKED_DIR;
                o.car = menu.SelectedCar();
                o.driver_look = profile.driver; // the player's driver character (D-076)
                if (const auto cat = ctx.catalogs.find(o.car); cat != ctx.catalogs.end())
                {
                    const racer::CarSetup setup = profile.garage.count(o.car) ? profile.garage.at(o.car) : racer::CarSetup{};
                    o.player_parts = racer::ResolvedParts(cat->second, setup);
                    for (const auto& [k, v] : setup.parts) // performance upgrades ride along (D-071)
                        if (k.rfind(racer::kPerformancePrefix, 0) == 0)
                            o.player_parts[k] = v;
                    if (setup.paint >= 0)
                    {
                        const racer::PaintPreset& c = racer::GetPaintPreset(setup.paint);
                        o.player_paint = XMFLOAT4(c.r, c.g, c.b, 1.0f);
                    }
                }
                o.opponents = running->participants - 1;
                o.laps = running->laps;
                o.track = running->track;
                o.mode = running->mode;
                if (running->mode == EventMode::Destruction)
                    o.time_limit_s = 90.0f; // plan 2.12: player + 7 targets, 90 s
                o.difficulty = running->difficulty;
                o.weather = running->weather;
                o.restrict_abilities = true;
                o.allowed_abilities = running->allowed_abilities;
                o.title = text().Get(running->name_key);
                o.text = &text();
                o.text_continue = text().Get("results.continue");
                o.audio = &audio;
                o.volume_master = settings.master_volume;
                o.volume_music = settings.music_volume;
                o.volume_sfx = settings.sfx_volume;
                o.bindings = settings.bindings;
                o.stick_deadzone = settings.stick_deadzone;
                o.ui_scale = settings.ui_scale;
                o.high_contrast = settings.high_contrast;
                o.motion_blur = settings.motion_blur;
                o.reduce_shake = settings.reduce_shake;
                if (scripted)
                    o.autotest_seconds = 6.0f; // a scripted start: prove the event loads, then return
                ++races_started;
                last_track = o.track;
                if (cycles > 0)
                {
                    o.autotest_seconds = 20.0f; // autopilot drives the player's car
                    // Same four fields repeat every four cycles, so the leak gate compares like with like.
                    o.field_offset = (cycles_done % 4) * 11;
                }
                race = std::make_unique<RacePath>(o);
                application.ActivatePath(race.get());
                menu.ClearRequests();
            }
            // Garage edits save at once; a scripted menu test never writes the owner's save.
            if (menu.TakeGarageChanged() && !scripted && !save_blocked)
            {
                std::string garage_error;
                menu.SetMessage(SaveIO::Save(save_path, profile, garage_error) ? text().Get("custom.saved") : text().Get("custom.save_failed") + " " + garage_error);
            }
            if (race && (race->ExitRequested() || race->AutotestFinished() || race->Failed()))
            {
                const RaceSummary summary = race->PlayerSummary();
                std::string message;
                if (race->Failed())
                    message = "Race failed to start", ++races_failed;
                else if (summary.finished && running)
                {
                    const std::string result_id = running->id + "-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
                    const ApplyOutcome out = ApplyRaceResult(profile, MakeRaceResult(*running, summary, result_id), events.mastery);
                    std::string save_error;
                    const bool saved = !save_blocked && !scripted && SaveIO::Save(save_path, profile, save_error); // tests never touch the save
                    message = text().Get("results.position") + " " + std::to_string(summary.position) + "   " + text().Get("results.medals_gained") + " +" +
                              std::to_string(out.medals_gained) + "   " + (saved ? text().Get("results.saved") : text().Get("results.save_failed") + " " + save_error);
                    for (const std::string& m : out.mastery_completed)
                        message += "   " + text().Get("results.mastery_done") + " " + text().Get("mastery." + m);
                }
                else
                    message = "-";
                menu.SetMessage(message);
                application.ActivatePath(&menu);
                retired = std::move(race);
                retire_countdown = 4;
                running = nullptr;
                if (cycles > 0)
                    ++cycles_done;
            }
            if (cycles > 0 && cycles_done >= cycles && !race && !retired && settle_frames == 0)
                DestroyWindow(hwnd);
            if (menu.QuitRequested())
                DestroyWindow(hwnd);
            if (scripted && menu.ScriptDone() && !race && !retired && ++script_done_frames == 20)
            {
                if (!screenshot_path.empty())
                    wi::helper::saveTextureToFile(wi::graphics::GetDevice()->GetBackBuffer(&application.swapChain), screenshot_path);
                char buf[512];
                std::snprintf(buf, sizeof(buf), "{\n  \"settings_open\": %s,\n  \"language\": \"%s\",\n  \"master\": %.2f, \"music\": %.2f, \"sfx\": %.2f,\n  \"event\": \"%s\", \"car\": \"%s\",\n  \"races_started\": %d, \"races_failed\": %d, \"last_track\": \"%s\"\n}\n",
                    menu.SettingsOpen() ? "true" : "false", settings.language.c_str(), settings.master_volume, settings.music_volume, settings.sfx_volume,
                    menu.SelectedEvent().id.c_str(), menu.SelectedCar().c_str(), races_started, races_failed, last_track.c_str());
                std::string report_text = buf;
                report_text.pop_back(); report_text.pop_back(); report_text.pop_back(); // "\n}\n"
                report_text += ",\n  \"bindings\": " + nlohmann::json(settings.bindings).dump() + ",\n  \"ui_scale\": " + std::to_string(settings.ui_scale) +
                               ", \"high_contrast\": " + (settings.high_contrast ? "true" : "false") + ", \"motion_blur\": " + (settings.motion_blur ? "true" : "false") +
                               ", \"stick_deadzone\": " + std::to_string(settings.stick_deadzone) + "\n}\n";
                if (!report_path.empty())
                    WriteText(report_path, report_text);
                DestroyWindow(hwnd);
            }
        }

        if (scripted)
            return 0; // never overwrite the player's settings.json from a test
        WriteText(settings_path, SerializeSettings(settings));
        if (!report_path.empty())
        {
            std::string j = "{\n  \"cycles\": " + std::to_string(cycles_done) + ",\n  \"menu_memory_mib\": [";
            for (size_t i = 0; i < memory.size(); ++i)
                j += (i ? ", " : "") + std::string("[") + std::to_string(memory[i].first) + ", " + std::to_string(memory[i].second) + "]";
            // Leak gate (D-031): the heap high-water mark rises over the first two passes through
            // the car list (measured: flat from ~cycle 10). Compare the medians of the third and
            // fourth quarters: one-off steps don't move a median, a real 2 MiB/cycle leak moves it
            // ~8 MiB. Needs >= 4 passes (16 cycles with 4 cars); shorter runs only report.
            bool leak_ok = true;
            double growth = 0.0, vram_growth = 0.0;
            if (cycles > 0 && memory.size() >= 4 * own_cars.size())
            {
                const size_t q = memory.size() / 4;
                // CPU-side memory = private bytes minus VRAM: with 25 imported cars the private bytes
                // move in lock-step with the GPU pool (64 MiB blocks), while RAM - VRAM stayed flat
                // at 965-1008 MiB over 32 cycles (D-031 follow-up).
                auto median_ram = [&](size_t from) {
                    std::vector<double> v;
                    for (size_t i = from; i < from + q; ++i)
                        v.push_back(double(memory[i].first) - double(memory[i].second));
                    std::sort(v.begin(), v.end());
                    return q % 2 ? v[q / 2] : 0.5 * (v[q / 2 - 1] + v[q / 2]);
                };
                growth = median_ram(memory.size() - q) - median_ram(memory.size() - 2 * q);
                // VRAM is a pool of 64 MiB blocks that Wicked keeps (measured 408-600 MiB, floor back to 408
                // at cycle 24 of 32): the median may move by one block; a leak keeps adding blocks.
                auto median_vram = [&](size_t from) {
                    std::vector<uint64_t> v;
                    for (size_t i = from; i < from + q; ++i)
                        v.push_back(memory[i].second);
                    std::sort(v.begin(), v.end());
                    return q % 2 ? double(v[q / 2]) : 0.5 * double(v[q / 2 - 1] + v[q / 2]);
                };
                vram_growth = median_vram(memory.size() - q) - median_vram(memory.size() - 2 * q);
                leak_ok = growth <= 4.0 && vram_growth <= 64.0;
            }
            j += "],\n  \"cpu_growth_last_quarter_mib\": " + std::to_string(growth) + ",\n  \"vram_growth_last_quarter_mib\": " + std::to_string(vram_growth);
            j += std::string(",\n  \"leak_gate\": \"") + (leak_ok ? "PASS" : "FAIL") + "\"\n}\n";
            WriteText(report_path, j);
            if (!leak_ok)
                return 3;
        }
        return 0;
    }
}

int APIENTRY wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ LPWSTR cmdline, _In_ int)
{
    static auto WndProc = [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
        switch (msg)
        {
        case WM_SIZE:
        case WM_DPICHANGED:
            if (application.is_window_active)
                application.SetWindow(hwnd);
            break;
        case WM_INPUT:
            wi::input::rawinput::ParseMessage((void*)lp);
            break;
        case WM_KILLFOCUS:
            application.is_window_active = false;
            break;
        case WM_SETFOCUS:
            application.is_window_active = true;
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hwnd, msg, wp, lp);
        }
        return 0;
    };

    const std::wstring cmd = cmdline ? cmdline : L"";
    const bool handling = cmd.find(L"--handling") != std::wstring::npos;
    const std::string autotest = Arg(cmd, L"--autotest=");
    const float autotest_seconds = autotest.empty() ? 0.0f : float(std::atof(autotest.c_str()));
    const std::string report = Arg(cmd, L"--report=");
    const std::string screenshot = Arg(cmd, L"--screenshot=");

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"RacerSandbox";
    RegisterClassExW(&wc);
    RECT rect = { 0, 0, 1600, 900 };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowW(wc.lpszClassName, handling ? L"Racer - handling sandbox (M1)" : L"Racer - race (M2)", WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, 0, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, nullptr);
    ShowWindow(hwnd, SW_SHOWDEFAULT);

    application.SetWindow(hwnd);
    // Engine overlay (FPS, VRAM) only for developers; the game itself shows just the HUD.
    application.infoDisplay.active = handling || cmd.find(L"--debug-overlay") != std::wstring::npos;
    application.infoDisplay.fpsinfo = true;
    application.infoDisplay.resolution = true;
    application.infoDisplay.vram_usage = true;

    int exit_code = 0;
    if (handling)
    {
        SandboxPath::Options o;
        o.content_dir = RACER_CONTENT_DIR;
        o.cooked_dir = RACER_COOKED_DIR;
        o.autotest_seconds = autotest_seconds;
        o.report_path = report;
        o.screenshot_path = screenshot;
        SandboxPath path(o);
        exit_code = Run(hwnd, path, autotest_seconds, screenshot, !report.empty());
    }
    else if (autotest_seconds <= 0 || cmd.find(L"--flow-cycles=") != std::wstring::npos || cmd.find(L"--menu-script=") != std::wstring::npos)
    {
        const std::string cycles = Arg(cmd, L"--flow-cycles=");
        exit_code = RunGameFlow(hwnd, cycles.empty() ? 0 : std::atoi(cycles.c_str()), report, Arg(cmd, L"--menu-script="), screenshot);
    }
    else
    {
        RacePath::Options o;
        o.content_dir = RACER_CONTENT_DIR;
        o.cooked_dir = RACER_COOKED_DIR;
        o.autotest_seconds = autotest_seconds;
        o.report_path = report;
        const std::string opponents = Arg(cmd, L"--opponents=");
        if (const std::string car = Arg(cmd, L"--car="); !car.empty())
            o.car = car;
        if (cmd.find(L"--pad") != std::wstring::npos)
            o.track = "pad";
        if (const std::string track = Arg(cmd, L"--track="); !track.empty())
            o.track = track;
        if (const std::string s = Arg(cmd, L"--ui-scale="); !s.empty())
            o.ui_scale = float(std::atof(s.c_str()));
        o.high_contrast = cmd.find(L"--high-contrast") != std::wstring::npos;
        o.motion_blur = cmd.find(L"--no-motion-blur") == std::wstring::npos;
        o.reduce_shake = cmd.find(L"--reduce-shake") != std::wstring::npos;
        o.time_of_day = Arg(cmd, L"--time=");
        if (const std::string b = Arg(cmd, L"--perf-budget="); !b.empty()) // mean_ms,p99_ms,ram_mib (D-092)
            std::sscanf(b.c_str(), "%f,%f,%f", &o.budget_mean_ms, &o.budget_p99_ms, &o.budget_ram_mib);
        // --weather=rain or --weather=storm:0.5 (D-083 test hook)
        if (const std::string w = Arg(cmd, L"--weather="); !w.empty())
        {
            const size_t colon = w.find(':');
            racer::ParseWeatherKind(w.substr(0, colon), o.weather.kind);
            if (colon != std::string::npos)
                o.weather.intensity = float(std::atof(w.c_str() + colon + 1));
        }
        o.features = Arg(cmd, L"--features=");
        // --parts=spoiler:gt,rims:mesh (customisation test hook, D-053)
        {
            std::stringstream parts(Arg(cmd, L"--parts="));
            for (std::string item; std::getline(parts, item, ',');)
                if (const size_t colon = item.find(':'); colon != std::string::npos)
                    o.player_parts[item.substr(0, colon)] = item.substr(colon + 1);
        }
        if (!opponents.empty())
            o.opponents = std::atoi(opponents.c_str());
        RacePath path(o);
        exit_code = Run(hwnd, path, autotest_seconds, screenshot, !report.empty());
    }

    wi::jobsystem::ShutDown();
    return exit_code;
}
