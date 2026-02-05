#pragma once
#include <algorithm>

class EnumFieldSchema : public FieldSchema
{
public:
    EnumFieldSchema(std::string name,
                    bool required,
                    std::vector<std::string> values,
                    std::string meta = "",
                    std::string description = "",
                    std::optional<std::string> defaultValue = std::nullopt)
        : FieldSchema(std::move(name), required, std::move(meta), std::move(description)),
          values_(std::move(values)),
          default_(std::move(defaultValue))
    {
        if (default_.has_value())
        {
            auto it = std::find(values_.begin(), values_.end(), *default_);
            if (it == values_.end())
            {
                throw std::invalid_argument("EnumFieldSchema default value '" + *default_ +
                                            "' is not in allowed values");
            }
        }
    }

    FieldType type() const override { return FieldType::Enum; }
    const std::vector<std::string> &values() const { return values_; }
    const std::optional<std::string> &defaultValue() const { return default_; }

private:
    std::vector<std::string> values_;
    std::optional<std::string> default_;
};
