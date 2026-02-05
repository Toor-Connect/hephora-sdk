class IntegerFieldSchema : public FieldSchema
{
public:
    IntegerFieldSchema(std::string name,
                       bool required,
                       std::string meta = "",
                       std::string description = "",
                       std::optional<int> defaultValue = std::nullopt)
        : FieldSchema(std::move(name), required, std::move(meta), std::move(description)),
          default_(defaultValue) {}

    FieldType type() const override { return FieldType::Integer; }
    const std::optional<int> &defaultValue() const { return default_; }

private:
    std::optional<int> default_;
};
