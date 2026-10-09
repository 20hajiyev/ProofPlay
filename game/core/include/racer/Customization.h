#pragma once
#include <map>
#include <string>
#include <vector>

namespace racer
{
    // Car customisation (plan 2.13, D-055). The catalog per car is generated with the model
    // (content/customization/<id>.json): optional parts in slots, each slot a list of variants and a
    // stock choice. The player's choices live in the save; anything a later content update removed
    // falls back to stock, so an old save never shows a missing part.
    struct CustomizationSlot
    {
        std::string id;                    // "spoiler", "aero_f", "rims", ...
        std::vector<std::string> variants; // "none" means no part in that slot
        std::string stock;
    };

    struct CarCustomization
    {
        std::string car;
        std::vector<CustomizationSlot> slots; // in file order
        const CustomizationSlot* Find(const std::string& slot) const;
    };

    bool ParseCustomization(const std::string& json_text, CarCustomization& out, std::string& error);

    // The player's setup of one car.
    struct CarSetup
    {
        std::map<std::string, std::string> parts; // slot -> variant (absent = stock)
        int paint = -1;                           // -1 = the car's stock colour, else a preset index
    };

    constexpr int kPaintPresetCount = 8;
    struct PaintPreset
    {
        const char* key; // localisation key "paint.<name>"
        float r, g, b;
    };
    const PaintPreset& GetPaintPreset(int index); // 0..kPaintPresetCount-1

    // The driver character (D-076): one model, recoloured and with one of three faces. The player's
    // look lives in the save; AI drivers get a varied look from their grid index.
    constexpr int kDriverHeads = 3, kSkinTones = 5, kHairColours = 6, kJacketColours = 8;
    struct DriverLook
    {
        int head = 0, skin = 0, hair = 0, jacket = 0;
    };
    struct DriverColour
    {
        float r, g, b;
    };
    enum class DriverField { Head, Skin, Hair, Jacket };
    DriverColour SkinTone(int index);
    DriverColour HairColour(int index);
    DriverColour JacketColour(int index);
    void CycleDriverLook(DriverLook& l, DriverField f, int dir);
    DriverLook AiDriverLook(int car_index);

    std::string ChosenVariant(const CarCustomization& c, const CarSetup& s, const std::string& slot);
    std::map<std::string, std::string> ResolvedParts(const CarCustomization& c, const CarSetup& s);
    void CycleVariant(const CarCustomization& c, CarSetup& s, const std::string& slot, int dir);
    void CyclePaint(CarSetup& s, int dir); // stock -> presets -> stock
}
