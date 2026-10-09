#include "racer/runtime/TrackBuilder.h"
#include "racer/runtime/HandlingTrack.h"

#include "wiECS.h"

#include <algorithm>
#include <cmath>

using namespace wi::ecs;
using namespace wi::scene;

namespace racer::runtime
{
    namespace
    {
        Entity StaticBox(Scene& scene, const char* name, XMFLOAT3 center, XMFLOAT3 half, float yaw, float grip, float pitch = 0.0f)
        {
            Entity e = CreateEntity();
            scene.names.Create(e) = name;
            TransformComponent& t = scene.transforms.Create(e);
            t.Scale(half);
            t.RotateRollPitchYaw(XMFLOAT3(pitch, yaw, 0));
            t.Translate(center);
            t.UpdateTransform();
            RigidBodyPhysicsComponent& rb = scene.rigidbodies.Create(e);
            rb.shape = RigidBodyPhysicsComponent::BOX;
            rb.box.halfextents = XMFLOAT3(1, 1, 1);
            rb.mass = 0.0f;
            rb.friction = grip;
            rb.restitution = 0.0f;
            return e;
        }
    }

    TrackPhysicsStats BuildTrackPhysics(Scene& scene, const TrackDefinition& track)
    {
        TrackPhysicsStats stats;
        float min_x = 1e9f, max_x = -1e9f, min_z = 1e9f, max_z = -1e9f;
        for (const Vec2& p : track.centerline)
        {
            min_x = std::min(min_x, p.x); max_x = std::max(max_x, p.x);
            min_z = std::min(min_z, p.z); max_z = std::max(max_z, p.z);
        }
        const float margin = 100.0f;
        stats.ground_extent_x = (max_x - min_x) * 0.5f + margin;
        stats.ground_extent_z = (max_z - min_z) * 0.5f + margin;
        StaticBox(scene, "track_ground", XMFLOAT3((min_x + max_x) * 0.5f, -0.5f, (min_z + max_z) * 0.5f),
                  XMFLOAT3(stats.ground_extent_x, 0.5f, stats.ground_extent_z), 0.0f, surface::kDryAsphalt);

        // Walls along offset polylines: each box spans consecutive offset points, lengthened a
        // little so neighbours overlap and no gap opens on the outside of a corner.
        const TrackRoute route = track.Route();
        const size_t n = track.centerline.size();
        auto h_at = [&](size_t i) { return track.elevation.empty() ? 0.0f : track.elevation[i % n]; };
        // Raised drive surface (D-059): one pitched deck box per raised segment, as wide as road plus
        // run-off to the walls; its top passes through both points' heights. A segment after a
        // drop has no deck: that is the jump gap.
        for (size_t i = 0; i < n && route.HasElevation(); ++i)
        {
            if (!route.DeckOnSegment(i))
                continue;
            const Vec2 a = track.centerline[i], b = track.centerline[(i + 1) % n];
            const float ha = h_at(i), hb = h_at(i + 1);
            const float len = std::hypot(b.x - a.x, b.z - a.z);
            const float yaw = std::atan2(b.x - a.x, b.z - a.z);
            const float pitch = -std::atan2(hb - ha, len); // +pitch tips +Z down (left-handed)
            const float half_t = 0.3f;
            StaticBox(scene, "track_deck", XMFLOAT3((a.x + b.x) * 0.5f, (ha + hb) * 0.5f - half_t, (a.z + b.z) * 0.5f),
                      XMFLOAT3(track.half_width_m + track.wall_offset_m + 0.6f, half_t, std::hypot(len, hb - ha) * 0.5f + 0.05f), yaw,
                      surface::kDryAsphalt, pitch);
            ++stats.deck_boxes;
        }
        const float thickness = 0.25f, height = 0.6f;
        auto track_height = [&](size_t i) { return track.elevation.empty() ? 0.0f : track.elevation[i % n]; };
        for (float side : { -1.0f, 1.0f })
        {
            const float offset = track.half_width_m + track.wall_offset_m + thickness;
            std::vector<Vec2> ring(n);
            for (size_t i = 0; i < n; ++i)
            {
                const Vec2 a = track.centerline[(i + n - 1) % n], b = track.centerline[(i + 1) % n];
                float dx = b.x - a.x, dz = b.z - a.z;
                const float l = std::max(1e-4f, std::hypot(dx, dz));
                dx /= l; dz /= l;
                // right of travel = (dz, -dx)
                ring[i] = { track.centerline[i].x + dz * offset * side, track.centerline[i].z - dx * offset * side };
            }
            for (size_t i = 0; i < n; ++i)
            {
                const Vec2 a = ring[i], b = ring[(i + 1) % n];
                const float len = std::hypot(b.x - a.x, b.z - a.z);
                const float yaw = std::atan2(b.x - a.x, b.z - a.z);
                // Walls ride on the deck; over a jump gap they stand on the ground.
                const float base = route.DeckOnSegment(i) ? (track_height(i) + track_height(i + 1)) * 0.5f : 0.0f;
                StaticBox(scene, "track_wall", XMFLOAT3((a.x + b.x) * 0.5f, base + height, (a.z + b.z) * 0.5f),
                          XMFLOAT3(thickness, height, len * 0.5f + 0.4f), yaw, 0.1f); // low grip: a wall hit slides the car along instead of spinning it (D-061)
                ++stats.wall_boxes;
            }
        }
        (void)route;
        return stats;
    }

    RaceSetup MakeTrackRaceSetup(const TrackDefinition& track, const std::vector<VehicleDefinition>& cars, int laps)
    {
        RaceSetup s;
        s.route = track.Route();
        s.race.laps = laps;
        s.race.checkpoints_s = track.checkpoints_s;
        for (size_t i = 0; i < cars.size() && i < track.grid.size(); ++i)
        {
            s.cars.push_back(cars[i]);
            s.grid.push_back({ XMFLOAT3(track.grid[i].x, 0.0f, track.grid[i].z), track.grid[i].yaw });
        }
        for (const PickupPlacement& p : track.pickups)
            s.pickups.push_back({ { p.x, p.z }, p.ability });
        return s;
    }
}
