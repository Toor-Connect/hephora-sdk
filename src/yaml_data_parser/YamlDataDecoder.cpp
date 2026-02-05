// src/yaml_data_parser/YamlDataDecoder.cpp
#include "YamlDataDecoder.h"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <sstream>

using std::string;

static inline bool isReservedKey(const std::string &k)
{
    return k == "_profile" || k == "_id" || k == "_label" || k == "_parent_id";
}

std::vector<YamlDataDecoder::DecodedNode> YamlDataDecoder::decode(
    const std::unordered_map<std::string, YAML::Node> &docs,
    const SchemaRegistry &registry)
{
    // Sort only the filenames; never copy/move YAML::Node into an intermediate container.
    std::vector<std::string> names;
    names.reserve(docs.size());
    for (const auto &kv : docs)
        names.push_back(kv.first);
    std::sort(names.begin(), names.end());

    std::vector<DecodedNode> out;
    out.reserve(names.size());
    for (const auto &name : names)
    {
        const auto it = docs.find(name);
        if (it == docs.end())
        {
            throw std::runtime_error("YamlDataDecoder: key disappeared during decode: " + name);
        }
        out.emplace_back(decodeOne(name, it->second, registry));
    }
    return out;
}

YamlDataDecoder::DecodedNode
YamlDataDecoder::decodeOne(const std::string &filename,
                           const YAML::Node &root,
                           const SchemaRegistry &registry)
{
    if (!root || !root.IsMap())
    {
        std::ostringstream oss;
        oss << "Top-level YAML must be a mapping in [" << filename << "]";
        throw std::runtime_error(oss.str());
    }

    DecodedNode dn;
    dn.source = filename;

    auto getScalar = [&](const char *key, bool required) -> std::string
    {
        const YAML::Node n = root[key];
        if (!n)
        {
            if (required)
            {
                std::ostringstream oss;
                oss << "Missing required '" << key << "' in [" << filename << "]";
                throw std::runtime_error(oss.str());
            }
            return {};
        }
        if (!n.IsScalar())
        {
            std::ostringstream oss;
            oss << "Reserved field '" << key << "' must be a scalar string in [" << filename << "]";
            throw std::runtime_error(oss.str());
        }
        return n.as<std::string>();
    };

    dn.profile = getScalar("_profile", /*required*/ true);
    dn.id = getScalar("_id", /*required*/ true);
    dn.label = getScalar("_label", /*required*/ false);
    dn.parent_id = getScalar("_parent_id", /*required*/ false);

    // Lookup schema for this profile
    const NodeSchema *schema = registry.getSchema(dn.profile);
    if (!schema)
    {
        std::ostringstream oss;
        oss << "Unknown profile '" << dn.profile << "' in [" << filename << "]";
        throw std::runtime_error(oss.str());
    }

    // Decode fields
    NodeInstance inst; // children remain empty; data files are single nodes
    for (auto it = root.begin(); it != root.end(); ++it)
    {
        const std::string key = it->first.as<std::string>();
        if (isReservedKey(key))
            continue;

        const FieldSchema *fs = schema->getField(key);
        if (!fs)
        {
            std::ostringstream oss;
            oss << "Unknown field '" << key << "' for profile '" << dn.profile
                << "' in [" << filename << "]";
            throw std::runtime_error(oss.str());
        }

        const YAML::Node &valNode = it->second;
        const std::string path = dn.profile + "." + key;
        inst.fields[key] = decodeFieldValue(valNode, fs, path);
    }

    // Add schema default values for missing primitive fields where available.
    for (const auto &kv : schema->fields())
    {
        const std::string &fname = kv.first;
        const FieldSchema *fs = kv.second.get();
        if (inst.fields.find(fname) != inst.fields.end())
            continue;

        switch (fs->type())
        {
        case FieldType::String:
        {
            auto s = dynamic_cast<const StringFieldSchema *>(fs);
            if (s && s->defaultValue())
                inst.fields[fname] = FieldValue(*s->defaultValue());
            break;
        }
        case FieldType::Integer:
        {
            auto i = dynamic_cast<const IntegerFieldSchema *>(fs);
            if (i && i->defaultValue())
                inst.fields[fname] = FieldValue(*i->defaultValue());
            break;
        }
        case FieldType::Boolean:
        {
            auto b = dynamic_cast<const BooleanFieldSchema *>(fs);
            if (b && b->defaultValue())
                inst.fields[fname] = FieldValue(*b->defaultValue());
            break;
        }
        case FieldType::Enum:
        {
            auto e = dynamic_cast<const EnumFieldSchema *>(fs);
            if (e && e->defaultValue())
                inst.fields[fname] = FieldValue(*e->defaultValue());
            break;
        }
        default:
            break; // no auto-defaulting for object/array/reference here
        }
    }

    dn.instance = std::move(inst);
    return dn;
}

FieldValue YamlDataDecoder::decodeFieldValue(const YAML::Node &node,
                                             const FieldSchema *schema,
                                             const std::string &path)
{
    switch (schema->type())
    {
    case FieldType::String:
    {
        if (!node.IsScalar())
            typeError(path, "expected string (scalar)");
        return FieldValue(node.as<std::string>());
    }
    case FieldType::Integer:
    {
        if (!node.IsScalar())
            typeError(path, "expected integer (scalar)");
        try
        {
            return FieldValue(node.as<int>());
        }
        catch (const YAML::BadConversion &)
        {
            typeError(path, "invalid integer literal");
        }
    }
    case FieldType::Boolean:
    {
        if (!node.IsScalar())
            typeError(path, "expected boolean (scalar)");
        try
        {
            return FieldValue(node.as<bool>());
        }
        catch (const YAML::BadConversion &)
        {
            typeError(path, "invalid boolean literal");
        }
    }
    case FieldType::Enum:
    {
        if (!node.IsScalar())
            typeError(path, "expected enum (string scalar)");
        const auto *e = dynamic_cast<const EnumFieldSchema *>(schema);
        const std::string s = node.as<std::string>();
        if (e)
        {
            const auto &vals = e->values();
            if (std::find(vals.begin(), vals.end(), s) == vals.end())
            {
                std::ostringstream oss;
                oss << "enum value '" << s << "' not in [";
                for (size_t i = 0; i < vals.size(); ++i)
                {
                    if (i)
                        oss << ",";
                    oss << vals[i];
                }
                oss << "]";
                typeError(path, oss.str());
            }
        }
        return FieldValue(s);
    }
    case FieldType::Reference:
    {
        if (!node.IsScalar())
            typeError(path, "expected reference id (string scalar)");
        return FieldValue(node.as<std::string>()); // referential integrity checked later
    }
    case FieldType::Object:
    {
        const auto *os = dynamic_cast<const ObjectFieldSchema *>(schema);
        return decodeObject(node, os, path);
    }
    case FieldType::Array:
    {
        const auto *as = dynamic_cast<const ArrayFieldSchema *>(schema);
        return decodeArray(node, as, path);
    }
    }
    typeError(path, "unknown field type");
    return FieldValue(); // unreachable
}

FieldValue YamlDataDecoder::decodeObject(const YAML::Node &node,
                                         const ObjectFieldSchema *os,
                                         const std::string &path)
{
    if (!node || !node.IsMap())
        typeError(path, "expected object (mapping)");

    // Helper to find a subfield schema by name in ObjectFieldSchema::fields()
    auto findSubField = [&](const std::string &key) -> const FieldSchema *
    {
        for (const auto &up : os->fields())
        {
            if (up && up->name() == key)
                return up.get();
        }
        return nullptr;
    };

    std::map<std::string, FieldValue> obj; // stable order

    // Decode provided keys and detect unknown ones
    for (auto it = node.begin(); it != node.end(); ++it)
    {
        const std::string key = it->first.as<std::string>();
        const FieldSchema *fs = findSubField(key);
        if (!fs)
        {
            std::ostringstream oss;
            oss << "unknown object field '" << key << "'";
            typeError(path, oss.str());
        }
        obj[key] = decodeFieldValue(it->second, fs, path + "." + key);
    }

    // Defaults for missing primitives inside the object
    for (const auto &up : os->fields())
    {
        const FieldSchema *fs = up.get();
        const std::string &fname = fs->name();
        if (obj.find(fname) != obj.end())
            continue;

        switch (fs->type())
        {
        case FieldType::String:
        {
            auto s = dynamic_cast<const StringFieldSchema *>(fs);
            if (s && s->defaultValue())
                obj[fname] = FieldValue(*s->defaultValue());
            break;
        }
        case FieldType::Integer:
        {
            auto i = dynamic_cast<const IntegerFieldSchema *>(fs);
            if (i && i->defaultValue())
                obj[fname] = FieldValue(*i->defaultValue());
            break;
        }
        case FieldType::Boolean:
        {
            auto b = dynamic_cast<const BooleanFieldSchema *>(fs);
            if (b && b->defaultValue())
                obj[fname] = FieldValue(*b->defaultValue());
            break;
        }
        case FieldType::Enum:
        {
            auto e = dynamic_cast<const EnumFieldSchema *>(fs);
            if (e && e->defaultValue())
                obj[fname] = FieldValue(*e->defaultValue());
            break;
        }
        default:
            break;
        }
    }

    return FieldValue(obj);
}

FieldValue YamlDataDecoder::decodeArray(const YAML::Node &node,
                                        const ArrayFieldSchema *as,
                                        const std::string &path)
{
    if (!node || !node.IsSequence())
        typeError(path, "expected array (sequence)");
    std::vector<FieldValue> arr;
    arr.reserve(node.size());
    const FieldSchema *item = as->items();

    for (size_t i = 0; i < node.size(); ++i)
    {
        const YAML::Node &el = node[i];
        arr.push_back(decodeFieldValue(el, item, path + "[" + std::to_string(i) + "]"));
    }

    return FieldValue(arr);
}

[[noreturn]] void YamlDataDecoder::typeError(const std::string &path,
                                             const std::string &msg)
{
    std::ostringstream oss;
    oss << "Type error at " << path << ": " << msg;
    throw std::runtime_error(oss.str());
}
