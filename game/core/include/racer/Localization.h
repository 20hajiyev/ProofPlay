#pragma once
#include <map>
#include <string>
#include <vector>

namespace racer
{
    // Key -> UTF-8 text for one language (plan 2.14: text is never baked into images).
    class Strings
    {
    public:
        bool Load(const std::string& json_text, std::string& error);
        // Missing keys return the key itself so a gap is visible on screen, never a crash.
        const std::string& Get(const std::string& key) const;
        bool Has(const std::string& key) const { return table_.count(key) != 0; }
        std::vector<std::string> Keys() const;

    private:
        std::map<std::string, std::string> table_;
    };
}
