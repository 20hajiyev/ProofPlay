#include "racer/Track.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <nlohmann/json.hpp>

namespace racer
{
    using nlohmann::json;

    Ability AbilityFromString(const std::string& name)
    {
        for (int a = 1; a < int(Ability::Count); ++a)
            if (name == ToString(Ability(a)))
                return Ability(a);
        return Ability::None;
    }

    namespace
    {
        void Issue(std::vector<ValidationIssue>& issues, std::string path, std::string msg) { issues.push_back({ std::move(path), std::move(msg) }); }

        bool Finite(const json& v) { return v.is_number() && std::isfinite(v.get<double>()); }

        // Reads an array of fixed-size numeric tuples; optional trailing string (pickup ability).
        bool Tuples(const json& root, const char* key, size_t numbers, bool trailing_string, std::vector<std::vector<double>>& out,
                    std::vector<std::string>& strings, std::vector<ValidationIssue>& issues)
        {
            const std::string path = std::string("$.") + key;
            if (!root.contains(key) || !root[key].is_array() || root[key].empty())
            {
                Issue(issues, path, "expected a non-empty array");
                return false;
            }
            bool ok = true;
            size_t i = 0;
            for (const json& t : root[key])
            {
                const std::string p = path + "[" + std::to_string(i++) + "]";
                const size_t want = numbers + (trailing_string ? 1 : 0);
                if (!t.is_array() || t.size() != want)
                {
                    Issue(issues, p, "expected " + std::to_string(want) + " values");
                    ok = false;
                    continue;
                }
                std::vector<double> row;
                for (size_t k = 0; k < numbers; ++k)
                {
                    if (!Finite(t[k]))
                    {
                        Issue(issues, p, "non-finite number");
                        ok = false;
                    }
                    row.push_back(t[k].is_number() ? t[k].get<double>() : 0.0);
                }
                out.push_back(row);
                if (trailing_string)
                    strings.push_back(t[numbers].is_string() ? t[numbers].get<std::string>() : std::string());
            }
            return ok;
        }
    }

    bool ParseTrackDefinition(const std::string& text, TrackDefinition& d, std::vector<ValidationIssue>& issues)
    {
        const size_t before = issues.size();
        const json root = json::parse(text, nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            Issue(issues, "$", "invalid JSON object");
            return false;
        }
        // Not a function-local static container: that allocates once and outlives every test's heap check.
        constexpr const char* known[] = { "schema_version", "id", "name", "region", "visual_asset", "half_width_m",
                                          "wall_offset_m", "centerline", "checkpoints_s", "grid", "pickups", "elevation", "drops" };
        for (auto it = root.begin(); it != root.end(); ++it)
            if (std::none_of(std::begin(known), std::end(known), [&](const char* k) { return it.key() == k; }))
                Issue(issues, "$." + it.key(), "unknown field");
        if (!root.contains("schema_version") || root["schema_version"] != 1)
            Issue(issues, "$.schema_version", "must be 1");
        auto str = [&](const char* k, std::string& out) {
            if (root.contains(k) && root[k].is_string() && !root[k].get<std::string>().empty())
                out = root[k].get<std::string>();
            else
                Issue(issues, std::string("$.") + k, "expected a non-empty string");
        };
        auto num = [&](const char* k, float lo, float hi, float& out) {
            if (!root.contains(k) || !Finite(root[k]))
                return Issue(issues, std::string("$.") + k, "expected a number");
            out = root[k].get<float>();
            if (!(out >= lo && out <= hi))
                Issue(issues, std::string("$.") + k, "out of range");
        };
        str("id", d.id);
        str("name", d.name);
        str("region", d.region);
        str("visual_asset", d.visual_asset);
        num("half_width_m", 4.0f, 9.0f, d.half_width_m); // plan 2.11: at least 8 m wide, main road 12-18 m
        num("wall_offset_m", 0.5f, 10.0f, d.wall_offset_m);

        std::vector<std::vector<double>> rows;
        std::vector<std::string> names;
        if (Tuples(root, "centerline", 2, false, rows, names, issues))
            for (auto& r : rows)
                d.centerline.push_back({ float(r[0]), float(r[1]) });
        rows.clear();
        if (Tuples(root, "grid", 3, false, rows, names, issues))
            for (auto& r : rows)
                d.grid.push_back({ float(r[0]), float(r[1]), float(r[2]) });
        rows.clear();
        names.clear();
        if (Tuples(root, "pickups", 2, true, rows, names, issues))
            for (size_t i = 0; i < rows.size(); ++i)
            {
                const Ability a = AbilityFromString(names[i]);
                if (a == Ability::None)
                    Issue(issues, "$.pickups[" + std::to_string(i) + "]", "unknown ability '" + names[i] + "'");
                d.pickups.push_back({ float(rows[i][0]), float(rows[i][1]), a });
            }
        // Elevation (D-059): optional; one finite height 0..30 m per centreline point. Drops mark
        // jump lips and must stand on a raised point.
        if (root.contains("elevation"))
        {
            const json& e = root["elevation"];
            if (!e.is_array() || e.size() != d.centerline.size())
                Issue(issues, "$.elevation", "expected one height per centreline point");
            else
                for (size_t i = 0; i < e.size(); ++i)
                {
                    const float h = Finite(e[i]) ? e[i].get<float>() : -1.0f;
                    if (!(h >= 0.0f && h <= 30.0f))
                        Issue(issues, "$.elevation[" + std::to_string(i) + "]", "expected 0..30 m");
                    d.elevation.push_back(h);
                }
        }
        if (root.contains("drops"))
        {
            const json& dr = root["drops"];
            if (!dr.is_array())
                Issue(issues, "$.drops", "expected an array of point indices");
            else
                for (const json& v : dr)
                {
                    const int i = v.is_number_integer() ? v.get<int>() : -1;
                    if (i < 0 || size_t(i) >= d.elevation.size() || d.elevation[size_t(i)] <= 0.0f)
                        Issue(issues, "$.drops", "a drop must be a raised centreline point");
                    d.drops.push_back(i);
                }
        }
        if (root.contains("checkpoints_s") && root["checkpoints_s"].is_array())
            for (const json& v : root["checkpoints_s"])
                d.checkpoints_s.push_back(Finite(v) ? v.get<float>() : -1.0f);
        else
            Issue(issues, "$.checkpoints_s", "expected an array");

        if (issues.size() == before)
            ValidateTrackLayout(d, issues);
        return issues.size() == before;
    }

    bool ValidateTrackLayout(const TrackDefinition& d, std::vector<ValidationIssue>& issues)
    {
        const size_t before = issues.size();
        if (d.centerline.size() < 16)
        {
            Issue(issues, "$.centerline", "needs at least 16 points");
            return false;
        }
        for (size_t i = 0; i < d.centerline.size(); ++i)
        {
            const Vec2 a = d.centerline[i], b = d.centerline[(i + 1) % d.centerline.size()];
            const float seg = std::hypot(b.x - a.x, b.z - a.z);
            if (seg < 0.1f || seg > 30.0f)
                Issue(issues, "$.centerline[" + std::to_string(i) + "]", "segment length must be 0.1-30 m");
        }
        if (issues.size() != before)
            return false;

        const TrackRoute route = d.Route();
        RaceConfig rc;
        rc.checkpoints_s = d.checkpoints_s;
        if (const std::string e = ValidateRaceConfig(route, rc); !e.empty())
            Issue(issues, "$.checkpoints_s", e);

        // Separate sections of road (and their walls) must never overlap.
        const float clearance = 2.0f * (d.half_width_m + d.wall_offset_m) + 2.0f;
        const size_t n = d.centerline.size();
        std::vector<float> s_at(n, 0.0f);
        for (size_t i = 1; i < n; ++i)
            s_at[i] = s_at[i - 1] + std::hypot(d.centerline[i].x - d.centerline[i - 1].x, d.centerline[i].z - d.centerline[i - 1].z);
        for (size_t i = 0; i < n; ++i)
            for (size_t j = i + 1; j < n; ++j)
            {
                const float along = std::min(route.Ahead(s_at[i], s_at[j]), route.Ahead(s_at[j], s_at[i]));
                if (along < clearance * 3.0f)
                    continue;
                const float gap = std::hypot(d.centerline[i].x - d.centerline[j].x, d.centerline[i].z - d.centerline[j].z);
                if (gap < clearance)
                {
                    Issue(issues, "$.centerline[" + std::to_string(i) + "]", "passes within " + std::to_string(int(gap)) + " m of another section");
                    i = n; // one report is enough
                    break;
                }
            }

        // Grid: 20 cars, on the road, behind the line, facing the route, not overlapping.
        if (d.grid.size() < 20)
            Issue(issues, "$.grid", "needs 20 slots (plan 2.11)");
        for (size_t i = 0; i < d.grid.size(); ++i)
        {
            const std::string p = "$.grid[" + std::to_string(i) + "]";
            const GridPose& g = d.grid[i];
            const TrackRoute::Projection pr = route.Project({ g.x, g.z });
            if (std::fabs(pr.lateral) > d.half_width_m - 1.0f)
                Issue(issues, p, "not on the road");
            const float behind = route.Ahead(pr.s, 0.0f);
            if (behind < 2.0f || behind > 300.0f)
                Issue(issues, p, "must be 2-300 m behind the start line");
            const float route_yaw = std::atan2(pr.direction.x, pr.direction.z);
            float diff = std::fabs(g.yaw - route_yaw);
            diff = std::fmin(diff, 6.2831853f - diff);
            if (diff > 0.18f)
                Issue(issues, p, "not facing along the route");
            for (size_t j = i + 1; j < d.grid.size(); ++j)
                if (std::hypot(g.x - d.grid[j].x, g.z - d.grid[j].z) < 4.0f)
                    Issue(issues, p, "overlaps slot " + std::to_string(j));
        }
        for (size_t i = 0; i < d.pickups.size(); ++i)
            if (std::fabs(route.Project({ d.pickups[i].x, d.pickups[i].z }).lateral) > d.half_width_m - 0.5f)
                Issue(issues, "$.pickups[" + std::to_string(i) + "]", "not on the road");
        return issues.size() == before;
    }
}
