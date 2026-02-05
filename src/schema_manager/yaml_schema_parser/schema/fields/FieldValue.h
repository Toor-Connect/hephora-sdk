// src/schema_manager/yaml_schema_parser/schema/fields/FieldValue.h
#pragma once
#include <variant>
#include <string>
#include <vector>
#include <map>

// Forward declaration
struct FieldValue;

using ObjectData = std::map<std::string, FieldValue>;
using ArrayData = std::vector<FieldValue>;

struct FieldValue : std::variant<std::monostate, int, bool, std::string, ArrayData, ObjectData>
{
    using variant::variant;

    // ---- type predicates ----
    bool isNull() const { return std::holds_alternative<std::monostate>(*this); }
    bool isInteger() const { return std::holds_alternative<int>(*this); }
    bool isBoolean() const { return std::holds_alternative<bool>(*this); }
    bool isString() const { return std::holds_alternative<std::string>(*this); }
    bool isArray() const { return std::holds_alternative<ArrayData>(*this); }
    bool isObject() const { return std::holds_alternative<ObjectData>(*this); }

    // ---- accessors (const) ----
    int asInteger() const { return std::get<int>(*this); }
    bool asBoolean() const { return std::get<bool>(*this); }
    const std::string &asString() const { return std::get<std::string>(*this); }
    const ArrayData &asArray() const { return std::get<ArrayData>(*this); }
    const ObjectData &asObject() const { return std::get<ObjectData>(*this); }

    // ---- accessors (mutable) ----
    ArrayData &asArray() { return std::get<ArrayData>(*this); }
    ObjectData &asObject() { return std::get<ObjectData>(*this); }

    std::string getType() const
    {
        if (isInteger())
        {
            return "integer";
        }
        if (isBoolean())
        {
            return "boolean";
        }
        if (isString())
        {
            return "string";
        }
        if (isArray())
        {
            return "array";
        }
        if (isObject())
        {
            return "object";
        }
        return "unknown";
    }
};
