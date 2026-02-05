class BooleanFieldSchema : public FieldSchema
{
public:
    BooleanFieldSchema(std::string name,
                       bool required,
                       std::string meta = "",
                       std::string description = "",
                       std::optional<bool> defaultValue = std::nullopt)
        : FieldSchema(std::move(name), required, std::move(meta), std::move(description)),
          default_(defaultValue) {}

    FieldType type() const override { return FieldType::Boolean; }
    const std::optional<bool> &defaultValue() const { return default_; }

private:
    std::optional<bool> default_;
};
