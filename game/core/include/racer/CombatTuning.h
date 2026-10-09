#pragma once
#include "racer/Combat.h"
#include "racer/Vehicle.h" // ValidationIssue

#include <string>
#include <vector>

namespace racer
{
    // content/combat/tuning.json -> CombatTuning (plan 2.8: combat numbers are data, tuned by
    // playtesting without a rebuild). Strict: an unknown key (a typo) or a non-positive value is an
    // error, never silently ignored; keys left out keep the code defaults.
    bool ParseCombatTuning(const std::string& json_text, CombatTuning& out, std::vector<ValidationIssue>& issues);
    int CombatTuningFieldCount();
}
