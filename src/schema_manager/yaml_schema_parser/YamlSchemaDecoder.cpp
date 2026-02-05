#include "YamlSchemaDecoder.h"
#include "StringFieldSchema.h"
#include "IntegerFieldSchema.h"
#include "BooleanFieldSchema.h"
#include "EnumFieldSchema.h"
#include "ObjectFieldSchema.h"
#include "ArrayFieldSchema.h"
#include "ReferenceFieldSchema.h"

#include <stdexcept>

static std::unique_ptr<FieldSchema> makeFieldSchema(
    const std::string &fieldName,
    const YAML::Node &fieldDef)
{
    if (fieldName.find('$') != std::string::npos)
    {
        throw std::runtime_error("Invalid field name: '$' is reserved for internal purposes (" + fieldName + ")");
    }

    std::string type = fieldDef["type"].as<std::string>();
    bool required = fieldDef["required"] ? fieldDef["required"].as<bool>() : false;

    std::string meta;
    if (fieldDef["meta"])
    {
        std::stringstream ss;
        ss << fieldDef["meta"];
        meta = ss.str();
    }

    std::string description = fieldDef["description"] ? fieldDef["description"].as<std::string>() : "";

    if (type == "string")
    {
        std::optional<std::string> def = fieldDef["default"] ? std::make_optional(fieldDef["default"].as<std::string>()) : std::nullopt;
        return std::make_unique<StringFieldSchema>(fieldName, required, meta, description, def);
    }
    else if (type == "integer")
    {
        std::optional<int> def = fieldDef["default"] ? std::make_optional(fieldDef["default"].as<int>()) : std::nullopt;
        return std::make_unique<IntegerFieldSchema>(fieldName, required, meta, description, def);
    }
    else if (type == "boolean")
    {
        std::optional<bool> def = fieldDef["default"] ? std::make_optional(fieldDef["default"].as<bool>()) : std::nullopt;
        return std::make_unique<BooleanFieldSchema>(fieldName, required, meta, description, def);
    }
    else if (type == "enum")
    {
        if (!fieldDef["values"])
            throw std::runtime_error("Enum field missing values: " + fieldName);

        auto values = fieldDef["values"].as<std::vector<std::string>>();
        std::optional<std::string> def = fieldDef["default"] ? std::make_optional(fieldDef["default"].as<std::string>()) : std::nullopt;

        return std::make_unique<EnumFieldSchema>(fieldName, required, values, meta, description, def);
    }
    else if (type == "object")
    {
        if (!fieldDef["fields"])
            throw std::runtime_error("Object field missing nested fields: " + fieldName);

        std::vector<std::unique_ptr<FieldSchema>> nestedFields;
        for (auto sub : fieldDef["fields"])
        {
            std::string subName = sub.first.as<std::string>();
            YAML::Node subDef = sub.second;
            nestedFields.push_back(makeFieldSchema(subName, subDef));
        }
        return std::make_unique<ObjectFieldSchema>(fieldName, required, std::move(nestedFields), meta, description);
    }
    else if (type == "array")
    {
        if (!fieldDef["items"])
            throw std::runtime_error("Array field missing items: " + fieldName);

        YAML::Node itemDef = fieldDef["items"];
        auto itemField = makeFieldSchema("item", itemDef);
        return std::make_unique<ArrayFieldSchema>(fieldName, required, std::move(itemField), meta, description);
    }
    else if (type == "reference")
    {
        if (!fieldDef["target"])
            throw std::runtime_error("Reference field missing target: " + fieldName);

        std::string target = fieldDef["target"].as<std::string>();
        return std::make_unique<ReferenceFieldSchema>(fieldName, required, target, meta, description);
    }
    else
    {
        throw std::runtime_error("Unsupported field type: " + type);
    }
}

void YamlSchemaDecoder::decodeProfile(const YAML::Node &node, SchemaRegistry &registry)
{
    std::string name = node["name"].as<std::string>();
    std::string kindStr = node["kind"].as<std::string>();
    NodeKind kind = (kindStr == "root") ? NodeKind::Root : NodeKind::Node;
    std::string description = node["description"].as<std::string>();
    std::string meta;
    if (node["meta"])
    {
        std::stringstream ss;
        ss << node["meta"];
        meta = ss.str();
    }
    std::string rawYaml;
    {
        std::stringstream ss;
        ss << node;
        rawYaml = ss.str();
    }

    auto schema = std::make_shared<NodeSchema>(name, kind, description, meta, rawYaml);

    if (node["fields"])
    {
        for (auto it : node["fields"])
        {
            std::string fieldName = it.first.as<std::string>();
            YAML::Node fieldDef = it.second;
            try
            {
                schema->addField(makeFieldSchema(fieldName, fieldDef));
            }
            catch (const std::exception &e)
            {
                throw std::runtime_error("Error processing profile '" + name + "'. In field '" + fieldName + "': " + e.what());
            }
        }
    }

    if (node["children"])
    {
        for (auto it : node["children"])
        {
            std::string childName = it.first.as<std::string>();
            std::string targetProfile = it.second["node"].as<std::string>();

            auto placeholder = std::make_shared<NodeSchema>(targetProfile, NodeKind::Node, "Unresolved placeholder");
            schema->addChild(childName, placeholder);
        }
    }

    if (node["commands"])
    {
        for (auto it : node["commands"])
        {
            std::string cmdName = it.first.as<std::string>();
            YAML::Node cmdDef = it.second;

            CustomCommand command;
            command.name = cmdName;
            command.description = cmdDef["description"] ? cmdDef["description"].as<std::string>() : "";
            command.script_name = cmdDef["script"] ? cmdDef["script"].as<std::string>() : "";
            if (command.script_name.empty())
            {
                throw std::runtime_error("Command missing script_name: " + cmdName);
            }
            command.alias = cmdDef["alias"] ? cmdDef["alias"].as<std::string>() : "";

            std::string trigger = cmdDef["trigger"] ? cmdDef["trigger"].as<std::string>() : "manual";
            if (trigger == "manual")
            {
                schema->addManualCustomCommand(cmdName, command);
            }
            else if (trigger == "on_create")
            {
                schema->addOnCreateCustomCommand(cmdName, command);
            }
            else if (trigger == "on_update")
            {
                schema->addOnUpdateCustomCommand(cmdName, command);
            }
            else if (trigger == "before_update")
            {
                schema->addBeforeUpdateCustomCommand(cmdName, command);
            }
            else
            {
                throw std::runtime_error("Unknown command trigger: " + trigger);
            }
        }
    }

    if (!registry.addSchema(schema))
    {
        throw std::runtime_error("Duplicate schema name: " + name);
    }
}

void YamlSchemaDecoder::decodeProfiles(const std::vector<YAML::Node> &nodes, SchemaRegistry &registry)
{
    for (const auto &node : nodes)
    {
        decodeProfile(node, registry);
    }
}
