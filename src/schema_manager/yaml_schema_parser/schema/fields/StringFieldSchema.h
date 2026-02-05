#pragma once
#include "FieldSchema.h"
#include <optional>

class StringFieldSchema : public FieldSchema
{
public:
    StringFieldSchema(std::string name,
                      bool required,
                      std::string meta = "",
                      std::string description = "",
                      std::optional<std::string> defaultValue = std::nullopt)
        : FieldSchema(std::move(name), required, std::move(meta), std::move(description)),
          default_(std::move(defaultValue)) {}

    FieldType type() const override { return FieldType::String; }

    const std::optional<std::string> &defaultValue() const { return default_; }

private:
    std::optional<std::string> default_;
};
