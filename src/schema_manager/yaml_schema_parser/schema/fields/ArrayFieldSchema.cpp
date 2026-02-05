#include "ArrayFieldSchema.h"
#include "ObjectFieldSchema.h"
#include <stdexcept>

ArrayFieldSchema::ArrayFieldSchema(std::string name, bool required,
                                   std::unique_ptr<FieldSchema> items,
                                   std::string meta,
                                   std::string description)
    : FieldSchema(std::move(name), required, std::move(meta), std::move(description)),
      items_(std::move(items))
{
    // Constraint: arrays of objects cannot contain arrays
    if (items_->type() == FieldType::Object)
    {
        const auto *objectField = dynamic_cast<const ObjectFieldSchema *>(items_.get());
        if (objectField)
        {
            for (const auto &sub : objectField->fields())
            {
                if (sub->type() == FieldType::Array)
                {
                    throw std::invalid_argument(
                        "Invalid schema: nested arrays inside objects are not allowed");
                }
            }
        }
    }
}