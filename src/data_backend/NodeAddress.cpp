#include "NodeAddress.h"
#include <nlohmann/json.hpp>
#include "YamlDataEncoder.h"
#include <yaml-cpp/yaml.h>

using json = nlohmann::json;

// Helper to convert FieldValue → json
static json fieldValueToJson(const FieldValue &v)
{
    if (v.isNull())
        return nullptr;
    if (v.isInteger())
        return v.asInteger();
    if (v.isBoolean())
        return v.asBoolean();
    if (v.isString())
        return v.asString();
    if (v.isArray())
    {
        json arr = json::array();
        for (const auto &el : v.asArray())
            arr.push_back(fieldValueToJson(el));
        return arr;
    }
    if (v.isObject())
    {
        json obj = json::object();
        for (const auto &kv : v.asObject())
            obj[kv.first] = fieldValueToJson(kv.second);
        return obj;
    }
    return nullptr; // fallback
}

std::string NodeSnapshot::toJson() const
{
    json j;

    // identity
    j["profile"] = key.profile;
    if (key.id.has_value())
        j["id"] = *key.id;

    // optional metadata
    if (label.has_value())
        j["label"] = *label;
    if (parent_id.has_value())
        j["parent"] = *parent_id;

    // fields (always include)
    json jf = json::object();
    for (const auto &kv : fields)
        jf[kv.first] = fieldValueToJson(kv.second);
    j["fields"] = jf;

    return j.dump(2); // pretty-print with 2 spaces
}

std::string NodeSnapshot::toYaml() const
{
    YamlDataEncoder enc;
    YAML::Node root = enc.encodeNode(*this);

    YAML::Emitter out;
    out.SetIndent(2);
    out.SetMapFormat(YAML::Block);
    out.SetSeqFormat(YAML::Block);
    out << root;

    return std::string(out.c_str());
}