#include "racer/Localization.h"

#include <nlohmann/json.hpp>

namespace racer
{
    bool Strings::Load(const std::string& text, std::string& error)
    {
        const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object())
        {
            error = "not a JSON object";
            return false;
        }
        std::map<std::string, std::string> table;
        for (auto it = j.begin(); it != j.end(); ++it)
        {
            if (!it.value().is_string() || it.value().get<std::string>().empty())
            {
                error = "key '" + it.key() + "' must map to non-empty text";
                return false;
            }
            table[it.key()] = it.value().get<std::string>();
        }
        table_ = std::move(table);
        return true;
    }

    const std::string& Strings::Get(const std::string& key) const
    {
        const auto it = table_.find(key);
        return it != table_.end() ? it->second : key;
    }

    std::vector<std::string> Strings::Keys() const
    {
        std::vector<std::string> keys;
        for (auto& [k, v] : table_)
            keys.push_back(k);
        return keys;
    }
}
