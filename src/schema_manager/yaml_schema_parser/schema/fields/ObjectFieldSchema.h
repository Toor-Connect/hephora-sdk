#pragma once
#include "FieldSchema.h"
#include <memory>
#include <vector>

class ObjectFieldSchema : public FieldSchema
{
public:
    ObjectFieldSchema(std::string name, bool required,
                      std::vector<std::unique_ptr<FieldSchema>> fields,
                      std::string meta = "",
                      std::string description = "");

    FieldType type() const override { return FieldType::Object; }
    const std::vector<std::unique_ptr<FieldSchema>> &fields() const { return fields_; }

    // ---- helper: find subfield by name ----
    const FieldSchema *getField(const std::string &name) const
    {
        for (const auto &f : fields_)
        {
            if (f->name() == name)
                return f.get();
        }
        return nullptr;
    }

private:
    std::vector<std::unique_ptr<FieldSchema>> fields_;
};