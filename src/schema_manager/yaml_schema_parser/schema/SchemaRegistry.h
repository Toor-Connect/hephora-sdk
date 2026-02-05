#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <stdexcept>

#include "NodeSchema.h"

class SchemaRegistry
{
public:
    bool addSchema(std::shared_ptr<NodeSchema> schema)
    {
        const auto &name = schema->profileName();
        auto [it, inserted] = schemas_.emplace(name, schema);
        if (!inserted)
            return false;

        if (schema->kind() == NodeKind::Root)
        {
            if (root_)
            {
                throw std::invalid_argument(
                    "Multiple root schemas detected: '" + root_->profileName() +
                    "' and '" + schema->profileName() + "'");
            }
            root_ = schema;
        }
        return true;
    }

    NodeSchema *getSchema(const std::string &name) const
    {
        auto it = schemas_.find(name);
        return (it != schemas_.end()) ? it->second.get() : nullptr;
    }

    const std::unordered_map<std::string, std::shared_ptr<NodeSchema>> &schemas() const
    {
        return schemas_;
    }

    NodeSchema *root() const
    {
        return root_.get();
    }

    bool hasSchema(const std::string &name) const
    {
        return schemas_.find(name) != schemas_.end();
    }

    void clear()
    {
        schemas_.clear();
        root_.reset();
    }

private:
    std::unordered_map<std::string, std::shared_ptr<NodeSchema>> schemas_;
    std::shared_ptr<NodeSchema> root_;
};
