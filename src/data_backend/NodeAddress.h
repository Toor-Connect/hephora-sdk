#pragma once
#include <string>
#include <map>
#include <optional>
#include "FieldValue.h"

// Unique identity of a node in a profile
struct NodeKey
{
    std::string profile;           // e.g., "requirement"
    std::optional<std::string> id; // e.g., "R-0001" or std::nullopt for auto-ID

    // Define ordering so NodeKey can be used in ordered containers (std::set, std::map)
    bool operator<(const NodeKey &other) const
    {
        if (profile < other.profile)
            return true;
        if (profile > other.profile)
            return false;
        // same profile: handle optional id
        if (!id.has_value() && other.id.has_value())
            return true; // consider unspecified id (auto-ID) as less than specified id
        if (id.has_value() && !other.id.has_value())
            return false;
        if (!id.has_value() && !other.id.has_value())
            return false; // equal
        return id.value() < other.id.value();
    }

    bool operator==(const NodeKey &other) const
    {
        return profile == other.profile && id == other.id;
    }
};

// Full in-memory snapshot of a node (used for upsert/fetch/find)
struct NodeSnapshot
{
    NodeKey key;
    std::optional<std::string> label;         // _label (may be empty)
    std::optional<std::string> parent_id;     // _parent_id (may be empty)
    std::map<std::string, FieldValue> fields; // same shape as NodeInstance.fields
    std::string toJson() const;
    std::string toYaml() const;
};
