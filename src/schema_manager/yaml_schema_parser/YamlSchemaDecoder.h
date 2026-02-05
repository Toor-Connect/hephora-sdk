#pragma once

#include <yaml-cpp/yaml.h>
#include "SchemaRegistry.h"

class YamlSchemaDecoder
{
public:
    // Decode a single YAML profile into the registry
    static void decodeProfile(const YAML::Node &node, SchemaRegistry &registry);

    // Decode multiple YAML profiles into the registry
    static void decodeProfiles(const std::vector<YAML::Node> &nodes, SchemaRegistry &registry);
};
