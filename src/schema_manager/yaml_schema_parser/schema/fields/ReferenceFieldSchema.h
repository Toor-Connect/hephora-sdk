#pragma once

#include "FieldSchema.h"

class ReferenceFieldSchema : public FieldSchema
{
public:
    ReferenceFieldSchema(std::string name, bool required,
                         std::string target,
                         std::string meta = "",
                         std::string description = "")
        : FieldSchema(std::move(name), required, std::move(meta), std::move(description)),
          target_(std::move(target)) {}

    FieldType type() const override { return FieldType::Reference; }
    const std::string &target() const { return target_; }

private:
    std::string target_;
};