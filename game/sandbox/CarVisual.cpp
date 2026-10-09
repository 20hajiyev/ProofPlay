#include "CarVisual.h"
#include "racer/Customization.h"

#include <algorithm>
#include <cstdlib>

#include <fstream>
#include <sstream>
#include <vector>

using namespace wi::scene;
using wi::ecs::Entity;
using wi::ecs::INVALID_ENTITY;

void ToonifyMaterials(Scene& s)
{
    for (size_t i = 0; i < s.materials.GetCount(); ++i)
    {
        MaterialComponent& m = s.materials[i];
        m.shaderType = MaterialComponent::SHADERTYPE_CARTOON;
        // Depth outlines on large ground surfaces seen at grazing angles fill the distance with
        // black (measured: the far quay turned solid black). Ground materials get no outline.
        const NameComponent* n = s.names.GetComponent(s.materials.GetEntity(i));
        const std::string name = n ? n->name : std::string();
        const bool ground = name.find("Road") != std::string::npos || name.find("Quay") != std::string::npos ||
                            name.find("Water") != std::string::npos || name.find("Paint White") != std::string::npos ||
                            name.find("Charcoal") != std::string::npos;
        // Corrugated cladding (D-054): ribs smaller than a pixel at distance flip the crease test
        // between neighbouring slopes and speckle the ink; the frame around the sheets still inks.
        const bool sheet = name.find("Sheet") != std::string::npos || name.find("NoInk") != std::string::npos; // + tiny/glowing sky parts (D-057)
        m.SetOutlineEnabled(!ground && !sheet);
    }
}

std::map<std::string, std::string> ResolveCarParts(const std::string& catalog_file, const std::map<std::string, std::string>& choice)
{
    std::ifstream f(catalog_file, std::ios::binary);
    if (!f)
        return {};
    std::stringstream text;
    text << f.rdbuf();
    racer::CarCustomization cat;
    std::string error;
    if (!racer::ParseCustomization(text.str(), cat, error))
        return {};
    racer::CarSetup setup;
    setup.parts = choice;
    std::map<std::string, std::string> out = racer::ResolvedParts(cat, setup);
    for (const auto& [k, v] : choice) // performance parts with a model part (intercooler, D-072)
        if (k.rfind("perf_", 0) == 0)
            out[k] = v;
    return out;
}

void KeepCarParts(Scene& s, const std::map<std::string, std::string>& resolved)
{
    if (resolved.empty())
        return;
    const auto rims = resolved.find("rims");
    std::vector<Entity> drop;
    for (size_t i = 0; i < s.names.GetCount(); ++i)
    {
        const std::string& n = s.names[i].name;
        if (n.rfind("PART_", 0) == 0)
        {
            // PART_<slot>_<variant>; slot names may hold '_' (aero_f), so match the known pairs.
            bool selected = false;
            for (const auto& [slot, v] : resolved)
                selected = selected || n == "PART_" + slot + "_" + v;
            if (!selected)
                drop.push_back(s.names.GetEntity(i));
            else if (std::getenv("RACER_PARTS_DEBUG"))
                wi::backlog::post("[racer] kept part " + n);
        }
        else if (n.rfind("RIM_", 0) == 0 && (rims == resolved.end() || n.rfind("RIM_" + rims->second + "_", 0) != 0))
            drop.push_back(s.names.GetEntity(i));
    }
    for (Entity e : drop)
        s.Entity_Remove(e, true);
}

void PaintCar(Scene& s, const XMFLOAT4& paint)
{
    for (size_t i = 0; i < s.materials.GetCount(); ++i)
    {
        const NameComponent* n = s.names.GetComponent(s.materials.GetEntity(i));
        if (n && n->name.find("paint") != std::string::npos)
            s.materials[i].SetBaseColor(paint);
    }
}

bool AttachDriver(Scene& s, Entity car_root, const std::string& file, const racer::DriverLook& look, std::vector<std::pair<Entity, std::string>>* mats)
{
    const Entity body = s.Entity_FindByName("BODY", car_root);
    const Entity car_anchor = s.Entity_FindByName("DRIVER_ANCHOR", car_root);
    const TransformComponent* ca = car_anchor != INVALID_ENTITY ? s.transforms.GetComponent(car_anchor) : nullptr;
    if (body == INVALID_ENTITY || !ca)
    {
        wi::backlog::post(std::string("[racer] AttachDriver: car has ") + (body == INVALID_ENTITY ? "no BODY" : "no DRIVER_ANCHOR"));
        return false;
    }
    Scene staging;
    const Entity root = LoadModel(staging, file, XMMatrixIdentity(), true);
    if (root == INVALID_ENTITY)
        return false;
    ToonifyMaterials(staging);
    const Entity drv_anchor = staging.Entity_FindByName("DRIVER_ANCHOR", root);
    const TransformComponent* da = drv_anchor != INVALID_ENTITY ? staging.transforms.GetComponent(drv_anchor) : nullptr;
    if (!da)
    {
        wi::backlog::post("[racer] AttachDriver: no DRIVER_ANCHOR in " + file);
        return false;
    }
    const XMFLOAT3 off(ca->translation_local.x - da->translation_local.x, ca->translation_local.y - da->translation_local.y,
                       ca->translation_local.z - da->translation_local.z);

    // one face: the chosen one is renamed DRIVER_HEAD, the others go
    static const char* kHeads[racer::kDriverHeads] = { "DRIVER_HEAD", "DRIVER_HEAD_1", "DRIVER_HEAD_2" };
    const int hv = std::clamp(look.head, 0, racer::kDriverHeads - 1);
    std::vector<Entity> heads;
    for (const char* n : kHeads)
        heads.push_back(staging.Entity_FindByName(n, root));
    for (int k = 0; k < racer::kDriverHeads; ++k)
        if (k != hv && heads[size_t(k)] != INVALID_ENTITY)
            staging.Entity_Remove(heads[size_t(k)], true);
    if (heads[size_t(hv)] != INVALID_ENTITY)
        if (NameComponent* n = staging.names.GetComponent(heads[size_t(hv)]))
            n->name = "DRIVER_HEAD";

    // the look's colours by material family
    const racer::DriverColour skin = racer::SkinTone(look.skin), hair = racer::HairColour(look.hair), jacket = racer::JacketColour(look.jacket);
    auto tint = [](const racer::DriverColour& c, float r, float g, float b) { return XMFLOAT4(c.r * r, c.g * g, c.b * b, 1.0f); };
    for (size_t i = 0; i < staging.materials.GetCount(); ++i)
    {
        const NameComponent* n = staging.names.GetComponent(staging.materials.GetEntity(i));
        if (!n)
            continue;
        const std::string& m = n->name;
        auto is = [&](const char* prefix) { return m.rfind(prefix, 0) == 0; };
        MaterialComponent& mc = staging.materials[i];
        if (is("toon_skin"))
            mc.SetBaseColor(tint(skin, 1, 1, 1));
        else if (is("toon_ear_in"))
            mc.SetBaseColor(tint(skin, 0.82f, 0.82f, 0.82f));
        else if (is("toon_lips"))
            mc.SetBaseColor(tint(skin, 0.78f, 0.55f, 0.55f));
        else if (is("toon_stubble"))
            mc.SetBaseColor(tint(skin, 0.8f, 0.78f, 0.78f));
        else if (is("toon_face_shade"))
            mc.SetBaseColor(tint(skin, 0.74f, 0.74f, 0.74f));
        else if (is("toon_face_shadow"))
            mc.SetBaseColor(tint(skin, 0.42f, 0.42f, 0.42f));
        else if (is("toon_hair") || is("toon_beard"))
            mc.SetBaseColor(tint(hair, 1, 1, 1));
        else if (is("toon_jacket_dark"))
            mc.SetBaseColor(tint(jacket, 0.55f, 0.55f, 0.55f));
        else if (is("toon_jacket"))
            mc.SetBaseColor(tint(jacket, 1, 1, 1));
        if (mats)
            mats->push_back({ staging.materials.GetEntity(i), m });
    }
    // gesture hands start hidden (the race rig shows them when used)
    for (const char* n : { "SHIFT_HAND", "TAUNT_HAND", "POINT_HAND_L", "POINT_HAND_R" })
        if (TransformComponent* t = staging.transforms.GetComponent(staging.Entity_FindByName(n, root)))
            t->scale_local = XMFLOAT3(0, 0, 0), t->SetDirty();

    // the pivots: siblings of DRIVER_ANCHOR (the cooked file wraps them in a scene node under the
    // load root), moved into the car's frame
    const HierarchyComponent* ah = staging.hierarchy.GetComponent(drv_anchor);
    const Entity container = ah ? ah->parentID : root;
    std::vector<Entity> top;
    for (size_t i = 0; i < staging.hierarchy.GetCount(); ++i)
        if (staging.hierarchy[i].parentID == container)
            top.push_back(staging.hierarchy.GetEntity(i));
    for (Entity e : top)
        if (TransformComponent* t = staging.transforms.GetComponent(e))
        {
            t->translation_local.x += off.x, t->translation_local.y += off.y, t->translation_local.z += off.z;
            t->SetDirty();
        }
    s.Merge(staging);
    for (Entity e : top)
    {
        // Attach detaches first, which rewrites the local transform from the (stale) world matrix:
        // put the shifted local back afterwards
        TransformComponent* t = s.transforms.GetComponent(e);
        const TransformComponent keep = t ? *t : TransformComponent{};
        s.Component_Attach(e, body, true);
        if ((t = s.transforms.GetComponent(e)) != nullptr)
        {
            t->translation_local = keep.translation_local;
            t->rotation_local = keep.rotation_local;
            t->scale_local = keep.scale_local;
            t->SetDirty();
        }
    }
    s.Entity_Remove(root, true); // the load root and the scene node, now empty
    if (std::getenv("RACER_DRIVER_DEBUG"))
    {
        s.Update(0);
        for (const char* n : { "DRIVER_TORSO", "DRIVER_HEAD", "THIGH_L", "HAND_L" })
        {
            const Entity e = s.Entity_FindByName(n, car_root);
            const TransformComponent* t = e != INVALID_ENTITY ? s.transforms.GetComponent(e) : nullptr;
            const HierarchyComponent* h = e != INVALID_ENTITY ? s.hierarchy.GetComponent(e) : nullptr;
            const NameComponent* pn = h ? s.names.GetComponent(h->parentID) : nullptr;
            if (t)
                wi::backlog::post(std::string("[drvdbg] ") + n + " local " + std::to_string(t->translation_local.x) + "," + std::to_string(t->translation_local.y) + "," + std::to_string(t->translation_local.z) +
                                  " world " + std::to_string(t->world._41) + "," + std::to_string(t->world._42) + "," + std::to_string(t->world._43) + " scale " + std::to_string(t->scale_local.x) + " parent " + (pn ? pn->name : std::string("?")));
            else
                wi::backlog::post(std::string("[drvdbg] ") + n + " missing");
        }
        wi::backlog::post("[drvdbg] top " + std::to_string(top.size()) + " off " + std::to_string(off.x) + "," + std::to_string(off.y) + "," + std::to_string(off.z));
    }
    return true;
}
