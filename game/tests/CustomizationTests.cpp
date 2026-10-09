#include "TestHarness.h"
#include "racer/Career.h"
#include "racer/Customization.h"

#include <algorithm>
#include <fstream>
#include <sstream>

using namespace racer;

namespace
{
    const char* kCatalog = R"({"schema_version": 1, "car": "D02", "slots": {
        "spoiler": {"variants": ["none", "lip", "wing", "gt"], "stock": "lip"},
        "rims": {"variants": ["five", "mesh", "dish"], "stock": "dish"}}})";

    CarCustomization Catalog()
    {
        CarCustomization c;
        std::string err;
        ParseCustomization(kCatalog, c, err);
        return c;
    }
}

TEST_CASE("customization: catalog parses slots, variants and the stock choice")
{
    CarCustomization c;
    std::string err;
    CHECK(ParseCustomization(kCatalog, c, err));
    CHECK(c.car == "D02");
    CHECK(c.slots.size() == 2);
    const CustomizationSlot* s = c.Find("spoiler");
    CHECK(s != nullptr && s->variants.size() == 4 && s->stock == "lip");
}

TEST_CASE("customization: a stock choice that is not a variant, or no slots, is refused")
{
    CarCustomization c;
    std::string err;
    CHECK(!ParseCustomization(R"({"schema_version":1,"car":"D02","slots":{"spoiler":{"variants":["none"],"stock":"wing"}}})", c, err));
    CHECK(err.find("spoiler") != std::string::npos);
    CHECK(!ParseCustomization(R"({"schema_version":1,"car":"D02","slots":{}})", c, err));
    CHECK(!ParseCustomization("not json", c, err));
}

TEST_CASE("customization: unset slots are stock; a saved variant that no longer exists falls back to stock")
{
    const CarCustomization c = Catalog();
    CarSetup setup;
    CHECK(ChosenVariant(c, setup, "spoiler") == "lip");
    setup.parts["spoiler"] = "gt";
    CHECK(ChosenVariant(c, setup, "spoiler") == "gt");
    setup.parts["rims"] = "chrome_wire"; // removed in a later content update
    CHECK(ChosenVariant(c, setup, "rims") == "dish");
    const auto all = ResolvedParts(c, setup);
    CHECK(all.size() == 2 && all.at("spoiler") == "gt" && all.at("rims") == "dish");
}

TEST_CASE("customization: cycling steps through the variants from the current choice and wraps")
{
    const CarCustomization c = Catalog();
    CarSetup setup;                      // spoiler at stock "lip" (index 1)
    CycleVariant(c, setup, "spoiler", 1);
    CHECK(setup.parts["spoiler"] == "wing");
    CycleVariant(c, setup, "spoiler", 1);
    CycleVariant(c, setup, "spoiler", 1);
    CHECK(setup.parts["spoiler"] == "none"); // gt -> none wraps
    CycleVariant(c, setup, "spoiler", -1);
    CHECK(setup.parts["spoiler"] == "gt");
    CycleVariant(c, setup, "unknown_slot", 1); // ignored, no entry created
    CHECK(setup.parts.count("unknown_slot") == 0);
}

TEST_CASE("customization: paint cycles through stock and the presets and wraps both ways")
{
    CarSetup setup;
    CHECK(setup.paint == -1);
    CyclePaint(setup, -1);
    CHECK(setup.paint == kPaintPresetCount - 1);
    CyclePaint(setup, 1);
    CHECK(setup.paint == -1);
    CyclePaint(setup, 1);
    CHECK(setup.paint == 0);
}

TEST_CASE("customization: the garage survives save/load; a save without a garage still loads")
{
    SaveProfile p;
    p.garage["D02"].parts["spoiler"] = "gt";
    p.garage["D02"].paint = 3;
    SaveProfile back;
    std::string err;
    CHECK(ParseProfile(SerializeProfile(p), back, err));
    CHECK(back.garage["D02"].parts["spoiler"] == "gt");
    CHECK(back.garage["D02"].paint == 3);
    SaveProfile old;
    std::string text = SerializeProfile(SaveProfile{});
    CHECK(ParseProfile(text, back, err));
    CHECK(back.garage.empty());
}

TEST_CASE("customization: every shipped car catalog parses (content/customization)")
{
    for (const char* id : { "D01", "D02", "D03", "D04", "D05" })
    {
        std::ifstream f(std::string(RACER_CONTENT_DIR) + "/customization/" + id + ".json", std::ios::binary);
        std::stringstream s;
        s << f.rdbuf();
        CarCustomization c;
        std::string err;
        CHECK(ParseCustomization(s.str(), c, err));
        CHECK(c.car == id);
        CHECK(c.Find("rims") != nullptr && c.Find("spoiler") != nullptr);
        // the modern kit (D-080) on every car: roof, light signature, widebody, three new rims
        auto has = [&](const char* slot, const char* v) {
            const CustomizationSlot* sl = c.Find(slot);
            return sl && std::find(sl->variants.begin(), sl->variants.end(), v) != sl->variants.end();
        };
        CHECK(has("roof", "scoop"));
        CHECK(has("roof", "rack"));
        CHECK(has("roof", "fin"));
        CHECK(has("lights", "drl"));
        CHECK(has("lights", "bar"));
        CHECK(has("flares", "widebody"));
        CHECK(has("rims", "y_spoke"));
        CHECK(has("rims", "aero"));
        CHECK(has("rims", "split"));
        const bool d05 = std::string(id) == "D05"; // the modern hatch ships with a roof scoop and DRLs (D-081)
        CHECK(c.Find("roof")->stock == (d05 ? "scoop" : "none"));
        CHECK(c.Find("lights")->stock == (d05 ? "drl" : "stock"));
    }
}

// ---- driver character (D-076) -------------------------------------------------------------------
TEST_CASE("driver look: every field cycles through its presets and wraps both ways")
{
    DriverLook l;
    CHECK(l.head == 0 && l.skin == 0 && l.hair == 0 && l.jacket == 0);
    CycleDriverLook(l, DriverField::Head, -1);
    CHECK(l.head == kDriverHeads - 1);
    CycleDriverLook(l, DriverField::Head, 1);
    CHECK(l.head == 0);
    for (int i = 0; i < kSkinTones; ++i)
        CycleDriverLook(l, DriverField::Skin, 1);
    CHECK(l.skin == 0);
    CycleDriverLook(l, DriverField::Hair, 1);
    CycleDriverLook(l, DriverField::Jacket, -1);
    CHECK(l.hair == 1 && l.jacket == kJacketColours - 1);
}

TEST_CASE("driver look: presets are valid colours; out-of-range indices clamp")
{
    for (int i = -1; i <= kSkinTones; ++i)
    {
        const DriverColour c = SkinTone(i);
        CHECK(c.r > 0.2f && c.r <= 1.0f && c.g > 0.1f && c.b > 0.05f && c.r >= c.g && c.g >= c.b); // warm skin, never grey
    }
    CHECK(HairColour(99).r == HairColour(kHairColours - 1).r);
    CHECK(JacketColour(-5).r == JacketColour(0).r);
}

TEST_CASE("driver look: AI drivers vary - a 12-car field has no two identical drivers")
{
    std::vector<DriverLook> field;
    for (int car = 0; car < 12; ++car)
        field.push_back(AiDriverLook(car));
    for (size_t a = 0; a < field.size(); ++a)
        for (size_t b = a + 1; b < field.size(); ++b)
            CHECK(!(field[a].head == field[b].head && field[a].skin == field[b].skin && field[a].hair == field[b].hair && field[a].jacket == field[b].jacket));
    CHECK(AiDriverLook(5).hair == AiDriverLook(5).hair); // deterministic
}

TEST_CASE("driver look: survives save/load; an old save without it loads the default")
{
    SaveProfile p;
    p.driver = { 2, 3, 4, 5 };
    SaveProfile back;
    std::string err;
    CHECK(ParseProfile(SerializeProfile(p), back, err));
    CHECK(back.driver.head == 2 && back.driver.skin == 3 && back.driver.hair == 4 && back.driver.jacket == 5);
    CHECK(ParseProfile(SerializeProfile(SaveProfile{}), back, err));
    CHECK(back.driver.head == 0 && back.driver.jacket == 0);
    CHECK(SerializeProfile(SaveProfile{}).find("\"driver\"") == std::string::npos); // default stays out of the file
}

// ---- part compatibility (D-093) ------------------------------------------------------------------
namespace
{
    CarCustomization RoofCar()
    {
        CarCustomization c;
        c.car = "X";
        c.slots = { { "weapon", { "none", "minigun", "rockets", "oil", "spikes" }, "none" }, { "roof", { "none", "scoop", "rack", "fin" }, "none" } };
        return c;
    }
}

TEST_CASE("customization: a roof rack and a roof weapon cannot both be fitted; the last pick wins")
{
    const CarCustomization c = RoofCar();
    CarSetup s;
    s.parts["weapon"] = "minigun";
    CycleVariant(c, s, "roof", 1); // none -> scoop: fits beside the minigun
    CHECK(ChosenVariant(c, s, "roof") == "scoop" && ChosenVariant(c, s, "weapon") == "minigun");
    CycleVariant(c, s, "roof", 1); // scoop -> rack: the minigun comes off
    CHECK(ChosenVariant(c, s, "roof") == "rack");
    CHECK(ChosenVariant(c, s, "weapon") == "none");
    CycleVariant(c, s, "weapon", 1); // none -> minigun: the rack comes off
    CycleVariant(c, s, "weapon", 1); // minigun -> rockets
    CHECK(ChosenVariant(c, s, "weapon") == "rockets");
    CHECK(ChosenVariant(c, s, "roof") == "none");
    s.parts["roof"] = "rack"; // an old save holding both: the weapon is shown, the rack is not
    const auto r = ResolvedParts(c, s);
    CHECK(r.at("weapon") == "rockets" && r.at("roof") == "none");
    s.parts["weapon"] = "oil"; // a rear weapon fits with the rack
    CHECK(ResolvedParts(c, s).at("roof") == "rack");
}
