#include "racer/runtime/HandlingTrack.h"

#include "wiECS.h"

using namespace wi::ecs;
using namespace wi::scene;

namespace racer::runtime
{
    namespace
    {
        struct Builder
        {
            Scene& scene;
            bool visuals;

            // Static box; the collider and the optional visual share the transform scale.
            Entity Box(const char* name, XMFLOAT3 center, XMFLOAT3 half, float grip, XMFLOAT4 color, XMFLOAT3 pitch_roll_yaw = {})
            {
                Entity e;
                if (visuals)
                {
                    e = scene.Entity_CreateCube(name);
                    if (MaterialComponent* m = scene.materials.GetComponent(e))
                    {
                        m->SetBaseColor(color);
                        m->SetRoughness(0.85f);
                    }
                }
                else
                {
                    e = CreateEntity();
                    scene.names.Create(e) = name;
                    scene.transforms.Create(e);
                }
                TransformComponent& t = *scene.transforms.GetComponent(e);
                t.Scale(half);
                t.RotateRollPitchYaw(pitch_roll_yaw);
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
        };
    }

    void BuildHandlingTrack(Scene& scene, bool with_visuals)
    {
        using L = HandlingTrackLayout;
        Builder b{ scene, with_visuals };
        const XMFLOAT4 asphalt(0.18f, 0.19f, 0.2f, 1), gravel(0.55f, 0.5f, 0.42f, 1), wet(0.1f, 0.12f, 0.16f, 1);
        const XMFLOAT4 concrete(0.6f, 0.6f, 0.58f, 1), curb(0.8f, 0.15f, 0.12f, 1), ramp(0.9f, 0.7f, 0.1f, 1);
        const float length = L::kStraightEndZ - L::kStraightStartZ;
        const float midZ = (L::kStraightEndZ + L::kStraightStartZ) * 0.5f;

        // Ground: non-overlapping lanes so every wheel sees exactly one surface.
        const float padMinX = -260.0f, asphaltMaxX = L::kGravelLaneX - L::kLaneHalfWidth;
        b.Box("ground_asphalt", { (padMinX + asphaltMaxX) * 0.5f, -0.5f, midZ }, { (asphaltMaxX - padMinX) * 0.5f, 0.5f, length * 0.5f }, surface::kDryAsphalt, asphalt);
        b.Box("ground_gravel", { L::kGravelLaneX, -0.5f, midZ }, { L::kLaneHalfWidth, 0.5f, length * 0.5f }, surface::kGravel, gravel);
        const float wetMin = L::kGravelLaneX + L::kLaneHalfWidth;
        const float wetMax = L::kWetLaneX + L::kLaneHalfWidth;
        b.Box("ground_wet", { (wetMin + wetMax) * 0.5f, -0.5f, midZ }, { (wetMax - wetMin) * 0.5f, 0.5f, length * 0.5f }, surface::kWetAsphalt, wet);

        // Ramp: 12 m long, 8 m wide, rising 10 degrees toward +Z; top edge ~1.4 m.
        const float rampPitch = -XMConvertToRadians(10.0f);
        b.Box("ramp", { L::kRampX, 0.55f, L::kRampZ }, { 4.0f, 0.5f, 6.0f }, surface::kConcrete, ramp, { rampPitch, 0, 0 });

        // Curbs: 8 cm high, 0.5 m deep strips across the lane every 10 m.
        for (int i = 0; i < 8; ++i)
            b.Box("curb", { L::kCurbX, 0.04f, L::kCurbZ0 + i * 10.0f }, { 5.0f, 0.04f, 0.25f }, surface::kConcrete, curb);

        // Perimeter barrier: nothing drivable ends in a drop (plan 2.9, static safety boundary).
        const float minX = padMinX, maxX = wetMax, minZ = L::kStraightStartZ, maxZ = L::kStraightEndZ;
        const float wallH = 1.2f, wallT = 1.0f;
        b.Box("perimeter_w", { minX - wallT, wallH * 0.5f, midZ }, { wallT, wallH * 0.5f, length * 0.5f + 2 * wallT }, surface::kConcrete, concrete);
        b.Box("perimeter_e", { maxX + wallT, wallH * 0.5f, midZ }, { wallT, wallH * 0.5f, length * 0.5f + 2 * wallT }, surface::kConcrete, concrete);
        b.Box("perimeter_s", { (minX + maxX) * 0.5f, wallH * 0.5f, minZ - wallT }, { (maxX - minX) * 0.5f, wallH * 0.5f, wallT }, surface::kConcrete, concrete);
        b.Box("perimeter_n", { (minX + maxX) * 0.5f, wallH * 0.5f, maxZ + wallT }, { (maxX - minX) * 0.5f, wallH * 0.5f, wallT }, surface::kConcrete, concrete);

        // Wall: 1.2 m concrete barrier along +Z for scrape tests.
        const float wallLen = L::kWallZ1 - L::kWallZ0;
        b.Box("wall", { L::kWallX, 0.6f, (L::kWallZ0 + L::kWallZ1) * 0.5f }, { 0.4f, 0.6f, wallLen * 0.5f }, surface::kConcrete, concrete);
    }
}
