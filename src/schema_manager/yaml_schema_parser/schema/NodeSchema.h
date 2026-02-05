#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <map>

#include "FieldSchema.h"
#include "NodeInstance.h"

struct CustomCommand
{
    std::string name;
    std::string description;
    std::string script_name;
    std::string alias;
};

enum class NodeKind
{
    Root,
    Node
};

class NodeSchema
{
public:
    NodeSchema(std::string name,
               NodeKind kind,
               std::string description,
               std::string meta = "", std::string rawYaml = "")
        : name_(std::move(name)),
          kind_(kind),
          description_(std::move(description)),
          meta_(std::move(meta)),
          rawYaml_(std::move(rawYaml)) {}
    const std::string &profileName() const { return name_; }
    NodeKind kind() const { return kind_; }
    const std::string &description() const { return description_; }
    const std::string &meta() const { return meta_; }

    bool addField(std::unique_ptr<FieldSchema> field)
    {
        const auto &fieldName = field->name();
        auto [it, inserted] = fields_.emplace(fieldName, std::move(field));
        return inserted;
    }

    const std::unordered_map<std::string, std::unique_ptr<FieldSchema>> &fields() const
    {
        return fields_;
    }

    FieldSchema *getField(const std::string &fieldName) const
    {
        auto it = fields_.find(fieldName);
        return (it != fields_.end()) ? it->second.get() : nullptr;
    }

    void addChild(const std::string &childName, std::shared_ptr<NodeSchema> childSchema)
    {
        children_[childName] = std::move(childSchema);
    }

    const std::map<std::string, std::shared_ptr<NodeSchema>> &children() const
    {
        return children_;
    }

    std::map<std::string, std::shared_ptr<NodeSchema>> &children()
    {
        return children_;
    }

    NodeSchema *getChild(const std::string &childName) const
    {
        auto it = children_.find(childName);
        return (it != children_.end()) ? it->second.get() : nullptr;
    }

    void addManualCustomCommand(const std::string &name, const CustomCommand &command)
    {
        manualCustomCommands_[name] = command;
    }
    void addOnCreateCustomCommand(const std::string &name, const CustomCommand &command)
    {
        onCreateCustomCommands_[name] = command;
    }
    void addOnUpdateCustomCommand(const std::string &name, const CustomCommand &command)
    {
        onUpdateCustomCommands_[name] = command;
    }
    void addBeforeUpdateCustomCommand(const std::string &name, const CustomCommand &command)
    {
        beforeUpdateCustomCommands_[name] = command;
    }
    const std::unordered_map<std::string, CustomCommand> &manualCustomCommands() const
    {
        return manualCustomCommands_;
    }
    const std::unordered_map<std::string, CustomCommand> &onCreateCustomCommands() const
    {
        return onCreateCustomCommands_;
    }
    const std::unordered_map<std::string, CustomCommand> &onUpdateCustomCommands() const
    {
        return onUpdateCustomCommands_;
    }
    const std::unordered_map<std::string, CustomCommand> &beforeUpdateCustomCommands() const
    {
        return beforeUpdateCustomCommands_;
    }

    const std::string &rawYaml() const { return rawYaml_; }

private:
    std::string name_;
    NodeKind kind_;
    std::string description_;
    std::string meta_;

    std::unordered_map<std::string, std::unique_ptr<FieldSchema>> fields_;
    std::map<std::string, std::shared_ptr<NodeSchema>> children_;
    std::unordered_map<std::string, CustomCommand> manualCustomCommands_;       // key: command name
    std::unordered_map<std::string, CustomCommand> onCreateCustomCommands_;     // key: command name
    std::unordered_map<std::string, CustomCommand> onUpdateCustomCommands_;     // key: command name
    std::unordered_map<std::string, CustomCommand> beforeUpdateCustomCommands_; // key: command name
    std::string rawYaml_;
};
