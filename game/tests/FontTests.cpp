// Plan 2.14 / M2 list: every character the game can show in AZ or EN must exist in the font the
// HUD and menus use (Wicked's built-in Liberation Sans). A missing glyph renders as a blank box.
#include "TestHarness.h"
#include "WickedEngine.h"
#include "Utility/liberation_sans.h"
#include "Utility/stb_truetype.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

namespace
{
    std::set<uint32_t> Codepoints(const std::string& utf8)
    {
        std::set<uint32_t> out;
        for (size_t i = 0; i < utf8.size();)
        {
            const unsigned char c = static_cast<unsigned char>(utf8[i]);
            uint32_t cp = c;
            size_t n = 1;
            if (c >= 0xF0) cp = c & 0x07, n = 4;
            else if (c >= 0xE0) cp = c & 0x0F, n = 3;
            else if (c >= 0xC0) cp = c & 0x1F, n = 2;
            for (size_t k = 1; k < n && i + k < utf8.size(); ++k)
                cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
            out.insert(cp);
            i += n;
        }
        return out;
    }

    std::set<uint32_t> TableCodepoints(const char* file)
    {
        std::ifstream f(std::string(RACER_CONTENT_DIR "/text/") + file, std::ios::binary);
        std::stringstream s;
        s << f.rdbuf();
        std::set<uint32_t> all;
        const nlohmann::json j = nlohmann::json::parse(s.str());
        const nlohmann::json& table = j.contains("strings") ? j.at("strings") : j;
        for (auto it = table.begin(); it != table.end(); ++it)
            if (it.value().is_string())
                for (uint32_t cp : Codepoints(it.value().get<std::string>()))
                    all.insert(cp);
        return all;
    }
}

TEST_CASE("font: every character in the AZ and EN string tables has a glyph in the game font")
{
    wi::vector<uint8_t> ttf;
    CHECK(wi::helper::Decompress(liberation_sans_zstd, sizeof(liberation_sans_zstd), ttf));
    stbtt_fontinfo font{};
    CHECK(stbtt_InitFont(&font, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0)) != 0);
    CHECK(stbtt_FindGlyphIndex(&font, 0x4E2D) == 0); // the checker must be able to see a missing glyph
    int checked = 0, missing = 0;
    std::set<uint32_t> needed;
    for (const char* table : { "az.json", "en.json" })
        for (uint32_t cp : TableCodepoints(table))
            needed.insert(cp);
    // Azerbaijani letters in both cases, even ones no string uses yet (names, future text).
    for (uint32_t cp : { 0x0259u, 0x018Fu, 0x011Fu, 0x011Eu, 0x0131u, 0x0130u, 0x00F6u, 0x00D6u, 0x015Fu, 0x015Eu, 0x00E7u, 0x00C7u, 0x00FCu, 0x00DCu })
        needed.insert(cp);
    for (uint32_t cp : needed)
    {
        if (cp < 0x20)
            continue;
        ++checked;
        if (stbtt_FindGlyphIndex(&font, int(cp)) == 0)
        {
            ++missing;
            std::printf("  MISSING glyph U+%04X\n", cp);
        }
    }
    std::printf("  INFO font: %d distinct characters checked, %d missing\n", checked, missing);
    CHECK(checked > 60);
    CHECK(missing == 0);
}
