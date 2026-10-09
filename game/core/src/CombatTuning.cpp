#include "racer/CombatTuning.h"

#include <nlohmann/json.hpp>

namespace racer
{
    namespace
    {
        using json = nlohmann::json;

        struct Field
        {
            const char* name;
            float CombatTuning::* f = nullptr;
            int CombatTuning::* i = nullptr;
        };

#define RACER_F(n) Field{ #n, &CombatTuning::n, nullptr }
#define RACER_I(n) Field{ #n, nullptr, &CombatTuning::n }
        // Every CombatTuning member, once. CombatTuningFieldCount() lets the content test prove the
        // shipped file sets all of them.
        const Field kFields[] = {
            RACER_F(car_radius_m), RACER_F(reference_mass_kg),
            RACER_F(surge_time_s), RACER_F(surge_accel), RACER_F(surge_back_brake_s), RACER_F(surge_back_decel), RACER_F(surge_back_boost_s),
            RACER_F(lance_damage), RACER_F(lance_speed), RACER_F(lance_turn_rate), RACER_F(lance_life_s), RACER_F(lance_radius),
            RACER_F(lance_knock), RACER_F(lance_target_range), RACER_F(lance_target_cone_rad), RACER_F(lance_route_window), RACER_I(lance_needle_hits),
            RACER_F(pulse_radius), RACER_F(pulse_damage), RACER_F(pulse_knock),
            RACER_F(trap_damage), RACER_F(trap_life_s), RACER_F(trap_arm_s), RACER_F(trap_radius), RACER_F(trap_knock_up),
            RACER_F(trap_throw_m), RACER_F(trap_drop_m), RACER_I(trap_needle_hits),
            RACER_I(needle_shots), RACER_F(needle_damage), RACER_F(needle_speed), RACER_F(needle_life_s), RACER_F(needle_radius), RACER_F(needle_knock),
            RACER_F(ward_time_s), RACER_I(ward_hits),
            RACER_F(mend_heal),
            RACER_I(storm_zones), RACER_F(storm_damage), RACER_F(storm_first_ahead_m), RACER_F(storm_spacing_m), RACER_F(storm_warning_s),
            RACER_F(storm_active_s), RACER_F(storm_zone_length_m), RACER_F(storm_cover_fraction), RACER_F(storm_knock),
            RACER_F(heavy_chain_s), RACER_F(heavy_chain_scale),
            RACER_F(respawn_protect_s), RACER_F(pickup_radius), RACER_F(pickup_respawn_s), RACER_F(owner_grace_s),
        };
#undef RACER_F
#undef RACER_I
    }

    int CombatTuningFieldCount()
    {
        return int(sizeof(kFields) / sizeof(kFields[0]));
    }

    bool ParseCombatTuning(const std::string& text, CombatTuning& out, std::vector<ValidationIssue>& issues)
    {
        const size_t before = issues.size();
        const json root = json::parse(text, nullptr, false);
        if (root.is_discarded() || !root.is_object() || !root.contains("tuning") || !root.at("tuning").is_object())
        {
            issues.push_back({ "$", "expected {\"schema_version\":1,\"tuning\":{...}}" });
            return false;
        }
        if (root.value("schema_version", 0) != 1)
            issues.push_back({ "$.schema_version", "must be 1" });
        CombatTuning t = out;
        for (auto it = root.at("tuning").begin(); it != root.at("tuning").end(); ++it)
        {
            const std::string path = "$.tuning." + it.key();
            const Field* field = nullptr;
            for (const Field& f : kFields)
                if (it.key() == f.name)
                    field = &f;
            if (!field)
            {
                issues.push_back({ path, "unknown field" });
                continue;
            }
            if (!it.value().is_number())
            {
                issues.push_back({ path, "expected a number" });
                continue;
            }
            const double v = it.value().get<double>();
            // All combat quantities are positive (0 would e.g. disable a whole ability silently).
            if (!(v > 0.0) || v > 100000.0)
            {
                issues.push_back({ path, "must be > 0" });
                continue;
            }
            if (field->i)
            {
                if (v != double(int(v)))
                    issues.push_back({ path, "expected a whole number" });
                else
                    t.*(field->i) = int(v);
            }
            else
                t.*(field->f) = float(v);
        }
        if (t.storm_cover_fraction >= 1.0f)
            issues.push_back({ "$.tuning.storm_cover_fraction", "must be < 1 (plan 2.8: a Storm always leaves a safe lane)" });
        if (t.heavy_chain_scale > 1.0f)
            issues.push_back({ "$.tuning.heavy_chain_scale", "must be <= 1 (it reduces chained knocks)" });
        if (issues.size() != before)
            return false;
        out = t;
        return true;
    }
}
