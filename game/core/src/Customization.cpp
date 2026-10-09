#include "racer/Customization.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace racer
{
    const CustomizationSlot* CarCustomization::Find(const std::string& slot) const
    {
        for (const CustomizationSlot& s : slots)
            if (s.id == slot)
                return &s;
        return nullptr;
    }

    bool ParseCustomization(const std::string& text, CarCustomization& out, std::string& error)
    {
        // ordered_json keeps the file's slot order for the menu rows.
        const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(text, nullptr, false);
        if (doc.is_discarded() || !doc.is_object())
        {
            error = "not valid JSON";
            return false;
        }
        if (!doc.contains("car") || !doc["car"].is_string() || !doc.contains("slots") || !doc["slots"].is_object() || doc["slots"].empty())
        {
            error = "needs a car id and at least one slot";
            return false;
        }
        CarCustomization c;
        c.car = doc["car"].get<std::string>();
        for (const auto& [id, def] : doc["slots"].items())
        {
            CustomizationSlot s;
            s.id = id;
            if (!def.is_object() || !def.contains("variants") || !def["variants"].is_array() || !def.contains("stock") || !def["stock"].is_string())
            {
                error = "slot " + id + ": needs variants and stock";
                return false;
            }
            for (const auto& v : def["variants"])
                if (v.is_string())
                    s.variants.push_back(v.get<std::string>());
            s.stock = def["stock"].get<std::string>();
            if (s.variants.empty() || std::find(s.variants.begin(), s.variants.end(), s.stock) == s.variants.end())
            {
                error = "slot " + id + ": stock '" + s.stock + "' is not one of its variants";
                return false;
            }
            c.slots.push_back(std::move(s));
        }
        out = std::move(c);
        return true;
    }

    const PaintPreset& GetPaintPreset(int index)
    {
        // Street-car colours that stay readable under the cel shader and against the harbour.
        static const PaintPreset kPresets[kPaintPresetCount] = {
            { "paint.red", 0.82f, 0.08f, 0.07f },   { "paint.white", 0.92f, 0.92f, 0.94f }, { "paint.black", 0.07f, 0.07f, 0.08f },
            { "paint.silver", 0.62f, 0.64f, 0.68f }, { "paint.blue", 0.1f, 0.28f, 0.78f },  { "paint.yellow", 0.98f, 0.78f, 0.08f },
            { "paint.green", 0.1f, 0.5f, 0.25f },   { "paint.purple", 0.42f, 0.12f, 0.6f },
        };
        return kPresets[std::clamp(index, 0, kPaintPresetCount - 1)];
    }

    DriverColour SkinTone(int i)
    {
        // warm tones from light to deep; the face's vertex-colour shading multiplies these
        // the default first: a warm mid tone (the lightest washed out to white under the grade, D-078)
        static const DriverColour k[kSkinTones] = { { 0.86f, 0.56f, 0.38f }, { 0.95f, 0.72f, 0.56f }, { 0.74f, 0.48f, 0.32f },
                                                    { 0.6f, 0.37f, 0.24f }, { 0.4f, 0.25f, 0.17f } };
        return k[std::clamp(i, 0, kSkinTones - 1)];
    }

    DriverColour HairColour(int i)
    {
        static const DriverColour k[kHairColours] = { { 0.24f, 0.13f, 0.06f }, { 0.07f, 0.06f, 0.06f }, { 0.62f, 0.6f, 0.57f },
                                                      { 0.85f, 0.65f, 0.3f }, { 0.6f, 0.2f, 0.08f }, { 0.15f, 0.35f, 0.85f } };
        return k[std::clamp(i, 0, kHairColours - 1)];
    }

    DriverColour JacketColour(int i)
    {
        static const DriverColour k[kJacketColours] = { { 0.75f, 0.1f, 0.08f }, { 0.12f, 0.2f, 0.55f }, { 0.1f, 0.1f, 0.11f }, { 0.18f, 0.4f, 0.2f },
                                                        { 0.95f, 0.6f, 0.1f }, { 0.55f, 0.15f, 0.6f }, { 0.85f, 0.85f, 0.82f }, { 0.4f, 0.28f, 0.15f } };
        return k[std::clamp(i, 0, kJacketColours - 1)];
    }

    void CycleDriverLook(DriverLook& l, DriverField f, int dir)
    {
        auto cycle = [dir](int& v, int n) { v = ((v + dir) % n + n) % n; };
        switch (f)
        {
        case DriverField::Head: cycle(l.head, kDriverHeads); break;
        case DriverField::Skin: cycle(l.skin, kSkinTones); break;
        case DriverField::Hair: cycle(l.hair, kHairColours); break;
        case DriverField::Jacket: cycle(l.jacket, kJacketColours); break;
        }
    }

    DriverLook AiDriverLook(int car)
    {
        // coprime strides through each list, so neighbours on the grid never look alike
        return { car % kDriverHeads, (car * 2 + 1) % kSkinTones, (car * 5 + 2) % kHairColours, (car * 3 + 1) % kJacketColours };
    }

    std::string ChosenVariant(const CarCustomization& c, const CarSetup& s, const std::string& slot)
    {
        const CustomizationSlot* def = c.Find(slot);
        if (!def)
            return {};
        const auto it = s.parts.find(slot);
        if (it != s.parts.end() && std::find(def->variants.begin(), def->variants.end(), it->second) != def->variants.end())
            return it->second;
        return def->stock;
    }

    namespace
    {
        // Parts that share a mount (D-093): both sit on the middle of the roof, so with both fitted the
        // weapon vanished inside the rack (garage and race screenshots). Pairs of (slot, variant).
        struct Clash
        {
            const char *slot_a, *variant_a, *slot_b, *variant_b;
        };
        constexpr Clash kClashes[] = {
            { "roof", "rack", "weapon", "minigun" },
            { "roof", "rack", "weapon", "rockets" },
        };

        // The slot and variant `slot = variant` cannot sit beside, or null.
        const Clash* ClashWith(const std::string& slot, const std::string& variant, const std::string& other_slot, const std::string& other_variant)
        {
            for (const Clash& k : kClashes)
                if ((slot == k.slot_a && variant == k.variant_a && other_slot == k.slot_b && other_variant == k.variant_b) ||
                    (slot == k.slot_b && variant == k.variant_b && other_slot == k.slot_a && other_variant == k.variant_a))
                    return &k;
            return nullptr;
        }
    }

    std::map<std::string, std::string> ResolvedParts(const CarCustomization& c, const CarSetup& s)
    {
        std::map<std::string, std::string> out;
        for (const CustomizationSlot& slot : c.slots)
            out[slot.id] = ChosenVariant(c, s, slot.id);
        // An old save holding a clashing pair keeps the weapon (it fights) and drops the roof part.
        for (const Clash& k : kClashes)
            if (out.count(k.slot_a) && out.count(k.slot_b) && out[k.slot_a] == k.variant_a && out[k.slot_b] == k.variant_b)
            {
                const std::string& drop = std::string(k.slot_a) == "weapon" ? k.slot_b : k.slot_a;
                if (const CustomizationSlot* def = c.Find(drop))
                    out[drop] = std::find(def->variants.begin(), def->variants.end(), "none") != def->variants.end() ? "none" : def->stock;
            }
        return out;
    }

    void CycleVariant(const CarCustomization& c, CarSetup& s, const std::string& slot, int dir)
    {
        const CustomizationSlot* def = c.Find(slot);
        if (!def || dir == 0)
            return;
        const int n = int(def->variants.size());
        const auto cur = std::find(def->variants.begin(), def->variants.end(), ChosenVariant(c, s, slot));
        const int i = int(cur - def->variants.begin());
        const std::string& picked = def->variants[size_t(((i + (dir > 0 ? 1 : -1)) % n + n) % n)];
        s.parts[slot] = picked;
        // The last pick wins: a part that cannot share its mount with the new one comes off (D-093).
        for (const CustomizationSlot& other : c.slots)
            if (other.id != slot && ClashWith(slot, picked, other.id, ChosenVariant(c, s, other.id)))
                s.parts[other.id] = std::find(other.variants.begin(), other.variants.end(), "none") != other.variants.end() ? "none" : other.stock;
    }

    void CyclePaint(CarSetup& s, int dir)
    {
        // Positions: 0 = stock (-1), 1..N = presets 0..N-1.
        const int n = kPaintPresetCount + 1;
        const int pos = s.paint + 1;
        s.paint = ((pos + (dir > 0 ? 1 : -1)) % n + n) % n - 1;
    }
}
