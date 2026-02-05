#include "InMemoryDataCommit.h"
#include <sstream>

// FieldValue is a std::variant-like; convert it recursively to YAML::Node
YAML::Node InMemoryDataCommit::toYaml(const FieldValue &v)
{
    // The project already added helpers like isString/asString etc,
    // but we keep this robust using std::visit on the underlying variant.
    YAML::Node n;
    std::visit([&](auto &&arg)
               {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            // leave as null node
        } else if constexpr (std::is_same_v<T, int>) {
            n = arg;
        } else if constexpr (std::is_same_v<T, bool>) {
            n = arg;
        } else if constexpr (std::is_same_v<T, std::string>) {
            n = arg;
        } else if constexpr (std::is_same_v<T, ArrayData>) {
            YAML::Node seq(YAML::NodeType::Sequence);
            for (const auto& item : arg) seq.push_back(toYaml(item));
            n = seq;
        } else if constexpr (std::is_same_v<T, ObjectData>) {
            YAML::Node map(YAML::NodeType::Map);
            for (const auto& kv : arg) map[kv.first] = toYaml(kv.second);
            n = map;
        } }, static_cast<const std::variant<std::monostate, int, bool, std::string, ArrayData, ObjectData> &>(v));
    return n;
}

YAML::Node InMemoryDataCommit::toYamlFieldsMap(const std::map<std::string, FieldValue> &fields)
{
    YAML::Node map(YAML::NodeType::Map);
    for (const auto &kv : fields)
    {
        map[kv.first] = toYaml(kv.second);
    }
    return map;
}

void InMemoryDataCommit::writeNode(const std::string &filename,
                                   const NodeSnapshot &node)
{
    // Throw if node key id is null
    if (!node.key.id.has_value())
        throw std::runtime_error("Node key id is null");
    YAML::Node root(YAML::NodeType::Map);
    root["_profile"] = node.key.profile;
    root["_id"] = *node.key.id;
    if (node.label.has_value())
        root["_label"] = *node.label;
    if (node.parent_id.has_value())
        root["_parent_id"] = *node.parent_id;

    // expand fields
    for (const auto &kv : node.fields)
    {
        root[kv.first] = toYaml(kv.second);
    }

    YAML::Emitter out;
    out << root;
    files_[filename] = out.c_str();
}

void InMemoryDataCommit::deleteNode(const std::string &filename)
{
    files_.erase(filename);
}
