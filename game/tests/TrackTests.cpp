#include "TestHarness.h"
#include "racer/Track.h"

#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

using namespace racer;
using nlohmann::json;

namespace
{
    std::string S01Text()
    {
        std::ifstream f(RACER_CONTENT_DIR "/tracks/S01_dock_loop.json", std::ios::binary);
        std::stringstream s;
        s << f.rdbuf();
        return s.str();
    }

    bool Has(const std::vector<ValidationIssue>& issues, const std::string& prefix)
    {
        for (auto& i : issues)
            if (i.path.rfind(prefix, 0) == 0)
                return true;
        return false;
    }

    std::vector<ValidationIssue> ParseModified(void (*mutate)(json&))
    {
        json j = json::parse(S01Text());
        mutate(j);
        TrackDefinition d;
        std::vector<ValidationIssue> issues;
        ParseTrackDefinition(j.dump(), d, issues);
        return issues;
    }
}

TEST_CASE("track: S01 Dock Loop definition is valid and matches plan 2.11")
{
    TrackDefinition d;
    std::vector<ValidationIssue> issues;
    const bool ok = ParseTrackDefinition(S01Text(), d, issues);
    for (auto& i : issues)
        std::printf("  ISSUE %s: %s\n", i.path.c_str(), i.message.c_str());
    CHECK(ok);
    const float len = d.Route().Length();
    std::printf("  INFO S01: %.0f m, width %.0f m, %zu grid slots, %zu pickups, %zu checkpoints\n", len, 2 * d.half_width_m, d.grid.size(), d.pickups.size(), d.checkpoints_s.size());
    CHECK(len > 2200 * 0.95f && len < 2200 * 1.05f); // plan: S01 Dock Loop, 2.2 km
    CHECK(2 * d.half_width_m >= 12.0f && 2 * d.half_width_m <= 18.0f);
    CHECK(d.grid.size() >= 20);
    CHECK(d.region == "R01");
}

TEST_CASE("track: unknown field, bad ability and narrow road are rejected")
{
    CHECK(Has(ParseModified([](json& j) { j["surprise"] = 1; }), "$.surprise"));
    CHECK(Has(ParseModified([](json& j) { j["pickups"][0][2] = "Nuke"; }), "$.pickups[0]"));
    CHECK(Has(ParseModified([](json& j) { j["half_width_m"] = 3.0; }), "$.half_width_m"));
}

TEST_CASE("track: overlapping grid slots and off-road pickups are rejected")
{
    CHECK(Has(ParseModified([](json& j) { j["grid"][1] = j["grid"][0]; }), "$.grid[0]"));
    CHECK(Has(ParseModified([](json& j) { j["pickups"][0][0] = j["pickups"][0][0].get<double>() + 40.0; }), "$.pickups[0]"));
}

TEST_CASE("track: a layout whose sections overlap is rejected")
{
    // Pinch two opposite sides of the loop together: walls would intersect.
    auto issues = ParseModified([](json& j) {
        auto& c = j["centerline"];
        const size_t n = c.size();
        for (size_t i = n / 8; i < n * 3 / 8; ++i)
            c[i][1] = c[i][1].get<double>() * 0.02;
        for (size_t i = n * 5 / 8; i < n * 7 / 8; ++i)
            c[i][1] = c[i][1].get<double>() * 0.02;
    });
    CHECK(Has(issues, "$.centerline"));
}

// --- Elevation (D-059): ramps, jumps and hump bridges on the drive surface.
TEST_CASE("track: elevation and jump drops parse; old tracks are flat")
{
    TrackDefinition flat;
    std::vector<ValidationIssue> issues;
    CHECK(ParseTrackDefinition(S01Text(), flat, issues));
    const TrackRoute flat_route = flat.Route();
    for (float s : { 0.0f, 300.0f, 1000.0f })
        CHECK(flat_route.ElevationAt(s) == 0.0f);

    json j = json::parse(S01Text());
    const size_t n = j["centerline"].size();
    std::vector<float> e(n, 0.0f);
    e[10] = 1.0f; e[11] = 2.0f; e[12] = 2.0f; // ramp up to a 2 m lip at 12, then a drop
    j["elevation"] = e;
    j["drops"] = { 12 };
    TrackDefinition d;
    issues.clear();
    CHECK(ParseTrackDefinition(j.dump(), d, issues));
    CHECK(d.elevation.size() == n && d.drops.size() == 1 && d.drops[0] == 12);
}

TEST_CASE("track: bad elevation data is rejected (wrong length, negative, drop at ground level)")
{
    CHECK(Has(ParseModified([](json& j) { j["elevation"] = { 1.0, 2.0 }; }), "$.elevation"));
    CHECK(Has(ParseModified([](json& j) { std::vector<float> e(j["centerline"].size(), 0.0f); e[3] = -1.0f; j["elevation"] = e; }), "$.elevation"));
    CHECK(Has(ParseModified([](json& j) { std::vector<float> e(j["centerline"].size(), 0.0f); j["elevation"] = e; j["drops"] = { 5 }; }), "$.drops"));
}

TEST_CASE("track: ElevationAt interpolates along a ramp and is ground level past a drop lip")
{
    std::vector<Vec2> line;
    for (int i = 0; i < 40; ++i)
        line.push_back({ float(i) * 10.0f, 0.0f });
    for (int i = 39; i >= 1; --i)
        line.push_back({ float(i) * 10.0f, 200.0f }); // closed loop back along a parallel line
    std::vector<float> e(line.size(), 0.0f);
    e[3] = 1.0f; e[4] = 2.0f;                         // 30 m -> 40 m rises to 2 m, lip at point 4
    TrackRoute r(line, 7.0f, e, { 4 });
    CHECK(std::fabs(r.ElevationAt(25.0f) - 0.5f) < 1e-3f); // half way up the first ramp segment
    CHECK(std::fabs(r.ElevationAt(40.0f) - 2.0f) < 1e-3f); // the lip
    CHECK(r.ElevationAt(45.0f) == 0.0f);                    // over the gap: the ground below
    CHECK(!r.DeckOnSegment(4) && r.DeckOnSegment(3) && r.DeckOnSegment(2) && !r.DeckOnSegment(10));
}
