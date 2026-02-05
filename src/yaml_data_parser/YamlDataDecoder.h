// src/data/YamlDataDecoder.h
#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <yaml-cpp/yaml.h>

#include "NodeInstance.h"
#include "SchemaRegistry.h"
#include "FieldSchema.h"
#include "StringFieldSchema.h"
#include "IntegerFieldSchema.h"
#include "BooleanFieldSchema.h"
#include "EnumFieldSchema.h"
#include "ReferenceFieldSchema.h"
#include "ArrayFieldSchema.h"
#include "ObjectFieldSchema.h"

// Decodes YAML data docs (filename -> YAML::Node) into typed records,
// using SchemaRegistry for field typing. Throws std::runtime_error on errors.
class YamlDataDecoder
{
public:
    struct DecodedNode
    {
        std::string source;    // filename key from the input dictionary
        std::string profile;   // _profile
        std::string id;        // _id
        std::string label;     // _label (optional)
        std::string parent_id; // _parent_id (optional)
        NodeInstance instance; // fields decoded per schema; children left empty
    };

    // Main entry: decode all docs. Iteration is deterministic (filenames sorted).
    static std::vector<DecodedNode> decode(
        const std::unordered_map<std::string, YAML::Node> &docs,
        const SchemaRegistry &registry);

private:
    // Helpers
    static DecodedNode decodeOne(const std::string &filename,
                                 const YAML::Node &root,
                                 const SchemaRegistry &registry);

    static FieldValue decodeFieldValue(const YAML::Node &node,
                                       const FieldSchema *schema,
                                       const std::string &path);

    static FieldValue decodeObject(const YAML::Node &node,
                                   const ObjectFieldSchema *os,
                                   const std::string &path);

    static FieldValue decodeArray(const YAML::Node &node,
                                  const ArrayFieldSchema *as,
                                  const std::string &path);

    static std::string scalarAsStringOrThrow(const YAML::Node &n,
                                             const std::string &path,
                                             const char *what);

    [[noreturn]] static void typeError(const std::string &path,
                                       const std::string &msg);
};
