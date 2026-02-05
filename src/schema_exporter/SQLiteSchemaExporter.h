#pragma once
#include "ISchemaExporter.h"
#include "NodeSchema.h"
#include "FieldSchema.h"
#include "StringFieldSchema.h"
#include "IntegerFieldSchema.h"
#include "BooleanFieldSchema.h"
#include "EnumFieldSchema.h"
#include "ReferenceFieldSchema.h"
#include "ArrayFieldSchema.h"
#include "ObjectFieldSchema.h"

#include <sstream>
#include <unordered_map>
#include <algorithm>

inline std::string refArrayValueCol(const std::string &owner,
                                    const std::string &field,
                                    const std::string &target)
{
    return (owner == target) ? (field + "$id") : (target + "$id");
}

static inline std::string joinNameSQLiteExporter(const std::string &prefix, const std::string &sub)
{
    return prefix.empty() ? sub : (prefix + "$" + sub);
}

class SQLiteSchemaExporter : public ISchemaExporter
{
public:
    std::string exportSchema(const SchemaRegistry &registry) const override
    {
        std::ostringstream out;

        // child -> parent mapping
        std::unordered_map<std::string, std::string> childToParent;
        for (const auto &[_, schemaPtr] : registry.schemas())
        {
            const NodeSchema *schema = schemaPtr.get();
            for (const auto &[childName, childSchema] : schema->children())
            {
                (void)childName;
                childToParent[childSchema->profileName()] = schema->profileName();
            }
        }

        // main tables
        for (const auto &[_, schemaPtr] : registry.schemas())
        {
            out << createMainTable(schemaPtr.get(), childToParent) << "\n\n";
        }

        // array side-tables
        for (const auto &[_, schemaPtr] : registry.schemas())
        {
            const NodeSchema *schema = schemaPtr.get();
            for (const auto &[fname, fptr] : schema->fields())
            {
                handleArrayOrNested(out, schema->profileName(), fname, fptr.get());
            }
        }

        return out.str();
    }

private:
    std::string createMainTable(
        const NodeSchema *schema,
        const std::unordered_map<std::string, std::string> &childToParent) const
    {
        std::vector<std::string> columns;
        std::vector<std::string> fks;

        const std::string prof = schema->profileName();
        const bool isChild = (childToParent.find(prof) != childToParent.end());

        // --- reserved cols ---
        columns.push_back("_id TEXT PRIMARY KEY NOT NULL");
        columns.push_back("_label TEXT NOT NULL");

        if (isChild)
        {
            // Children must carry parent identity + profile
            columns.push_back("_parent_id TEXT NOT NULL");
            columns.push_back("_parent_profile TEXT NOT NULL");
        }
        else
        {
            // Roots must not have any parent info
            columns.push_back("_parent_id TEXT CHECK(_parent_id IS NULL)");
            columns.push_back("_parent_profile TEXT CHECK(_parent_profile IS NULL)");
        }

        // --- schema fields ---
        for (const auto &[fname, fptr] : schema->fields())
            emitFieldColumn(schema->profileName(), fname, fptr.get(), "", columns, fks);

        // --- emit SQL ---
        std::ostringstream out;
        out << "CREATE TABLE " << schema->profileName() << " (\n";
        for (size_t i = 0; i < columns.size(); ++i)
        {
            out << "    " << columns[i];
            if (i + 1 < columns.size() || !fks.empty())
                out << ",";
            out << "\n";
        }
        for (size_t i = 0; i < fks.size(); ++i)
        {
            out << "    " << fks[i];
            if (i + 1 < fks.size())
                out << ",";
            out << "\n";
        }
        out << ") STRICT;";
        return out.str();
    }

    // --- recursive column emission for main tables
    void emitFieldColumn(const std::string &owner,
                         const std::string &fname,
                         const FieldSchema *field,
                         const std::string &prefix,
                         std::vector<std::string> &columns,
                         std::vector<std::string> &fks) const
    {
        const std::string colName = joinNameSQLiteExporter(prefix, fname);

        switch (field->type())
        {
        case FieldType::String:
        {
            auto f = dynamic_cast<const StringFieldSchema *>(field);
            std::ostringstream c;
            c << colName << " TEXT";
            if (field->required())
                c << " NOT NULL";
            if (f->defaultValue())
                c << " DEFAULT '" << *f->defaultValue() << "'";
            columns.push_back(c.str());
            break;
        }
        case FieldType::Integer:
        {
            auto f = dynamic_cast<const IntegerFieldSchema *>(field);
            std::ostringstream c;
            c << colName << " INTEGER";
            if (field->required())
                c << " NOT NULL";
            if (f->defaultValue())
                c << " DEFAULT " << *f->defaultValue();
            columns.push_back(c.str());
            break;
        }
        case FieldType::Boolean:
        {
            auto f = dynamic_cast<const BooleanFieldSchema *>(field);
            std::ostringstream c;
            c << colName << " INTEGER";
            if (field->required())
                c << " NOT NULL";
            if (f->defaultValue())
                c << " DEFAULT " << (*f->defaultValue() ? 1 : 0);
            c << " CHECK(" << colName << " IN (0,1))";
            columns.push_back(c.str());
            break;
        }
        case FieldType::Enum:
        {
            auto e = dynamic_cast<const EnumFieldSchema *>(field);
            std::ostringstream c;
            c << colName << " TEXT";
            if (field->required())
                c << " NOT NULL";
            if (e->defaultValue())
                c << " DEFAULT '" << *e->defaultValue() << "'";
            if (!e->values().empty())
            {
                c << " CHECK(" << colName << " IN (";
                for (size_t i = 0; i < e->values().size(); ++i)
                {
                    if (i)
                        c << ",";
                    c << "'" << e->values()[i] << "'";
                }
                c << "))";
            }
            columns.push_back(c.str());
            break;
        }
        case FieldType::Reference:
        {
            auto ref = dynamic_cast<const ReferenceFieldSchema *>(field);
            std::ostringstream c;
            c << colName << " TEXT";
            if (field->required())
                c << " NOT NULL";
            columns.push_back(c.str());
            // Keep reference FK with SET NULL behavior for soft deletion of targets
            fks.push_back("FOREIGN KEY(" + colName + ") REFERENCES " +
                          ref->target() + "(_id) ON DELETE SET NULL");
            break;
        }
        case FieldType::Object:
        {
            auto obj = dynamic_cast<const ObjectFieldSchema *>(field);
            for (const auto &sub : obj->fields())
                emitFieldColumn(owner, sub->name(), sub.get(), colName, columns, fks);
            break;
        }
        case FieldType::Array:
            // arrays are emitted as side-tables
            break;
        }
    }

    // --- recursively detect arrays / nested
    void handleArrayOrNested(std::ostringstream &out,
                             const std::string &owner,
                             const std::string &fname,
                             const FieldSchema *field,
                             const std::string &prefix = "") const
    {
        try
        {
            if (field->type() == FieldType::Array)
            {
                auto arr = dynamic_cast<const ArrayFieldSchema *>(field);
                const auto *items = arr->items();
                if (items->type() == FieldType::Array)
                    throw std::runtime_error("Nested arrays (arrays-of-arrays)");

                handleArrayField(out, owner, fname, arr);
                return;
            }

            if (field->type() == FieldType::Object)
            {
                auto obj = dynamic_cast<const ObjectFieldSchema *>(field);
                for (const auto &sub : obj->fields())
                    handleArrayOrNested(out, owner, joinNameSQLiteExporter(fname, sub->name()), sub.get(), prefix);
            }
        }
        catch (const std::exception &e)
        {
            std::ostringstream err;
            err << "Error exporting profile '" << owner
                << "' at field '" << joinNameSQLiteExporter(prefix, fname)
                << "': " << e.what();
            throw std::runtime_error(err.str());
        }
    }

    // --- generate SQL for array fields (including nested object items)
    void handleArrayField(std::ostringstream &out,
                          const std::string &owner,
                          const std::string &fname,
                          const ArrayFieldSchema *arr) const
    {
        try
        {
            const FieldSchema *items = arr->items();
            std::vector<std::string> columns;
            std::vector<std::string> fks;

            columns.push_back("id INTEGER PRIMARY KEY AUTOINCREMENT");
            columns.push_back(owner + "_id TEXT NOT NULL");
            // Side tables should cascade with their owner row
            fks.push_back("FOREIGN KEY(" + owner + "_id) REFERENCES " + owner + "(_id) ON DELETE CASCADE");

            switch (items->type())
            {
            case FieldType::String:
            {
                auto f = dynamic_cast<const StringFieldSchema *>(items);
                std::ostringstream c;
                c << "value TEXT";
                if (f->defaultValue())
                    c << " DEFAULT '" << *f->defaultValue() << "'";
                columns.push_back(c.str());
                break;
            }
            case FieldType::Integer:
            {
                auto f = dynamic_cast<const IntegerFieldSchema *>(items);
                std::ostringstream c;
                c << "value INTEGER";
                if (f->defaultValue())
                    c << " DEFAULT " << *f->defaultValue();
                columns.push_back(c.str());
                break;
            }
            case FieldType::Boolean:
            {
                auto f = dynamic_cast<const BooleanFieldSchema *>(items);
                std::ostringstream c;
                c << "value INTEGER CHECK(value IN (0,1))";
                if (f->defaultValue())
                    c << " DEFAULT " << (*f->defaultValue() ? 1 : 0);
                columns.push_back(c.str());
                break;
            }
            case FieldType::Enum:
            {
                auto e = dynamic_cast<const EnumFieldSchema *>(items);
                std::ostringstream col;
                col << "value TEXT";
                if (e->defaultValue())
                    col << " DEFAULT '" << *(e->defaultValue()) << "'";
                if (!e->values().empty())
                {
                    col << " CHECK(value IN (";
                    for (size_t i = 0; i < e->values().size(); ++i)
                    {
                        if (i)
                            col << ",";
                        col << "'" << e->values()[i] << "'";
                    }
                    col << "))";
                }
                columns.push_back(col.str());
                break;
            }
            case FieldType::Reference:
            {
                auto ref = dynamic_cast<const ReferenceFieldSchema *>(items);
                const std::string valueCol = refArrayValueCol(owner, fname, ref->target());
                columns.push_back(valueCol + " TEXT");
                fks.push_back("FOREIGN KEY(" + valueCol + ") REFERENCES " +
                              ref->target() + "(_id) ON DELETE SET NULL");
                break;
            }
            case FieldType::Object:
            {
                auto obj = dynamic_cast<const ObjectFieldSchema *>(items);
                for (const auto &sub : obj->fields())
                {
                    const std::string cname = sub->name();
                    std::ostringstream col;
                    switch (sub->type())
                    {
                    case FieldType::String:
                    {
                        auto f = dynamic_cast<const StringFieldSchema *>(sub.get());
                        col << cname << " TEXT";
                        if (f->defaultValue())
                            col << " DEFAULT '" << *f->defaultValue() << "'";
                        break;
                    }
                    case FieldType::Integer:
                    {
                        auto f = dynamic_cast<const IntegerFieldSchema *>(sub.get());
                        col << cname << " INTEGER";
                        if (f->defaultValue())
                            col << " DEFAULT " << *f->defaultValue();
                        break;
                    }
                    case FieldType::Boolean:
                    {
                        auto f = dynamic_cast<const BooleanFieldSchema *>(sub.get());
                        col << cname << " INTEGER CHECK(" << cname << " IN (0,1))";
                        if (f->defaultValue())
                            col << " DEFAULT " << (*f->defaultValue() ? 1 : 0);
                        break;
                    }
                    case FieldType::Enum:
                    {
                        auto e = dynamic_cast<const EnumFieldSchema *>(sub.get());
                        col << cname << " TEXT";
                        if (e->defaultValue())
                            col << " DEFAULT '" << *(e->defaultValue()) << "'";
                        if (!e->values().empty())
                        {
                            col << " CHECK(" << cname << " IN (";
                            for (size_t i = 0; i < e->values().size(); ++i)
                            {
                                if (i)
                                    col << ",";
                                col << "'" << e->values()[i] << "'";
                            }
                            col << "))";
                        }
                        break;
                    }
                    case FieldType::Reference:
                    {
                        auto r = dynamic_cast<const ReferenceFieldSchema *>(sub.get());
                        col << cname << " TEXT";
                        fks.push_back("FOREIGN KEY(" + cname + ") REFERENCES " +
                                      r->target() + "(_id) ON DELETE SET NULL");
                        break;
                    }
                    default:
                        throw std::runtime_error("Nested object/array inside array<object>");
                    }

                    if (sub->required())
                        col << " NOT NULL";
                    columns.push_back(col.str());
                }
                break;
            }
            case FieldType::Array:
                throw std::runtime_error("Nested arrays (array-of-array)");
            }

            std::ostringstream tbl;
            tbl << "CREATE TABLE " << owner << "$" << fname << " (\n";
            for (size_t i = 0; i < columns.size(); ++i)
            {
                tbl << "    " << columns[i];
                if (i + 1 < columns.size() || !fks.empty())
                    tbl << ",";
                tbl << "\n";
            }
            for (size_t i = 0; i < fks.size(); ++i)
            {
                tbl << "    " << fks[i];
                if (i + 1 < fks.size())
                    tbl << ",";
                tbl << "\n";
            }
            tbl << ") STRICT;";
            out << tbl.str();
        }
        catch (const std::exception &e)
        {
            std::ostringstream err;
            err << "Error exporting profile '" << owner
                << "' array field '" << fname << "': "
                << e.what();
            throw std::runtime_error(err.str());
        }
    }
};
