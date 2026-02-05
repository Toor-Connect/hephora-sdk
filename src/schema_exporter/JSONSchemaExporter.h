// JsonSchemaExporter.h
#pragma once
#include "ISchemaExporter.h"
#include "NodeSchema.h"
#include "FieldSchema.h"

class JSONSchemaExporter : public ISchemaExporter
{
public:
    ~JSONSchemaExporter() override; // key function (out-of-line def in .cpp)
    std::string exportSchema(const SchemaRegistry &) const override;

private:
    // helpers (defined in .cpp)
    static void indent(std::ostream &, int);
    static std::string jsonEscape(const std::string &);
    static void emitSimpleTypeProp(std::ostream &, const std::string &name,
                                   const std::string &type, int indentLvl);
    static void emitKV(std::ostream &, const std::string &k, const std::string &v,
                       int indentLvl, bool &firstMember);
    void emitProfileSchema(std::ostream &, const NodeSchema *schema, int baseIndent) const;
    void emitField(std::ostream &, const FieldSchema *field, int indentLvl) const;
};