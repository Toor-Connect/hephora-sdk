#include "ObjectFieldSchema.h"
#include "ArrayFieldSchema.h"
#include <stdexcept>

ObjectFieldSchema::ObjectFieldSchema(std::string name, bool required,
                                     std::vector<std::unique_ptr<FieldSchema>> fields,
                                     std::string meta,
                                     std::string description)
    : FieldSchema(std::move(name), required, std::move(meta), std::move(description)),
      fields_(std::move(fields))
{
    // Constraint: objects can contain arrays, but only arrays of primitives OR arrays of flat objects
    for (const auto &child : fields_)
    {
        if (child->type() == FieldType::Array)
        {
            const auto *arrayField = dynamic_cast<const ArrayFieldSchema *>(child.get());
            if (arrayField && arrayField->items()->type() == FieldType::Object)
            {
                const auto *obj = dynamic_cast<const ObjectFieldSchema *>(arrayField->items());
                if (obj)
                {
                    for (const auto &sub : obj->fields())
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
    }
}