#pragma once
#include "FieldSchema.h"
#include <memory>

class ArrayFieldSchema : public FieldSchema
{
public:
    ArrayFieldSchema(std::string name, bool required,
                     std::unique_ptr<FieldSchema> items,
                     std::string meta = "",
                     std::string description = "");

    FieldType type() const override { return FieldType::Array; }
    const FieldSchema *items() const { return items_.get(); }

private:
    std::unique_ptr<FieldSchema> items_;
};