#include "JSONSchemaExporter.h"
#include "StringFieldSchema.h"
#include "IntegerFieldSchema.h"
#include "BooleanFieldSchema.h"
#include "EnumFieldSchema.h"
#include "ReferenceFieldSchema.h"
#include "ArrayFieldSchema.h"
#include "ObjectFieldSchema.h"

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <yaml-cpp/yaml.h>

JSONSchemaExporter::~JSONSchemaExporter() = default;

static std::string extractAliasFromMeta(const std::string &meta_raw)
{
    if (meta_raw.empty())
        return "";
    try
    {
        YAML::Node meta = YAML::Load(meta_raw);
        if (meta["alias"])
            return meta["alias"].as<std::string>();
    }
    catch (...)
    {
    }
    return "";
}

// ---------- small helpers ----------
void JSONSchemaExporter::indent(std::ostream &os, int n)
{
    for (int i = 0; i < n; ++i)
        os.put(' ');
}

std::string JSONSchemaExporter::jsonEscape(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20)
            {
                char buf[7];
                std::snprintf(buf, sizeof(buf), "\\u%04x", (int)c);
                out += buf;
            }
            else
                out.push_back((char)c);
        }
    }
    return out;
}

void JSONSchemaExporter::emitSimpleTypeProp(std::ostream &out, const std::string &name,
                                            const std::string &type, int indentLvl)
{
    indent(out, indentLvl);
    out << "\"" << jsonEscape(name) << "\":{\"type\":\"" << jsonEscape(type) << "\"}";
}

void JSONSchemaExporter::emitKV(std::ostream &out, const std::string &k, const std::string &v,
                                int indentLvl, bool &firstMember)
{
    indent(out, indentLvl);
    out << (firstMember ? "" : ",\n")
        << "\"" << jsonEscape(k) << "\":\"" << jsonEscape(v) << "\"";
    firstMember = false;
}

// ---------- main entry ----------
std::string JSONSchemaExporter::exportSchema(const SchemaRegistry &registry) const
{
    std::ostringstream out;
    out << "{\n";
    out << "  \"$schema\":\"https://json-schema.org/draft/2020-12/schema\",\n";
    out << "  \"$id\":\"hephora.bundle.json\",\n";

    if (auto root = registry.root())
        out << "  \"$ref\":\"#/$defs/" << jsonEscape(root->profileName()) << "\",\n";

    out << "  \"$defs\": {\n";

    std::vector<std::pair<std::string, std::shared_ptr<NodeSchema>>> profiles(
        registry.schemas().begin(), registry.schemas().end());
    std::sort(profiles.begin(), profiles.end(),
              [](const auto &a, const auto &b)
              { return a.first < b.first; });

    for (size_t i = 0; i < profiles.size(); ++i)
    {
        const NodeSchema *schema = profiles[i].second.get();
        out << "    \"" << jsonEscape(schema->profileName()) << "\": {\n";
        emitProfileSchema(out, schema, 6);
        out << "    }" << (i + 1 < profiles.size() ? "," : "") << "\n";
    }

    out << "  }\n";
    out << "}\n";
    return out.str();
}

// ---------- profile ----------
void JSONSchemaExporter::emitProfileSchema(std::ostream &out, const NodeSchema *schema, int baseIndent) const
{
    bool firstMember = true;

    emitKV(out, "type", "object", baseIndent, firstMember);
    emitKV(out, "title", extractAliasFromMeta(schema->meta()), baseIndent, firstMember);
    if (!schema->description().empty())
        emitKV(out, "description", schema->description(), baseIndent, firstMember);

    indent(out, baseIndent);
    out << (firstMember ? "" : ",\n");
    firstMember = false;
    out << "\"properties\":{\n";

    // reserved
    emitSimpleTypeProp(out, "_id", "string", baseIndent + 2);
    out << ",\n";
    emitSimpleTypeProp(out, "_label", "string", baseIndent + 2);
    out << ",\n";
    emitSimpleTypeProp(out, "_parent_id", "string", baseIndent + 2);

    // fields (stable order)
    std::vector<std::string> fieldNames;
    fieldNames.reserve(schema->fields().size());
    for (const auto &kv : schema->fields())
        fieldNames.push_back(kv.first);
    std::sort(fieldNames.begin(), fieldNames.end());

    for (const auto &fname : fieldNames)
    {
        const FieldSchema *f = schema->getField(fname);
        out << ",\n";
        indent(out, baseIndent + 2);
        out << "\"" << jsonEscape(fname) << "\":";
        emitField(out, f, baseIndent + 2);
    }

    // children as arrays of $ref
    for (auto it = schema->children().begin(); it != schema->children().end(); ++it)
    {
        const std::string &childName = it->first;
        const NodeSchema *child = it->second.get();
        out << ",\n";
        indent(out, baseIndent + 2);
        out << "\"" << jsonEscape(childName) << "\":{\"type\":\"array\",\"items\":{\"$ref\":\"#/$defs/"
            << jsonEscape(child->profileName()) << "\"}}";
    }

    out << "\n";
    indent(out, baseIndent);
    out << "}"; // end properties

    // required
    std::vector<std::string> req;
    req.push_back("_id");
    for (const auto &fname : fieldNames)
    {
        const FieldSchema *f = schema->getField(fname);
        if (f && f->required())
            req.push_back(fname);
    }
    if (!req.empty())
    {
        out << ",\n";
        indent(out, baseIndent);
        out << "\"required\":[";
        for (size_t i = 0; i < req.size(); ++i)
        {
            if (i)
                out << ",";
            out << "\"" << jsonEscape(req[i]) << "\"";
        }
        out << "]";
    }
    out << "\n";
}

// ---------- field ----------
void JSONSchemaExporter::emitField(std::ostream &out, const FieldSchema *field, int indentLvl) const
{
    switch (field->type())
    {
    case FieldType::String:
    {
        auto f = dynamic_cast<const StringFieldSchema *>(field);
        out << "{\"type\":\"string\"";
        if (f->defaultValue())
            out << ",\"default\":\"" << jsonEscape(*f->defaultValue()) << "\"";
        std::string alias = extractAliasFromMeta(field->meta());
        if (!alias.empty())
            out << ",\"title\":\"" << jsonEscape(alias) << "\"";

        if (!field->description().empty())
            out << ",\"description\":\"" << jsonEscape(field->description()) << "\"";
        out << "}";
        break;
    }
    case FieldType::Integer:
    {
        auto f = dynamic_cast<const IntegerFieldSchema *>(field);
        out << "{\"type\":\"integer\"";
        if (f->defaultValue())
            out << ",\"default\":" << *f->defaultValue();
        std::string alias = extractAliasFromMeta(field->meta());
        if (!alias.empty())
            out << ",\"title\":\"" << jsonEscape(alias) << "\"";

        if (!field->description().empty())
            out << ",\"description\":\"" << jsonEscape(field->description()) << "\"";
        out << "}";
        break;
    }
    case FieldType::Boolean:
    {
        auto f = dynamic_cast<const BooleanFieldSchema *>(field);
        out << "{\"type\":\"boolean\"";
        if (f->defaultValue())
            out << ",\"default\":" << (*f->defaultValue() ? "true" : "false");
        std::string alias = extractAliasFromMeta(field->meta());
        if (!alias.empty())
            out << ",\"title\":\"" << jsonEscape(alias) << "\"";

        if (!field->description().empty())
            out << ",\"description\":\"" << jsonEscape(field->description()) << "\"";
        out << "}";
        break;
    }
    case FieldType::Enum:
    {
        auto e = dynamic_cast<const EnumFieldSchema *>(field);
        out << "{\"type\":\"string\",\"enum\":[";
        for (size_t i = 0; i < e->values().size(); ++i)
        {
            if (i)
                out << ",";
            out << "\"" << jsonEscape(e->values()[i]) << "\"";
        }
        if (e->defaultValue())
            out << "],\"default\":\"" << jsonEscape(*e->defaultValue()) << "\"";
        else
            out << "]";
        std::string alias = extractAliasFromMeta(field->meta());
        if (!alias.empty())
            out << ",\"title\":\"" << jsonEscape(alias) << "\"";

        if (!field->description().empty())
            out << ",\"description\":\"" << jsonEscape(field->description()) << "\"";
        out << "}";
        break;
    }
    case FieldType::Reference:
    {
        auto r = dynamic_cast<const ReferenceFieldSchema *>(field);
        out << "{\"type\":\"string\",\"description\":\"ref to "
            << jsonEscape(r->target()) << "._id\""; // <<< fixed to \"._id\"
        std::string alias = extractAliasFromMeta(field->meta());
        if (!alias.empty())
            out << ",\"title\":\"" << jsonEscape(alias) << "\"";

        if (!field->description().empty())
            out << ",\"description\":\"" << jsonEscape(field->description()) << "\"";
        out << "}";
        break;
    }
    case FieldType::Object:
    {
        auto o = dynamic_cast<const ObjectFieldSchema *>(field);
        out << "{\"type\":\"object\",\"properties\":{\n";
        std::vector<const FieldSchema *> subs;
        subs.reserve(o->fields().size());
        for (const auto &p : o->fields())
            subs.push_back(p.get());
        std::sort(subs.begin(), subs.end(), [](const FieldSchema *a, const FieldSchema *b)
                  { return a->name() < b->name(); });
        for (size_t i = 0; i < subs.size(); ++i)
        {
            const FieldSchema *sf = subs[i];
            indent(out, indentLvl + 2);
            out << "\"" << jsonEscape(sf->name()) << "\":";
            emitField(out, sf, indentLvl + 2);
            if (i + 1 < subs.size())
                out << ",";
            out << "\n";
        }
        indent(out, indentLvl);
        out << "}";
        std::vector<std::string> req;
        for (const auto *sf : subs)
            if (sf->required())
                req.push_back(sf->name());
        if (!req.empty())
        {
            out << ",\"required\":[";
            for (size_t i = 0; i < req.size(); ++i)
            {
                if (i)
                    out << ",";
                out << "\"" << jsonEscape(req[i]) << "\"";
            }
            out << "]";
        }
        std::string alias = extractAliasFromMeta(field->meta());
        if (!alias.empty())
            out << ",\"title\":\"" << jsonEscape(alias) << "\"";

        if (!field->description().empty())
            out << ",\"description\":\"" << jsonEscape(field->description()) << "\"";
        out << "}";
        break;
    }
    case FieldType::Array:
    {
        auto a = dynamic_cast<const ArrayFieldSchema *>(field);
        out << "{\"type\":\"array\",\"items\":";
        emitField(out, a->items(), indentLvl); // recurse
        std::string alias = extractAliasFromMeta(field->meta());
        if (!alias.empty())
            out << ",\"title\":\"" << jsonEscape(alias) << "\"";

        if (!field->description().empty())
            out << ",\"description\":\"" << jsonEscape(field->description()) << "\"";
        out << "}";
        break;
    }
    }
}
