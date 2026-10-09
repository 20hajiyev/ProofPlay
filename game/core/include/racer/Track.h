#pragma once
#include "racer/Combat.h"
#include "racer/Race.h"
#include "racer/Vehicle.h"

#include <string>
#include <vector>

namespace racer
{
    struct GridPose
    {
        float x = 0, z = 0, yaw = 0;
    };

    struct PickupPlacement
    {
        float x = 0, z = 0;
        Ability ability = Ability::None;
    };

    // Authored layout package (plan 2.11). Ground coordinates: +X right, +Z forward, metres.
    struct TrackDefinition
    {
        std::string id, name, region, visual_asset;
        float half_width_m = 0;
        float wall_offset_m = 0;
        std::vector<Vec2> centerline;
        std::vector<float> checkpoints_s;
        std::vector<GridPose> grid;
        std::vector<PickupPlacement> pickups;
        std::vector<float> elevation; // optional, one height per centreline point (D-059)
        std::vector<int> drops;       // optional, jump lips (indices into centerline)

        TrackRoute Route() const { return TrackRoute(centerline, half_width_m, elevation, drops); }
    };

    bool ParseTrackDefinition(const std::string& json_text, TrackDefinition& out, std::vector<ValidationIssue>& issues);

    // Layout rules that need the route geometry (plan 2.11): road width, 20-car grid behind the
    // line on the road without overlaps, pickups on the road, checkpoints valid. Appends issues.
    bool ValidateTrackLayout(const TrackDefinition& track, std::vector<ValidationIssue>& issues);

    Ability AbilityFromString(const std::string& name);
}
