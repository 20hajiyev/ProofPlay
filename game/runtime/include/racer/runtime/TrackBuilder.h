#pragma once
#include "racer/Track.h"
#include "racer/runtime/RaceSession.h"
#include "wiScene.h"

namespace racer::runtime
{
    // Physics for an authored layout, built from the same centreline the rules and AI use:
    // one flat seamless drive surface at y = 0 and continuous walls on both sides whose faces sit
    // wall_offset_m beyond the road edge (matching the Blender barrier line). No visuals.
    struct TrackPhysicsStats
    {
        size_t wall_boxes = 0;
        size_t deck_boxes = 0; // raised road segments (D-059)
        float ground_extent_x = 0, ground_extent_z = 0;
    };
    TrackPhysicsStats BuildTrackPhysics(wi::scene::Scene& scene, const TrackDefinition& track);

    RaceSetup MakeTrackRaceSetup(const TrackDefinition& track, const std::vector<VehicleDefinition>& cars, int laps);
}
