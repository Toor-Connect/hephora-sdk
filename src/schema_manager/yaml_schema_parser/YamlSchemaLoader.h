#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <yaml-cpp/yaml.h>

class YamlSchemaLoader
{
public:
    // Convert map of filename → YAML string into YAML::Node list
    static std::vector<YAML::Node> loadFromSources(
        const std::unordered_map<std::string, std::string> &sources);

    // Convenience: directly load and decode into SchemaRegistry
    static void loadIntoRegistry(
        const std::unordered_map<std::string, std::string> &sources,
        class SchemaRegistry &registry);
};
