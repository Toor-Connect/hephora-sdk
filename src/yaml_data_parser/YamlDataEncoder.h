#pragma once
#include <yaml-cpp/yaml.h>
#include "NodeAddress.h" // NodeSnapshot, FieldValue

// Stateless helper to encode a NodeSnapshot exactly like YamlDataDecoder expects.
class YamlDataEncoder
{
public:
    // Produces:
    // _profile: "<profile>"
    // _id: "<id>"
    // _label: "<label>"      # omitted if empty
    // _parent_id: "<pid>"    # omitted if empty
    // <field1>: ...
    // <field2>: ...
    YAML::Node encodeNode(const NodeSnapshot &snap) const;

private:
    YAML::Node encodeValue(const FieldValue &v) const;
};
