#pragma once

#include <string>
#include <vector>
#include <memory>
#include "FieldValue.h"

enum class FieldType
{
    String,
    Integer,
    Boolean,
    Enum,
    Object,
    Array,
    Reference
};

class ValidationError
{
public:
    ValidationError(std::string field, std::string message)
        : field_(std::move(field)), message_(std::move(message)) {}

    const std::string &field() const { return field_; }
    const std::string &message() const { return message_; }

private:
    std::string field_;
    std::string message_;
};

class FieldSchema
{
public:
    FieldSchema(std::string name, bool required = false,
                std::string meta = "",
                std::string description = "")
        : name_(std::move(name)),
          required_(required),
          meta_(std::move(meta)),
          description_(std::move(description)) {}

    virtual ~FieldSchema() = default;

    virtual FieldType type() const = 0;

    const std::string &name() const { return name_; }
    bool required() const { return required_; }
    const std::string &meta() const { return meta_; }
    const std::string &description() const { return description_; }

protected:
    std::string name_;
    bool required_;
    std::string meta_;
    std::string description_;
};
