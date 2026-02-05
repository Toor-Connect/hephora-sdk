// src/data/YamlDataLoader.h
#pragma once

#include <string>
#include <unordered_map>
#include <yaml-cpp/yaml.h>

class YamlDataLoader
{
public:
    static std::unordered_map<std::string, YAML::Node> loadFromSources(
        const std::unordered_map<std::string, std::string> &sources);
};
