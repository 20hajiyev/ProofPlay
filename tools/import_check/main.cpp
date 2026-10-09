// Imports a GLB through Wicked's own glTF importer.
//   import_check <file.glb>                  prints world positions of named entities as JSON
//   import_check --leak-test <N> <file.glb>  imports N times and fails if RAM or VRAM keeps growing
#include <algorithm>
#include <vector>
#include "WickedEngine.h"
#include "wiGraphicsDevice_DX12.h"
#include "ModelImporter.h"

#include <windows.h>
#include <psapi.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace
{
    uint64_t PrivateBytes()
    {
        PROCESS_MEMORY_COUNTERS_EX counters{};
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
        return counters.PrivateUsage;
    }

    // Mirrors wi::Application's frame end. Deferred GPU deletions and mesh suballocation
    // releases only happen here; skipping UpdateGPUSuballocator leaks ~12 MiB per D01 import.
    void FlushGpuFrames(wi::graphics::GraphicsDevice* device)
    {
        for (uint32_t i = 0; i < device->GetBufferCount() + 1; ++i)
        {
            device->BeginCommandList();
            device->SubmitCommandLists();
            wi::renderer::UpdateGPUSuballocator();
        }
        device->WaitForGPU();
    }

    int PrintEntities(const char* file)
    {
        wi::scene::Scene scene;
        ImportModel_GLTF(file, scene);
        std::printf("{\n  \"file\": \"%s\",\n  \"meshes\": %zu,\n  \"entities\": {", file, scene.meshes.GetCount());
        bool first = true;
        for (size_t i = 0; i < scene.names.GetCount(); ++i)
        {
            wi::ecs::Entity entity = scene.names.GetEntity(i);
            if (!scene.transforms.Contains(entity))
                continue;
            XMFLOAT4X4 world;
            XMStoreFloat4x4(&world, scene.ComputeEntityMatrixRecursive(entity));
            std::printf("%s\n    \"%s\": [%.4f, %.4f, %.4f]", first ? "" : ",", scene.names[i].name.c_str(), world._41, world._42, world._43);
            first = false;
        }
        std::printf("\n  }\n}\n");
        return 0;
    }

    // Cooks a GLB into Wicked's native scene archive, the runtime format the game loads.
    int Cook(const char* in, const char* out)
    {
        // Default mode drops image file data after upload, so GLB-embedded textures would be
        // written as bare names and load as missing (white albedo) at runtime.
        wi::resourcemanager::SetMode(wi::resourcemanager::Mode::EMBED_FILE_DATA);
        wi::scene::Scene scene;
        ImportModel_GLTF(in, scene);
        if (scene.meshes.GetCount() == 0)
        {
            std::fprintf(stderr, "cook failed: %s produced no meshes\n", in);
            return 1;
        }
        wi::Archive archive;
        scene.Serialize(archive);
        // Atomic (D-090): write beside the target, then swap it in. A game started while a track
        // or car was being re-cooked could read a half-written archive - the road missing and
        // broken triangles across the scene (owner's screenshots, 2026-10-04).
        const std::string tmp = std::string(out) + ".tmp";
        if (!archive.SaveFile(tmp))
        {
            std::fprintf(stderr, "cook failed: cannot write %s\n", tmp.c_str());
            return 1;
        }
        if (!MoveFileExA(tmp.c_str(), out, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            std::fprintf(stderr, "cook failed: cannot replace %s (in use?)\n", out);
            return 1;
        }
        std::printf("cooked %s -> %s (%zu meshes, %zu materials)\n", in, out, scene.meshes.GetCount(), scene.materials.GetCount());
        return 0;
    }

    int LeakTest(wi::graphics::GraphicsDevice* device, int iterations, const char* file)
    {
        // Growth = median of the last quarter of samples minus median of the third quarter (after a
        // warm-up), as the game's flow gate does (D-031): a leak climbs steadily and still shows,
        // while the allocator's high-water steps with a big model (the ~29k-face toon cars, D-059)
        // no longer read as one (measured last-minus-first: 0.5, 11.4, 9.5 MiB at 60, 200, 400 imports).
        constexpr int kWarmup = 10;
        constexpr double kMaxRamGrowthMiB = 4.0;
        constexpr double kMaxVramGrowthMiB = 1.0;
        // gate self-check: RACER_INJECT_LEAK_KIB=<n> leaks n KiB per import, so the gate can be seen failing
        const char* inject = std::getenv("RACER_INJECT_LEAK_KIB");
        const size_t inject_bytes = inject ? size_t(std::atoi(inject)) * 1024 : 0;
        std::vector<char*> injected;
        std::vector<double> ram_s, vram_s;
        for (int i = 0; i < kWarmup + iterations; ++i)
        {
            {
                wi::scene::Scene scene;
                ImportModel_GLTF(file, scene);
            }
            if (inject_bytes)
            {
                injected.push_back(new char[inject_bytes]);
                std::memset(injected.back(), 1, inject_bytes); // touch the pages so they count
            }
            wi::resourcemanager::Clear();
            FlushGpuFrames(device);
            if (i >= kWarmup)
            {
                ram_s.push_back(double(PrivateBytes()) / (1024.0 * 1024.0));
                vram_s.push_back(double(device->GetMemoryUsage().usage) / (1024.0 * 1024.0));
            }
        }
        auto median = [](std::vector<double> v) { std::sort(v.begin(), v.end()); return v.empty() ? 0.0 : v[v.size() / 2]; };
        auto quarter_growth = [&](const std::vector<double>& v) {
            const size_t q = std::max<size_t>(1, v.size() / 4);
            auto quarter = [&](size_t k) { return median(std::vector<double>(v.begin() + k * q, v.begin() + (k + 1) * q)); };
            // A leak climbs in every quarter; the allocator's one-off high-water step lands in one
            // (toon D01: +8 MiB once between 200 and 400 imports, flat to 800). With the ~55k-face cars
            // (D-080) such a step fell between the third and fourth quarter of a 60-import run in 2 of
            // 4 runs (16.3, 13.9 vs -0.3, 2.2 MiB), so growth is the smaller of the last two steps (D-081).
            return std::min(quarter(3) - quarter(2), quarter(2) - quarter(1));
        };
        const double ram = quarter_growth(ram_s);
        const double vram = quarter_growth(vram_s);
        const bool ok = ram <= kMaxRamGrowthMiB && vram <= kMaxVramGrowthMiB;
        std::printf("%s leak-test %s: %d imports, RAM growth %.2f MiB (limit %.1f), VRAM growth %.2f MiB (limit %.1f)\n",
            ok ? "PASS" : "FAIL", file, iterations, ram, kMaxRamGrowthMiB, vram, kMaxVramGrowthMiB);
        return ok ? 0 : 1;
    }
}

int main(int argc, char** argv)
{
    const bool leak = argc == 4 && std::strcmp(argv[1], "--leak-test") == 0;
    const bool cook = argc == 4 && std::strcmp(argv[1], "--cook") == 0;
    if (!(argc == 2 || leak || cook))
    {
        std::fprintf(stderr, "usage: import_check <file.glb> | --leak-test <N> <file.glb> | --cook <in.glb> <out.wiscene>\n");
        return 2;
    }
    const char* file = cook ? argv[2] : argv[argc - 1];
    if (!wi::helper::FileExists(file))
    {
        std::fprintf(stderr, "file not found: %s\n", file);
        return 2;
    }

    wi::jobsystem::Initialize();
    auto device = std::make_unique<wi::graphics::GraphicsDevice_DX12>();
    wi::graphics::GetDevice() = device.get();

    const int rc = cook ? Cook(file, argv[3]) : leak ? LeakTest(device.get(), std::atoi(argv[2]), file) : PrintEntities(file);

    device->WaitForGPU();
    wi::graphics::GetDevice() = nullptr;
    device.reset();
    wi::jobsystem::ShutDown();
    return rc;
}
