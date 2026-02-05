#include "YamlDataEncoder.h"

YAML::Node YamlDataEncoder::encodeNode(const NodeSnapshot &snap) const
{
    if (!snap.key.id.has_value())
        throw std::runtime_error("encodeNode: _id is missing");

    YAML::Node root(YAML::NodeType::Map);

    // reserved metadata (underscore-prefixed)
    root["_profile"] = snap.key.profile;
    root["_id"] = *snap.key.id;

    if (snap.label.has_value())
        root["_label"] = *snap.label;

    if (snap.parent_id.has_value())
    {
        if (!snap.parent_id->empty())
            root["_parent_id"] = *snap.parent_id;
    }

    // plain fields at top-level (no "fields:" map)
    for (const auto &kv : snap.fields)
        root[kv.first] = encodeValue(kv.second);

    return root;
}

YAML::Node YamlDataEncoder::encodeValue(const FieldValue &v) const
{
    if (v.isNull())
        return YAML::Node(); // null
    if (v.isString())
        return YAML::Node(v.asString());
    if (v.isInteger())
        return YAML::Node(v.asInteger());
    if (v.isBoolean())
        return YAML::Node(v.asBoolean());

    if (v.isArray())
    {
        YAML::Node seq(YAML::NodeType::Sequence);
        for (const auto &item : v.asArray())
            seq.push_back(encodeValue(item));
        return seq;
    }
    if (v.isObject())
    {
        YAML::Node map(YAML::NodeType::Map);
        for (const auto &okv : v.asObject())
            map[okv.first] = encodeValue(okv.second);
        return map;
    }
    return YAML::Node(); // defensive
}
