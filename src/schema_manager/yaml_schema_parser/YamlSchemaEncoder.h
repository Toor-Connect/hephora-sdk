#pragma once
#include <yaml-cpp/yaml.h>
#include "NodeSchema.h"

class YamlSchemaEncoder
{
public:
    static YAML::Node encodeNodeSchema(const NodeSchema &schema);

private:
};
