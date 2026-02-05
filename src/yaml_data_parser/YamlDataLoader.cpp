#include "YamlDataLoader.h"
#include <algorithm>
#include <stdexcept>
#include <vector>

// yaml-cpp includes are already pulled via the header, but catching by std::exception is fine.

std::unordered_map<std::string, YAML::Node>
YamlDataLoader::loadFromSources(const std::unordered_map<std::string, std::string> &sources)
{
    std::unordered_map<std::string, YAML::Node> out;
    out.reserve(sources.size());

    // Deterministic: sort filenames, then always fetch text by that key.
    std::vector<std::string> names;
    names.reserve(sources.size());
    for (const auto &kv : sources)
        names.push_back(kv.first);
    std::sort(names.begin(), names.end());

    for (const auto &name : names)
    {
        auto it = sources.find(name);
        if (it == sources.end())
            throw std::runtime_error("YamlDataLoader: key disappeared during load: " + name);

        const std::string &text = it->second;

        YAML::Node n;
        try
        {
            n = YAML::Load(text);
        }
        catch (const std::exception &e)
        {
            // Required by the test: wrap with filename
            throw std::runtime_error(std::string("YAML parse error in [") + name + "]: " + e.what());
        }

        if (!n || !n.IsMap())
            throw std::runtime_error("YamlDataLoader: " + name + " is empty or not a YAML map");

        // Tag the node with the filename for diagnostics (test checks Tag().find(name))
        n.SetTag(std::string("!file:") + name);

        out.emplace(name, std::move(n));
    }

    return out;
}
