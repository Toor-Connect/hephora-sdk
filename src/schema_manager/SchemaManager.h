#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include "SchemaRegistry.h"
#include "YamlSchemaLoader.h"
#include "ReferenceFieldSchema.h"
#include "NodeInstance.h"

class SchemaManager
{
public:
    SchemaManager() = default;

    void loadSources(const std::unordered_map<std::string, std::string> &sources)
    {
        registry_.clear();
        YamlSchemaLoader::loadIntoRegistry(sources, registry_);
        validateSchemas();
    }

    const NodeSchema *getSchema(const std::string &name) const
    {
        return registry_.getSchema(name);
    }

    const NodeSchema *root() const
    {
        return registry_.root();
    }

    bool hasSchema(const std::string &name) const
    {
        return registry_.hasSchema(name);
    }

    void clear()
    {
        registry_.clear();
    }

    const SchemaRegistry &registry() const { return registry_; }
    SchemaRegistry &registry() { return registry_; }

    void validateSchemas()
    {
        if (!registry_.root())
        {
            throw std::runtime_error("Validation failed: No root schema defined");
        }

        for (auto &[schemaName, schemaPtr] : registry_.schemas())
        {
            NodeSchema *schema = schemaPtr.get();

            // --- Fix child placeholders ---
            for (auto &[childName, childSchemaPtr] : schema->children()) // now std::map
            {
                const std::string &targetName = childSchemaPtr->profileName();
                if (!registry_.hasSchema(targetName))
                {
                    throw std::runtime_error(
                        "Schema '" + schema->profileName() +
                        "' has unknown child schema: '" + targetName + "'");
                }

                // Rebind placeholder with real schema
                childSchemaPtr = registry_.schemas().at(targetName);
            }

            // --- Validate reference fields ---
            for (const auto &[fieldName, fieldPtr] : schema->fields())
            {
                if (auto ref = dynamic_cast<const ReferenceFieldSchema *>(fieldPtr.get()))
                {
                    if (!registry_.hasSchema(ref->target()))
                    {
                        throw std::runtime_error(
                            "Schema '" + schema->profileName() +
                            "' has reference field '" + fieldPtr->name() +
                            "' pointing to unknown target schema: '" + ref->target() + "'");
                    }
                }
            }
        }
    }

private:
    SchemaRegistry registry_;
};
