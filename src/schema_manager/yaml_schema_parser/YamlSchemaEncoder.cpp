#include "YamlSchemaEncoder.h"

YAML::Node YamlSchemaEncoder::encodeNodeSchema(const NodeSchema &schema)
{
    YAML::Node rawNode = YAML::Load(schema.rawYaml());
    return rawNode;
}