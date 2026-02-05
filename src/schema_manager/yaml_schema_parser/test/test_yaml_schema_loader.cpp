#include <catch2/catch_test_macros.hpp>
#include "YamlSchemaLoader.h"
#include "SchemaRegistry.h"
#include "NodeSchema.h"
#include "StringFieldSchema.h"
#include "IntegerFieldSchema.h"
#include "BooleanFieldSchema.h"
#include "EnumFieldSchema.h"
#include "ArrayFieldSchema.h"
#include "ReferenceFieldSchema.h"

TEST_CASE("YamlSchemaLoader loads simple valid sources", "[yaml][loader]")
{
    std::unordered_map<std::string, std::string> files = {
        {"project.yaml", R"(
name: project
kind: root
description: Root schema
meta:
    alias: Project
fields:
  title:
    type: string
    required: true
    default: Untitled
children:
  requirements:
    node: requirement
)"},
        {"requirement.yaml", R"(
name: requirement
kind: node
description: Requirement schema
fields:
  id:
    type: string
    default: REQ-000
  priority:
    type: enum
    values: [low, medium, high]
    default: medium
)"}};

    SchemaRegistry registry;
    REQUIRE_NOTHROW(YamlSchemaLoader::loadIntoRegistry(files, registry));

    auto *project = registry.getSchema("project");
    auto *req = registry.getSchema("requirement");

    REQUIRE(project != nullptr);
    REQUIRE(req != nullptr);

    // Metadata
    YAML::Node metaNode = YAML::Load(project->meta());
    REQUIRE(metaNode["alias"]);
    REQUIRE(metaNode["alias"].as<std::string>() == "Project");
    REQUIRE(project->kind() == NodeKind::Root);

    // Fields
    REQUIRE(project->fields().count("title") == 1);
    auto *title = dynamic_cast<StringFieldSchema *>(project->fields().at("title").get());
    REQUIRE(title != nullptr);
    REQUIRE(title->required());
    REQUIRE(title->defaultValue().has_value());
    REQUIRE(title->defaultValue().value() == "Untitled");

    // Children placeholder
    REQUIRE(project->children().count("requirements") == 1);
    REQUIRE(project->children().at("requirements")->profileName() == "requirement");

    // Requirement schema checks
    REQUIRE(req->fields().count("id") == 1);
    auto *idField = dynamic_cast<StringFieldSchema *>(req->fields().at("id").get());
    REQUIRE(idField != nullptr);
    REQUIRE(idField->defaultValue().has_value());
    REQUIRE(idField->defaultValue().value() == "REQ-000");

    auto *priority = dynamic_cast<EnumFieldSchema *>(req->fields().at("priority").get());
    REQUIRE(priority != nullptr);
    REQUIRE(priority->values().size() == 3);
    REQUIRE(priority->values()[0] == "low");
    REQUIRE(priority->defaultValue().has_value());
    REQUIRE(priority->defaultValue().value() == "medium");
}

TEST_CASE("YamlSchemaLoader parses reference fields correctly", "[yaml][loader]")
{
    std::unordered_map<std::string, std::string> files = {
        {"attachment.yaml", R"(
name: attachment
kind: node
description: Attachment schema
fields:
  filename:
    type: string
)"},
        {"requirement.yaml", R"(
name: requirement
kind: node
description: Requirement with refs
fields:
  references:
    type: array
    items:
      type: reference
      target: attachment
)"}};

    SchemaRegistry registry;
    YamlSchemaLoader::loadIntoRegistry(files, registry);

    auto *req = registry.getSchema("requirement");
    REQUIRE(req != nullptr);

    auto *refs = dynamic_cast<ArrayFieldSchema *>(req->fields().at("references").get());
    REQUIRE(refs != nullptr);

    auto *refField = dynamic_cast<const ReferenceFieldSchema *>(refs->items());
    REQUIRE(refField != nullptr);
    REQUIRE(refField->target() == "attachment");
}

TEST_CASE("YamlSchemaLoader throws on invalid YAML", "[yaml][loader]")
{
    std::unordered_map<std::string, std::string> files = {
        {"broken.yaml", "name: bad: : yaml\n"}};

    SchemaRegistry registry;
    REQUIRE_THROWS_AS(YamlSchemaLoader::loadIntoRegistry(files, registry), std::runtime_error);
}

TEST_CASE("YamlSchemaLoader throws on duplicate schema names across files", "[yaml][loader]")
{
    std::unordered_map<std::string, std::string> files = {
        {"a.yaml", "name: dup\nkind: node\ndescription: one\n"},
        {"b.yaml", "name: dup\nkind: node\ndescription: two\n"}};

    SchemaRegistry registry;
    REQUIRE_THROWS_AS(YamlSchemaLoader::loadIntoRegistry(files, registry), std::runtime_error);
}

TEST_CASE("YamlSchemaLoader throws on missing required keys", "[yaml][loader]")
{
    std::unordered_map<std::string, std::string> files = {
        {"missing.yaml", "kind: node\ndescription: Missing name\n"}};

    SchemaRegistry registry;
    REQUIRE_THROWS_AS(YamlSchemaLoader::loadIntoRegistry(files, registry), std::exception);
}

TEST_CASE("YamlSchemaLoader handles empty files gracefully", "[yaml][loader]")
{
    std::unordered_map<std::string, std::string> files = {
        {"empty.yaml", ""}};

    SchemaRegistry registry;
    REQUIRE_THROWS_AS(YamlSchemaLoader::loadIntoRegistry(files, registry), std::exception);
}
