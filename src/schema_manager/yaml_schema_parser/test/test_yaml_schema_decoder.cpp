#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <yaml-cpp/yaml.h>

#include "YamlSchemaDecoder.h"
#include "SchemaRegistry.h"
#include "NodeSchema.h"
#include "FieldSchema.h"
#include "StringFieldSchema.h"
#include "IntegerFieldSchema.h"
#include "BooleanFieldSchema.h"
#include "EnumFieldSchema.h"
#include "ObjectFieldSchema.h"
#include "ArrayFieldSchema.h"
#include "ReferenceFieldSchema.h"

// --- Helpers ---
template <typename T>
bool isType(const FieldSchema *field)
{
  return dynamic_cast<const T *>(field) != nullptr;
}

// ------------------- PROJECT -------------------
TEST_CASE("YamlSchemaDecoder decodes project.yaml correctly", "[yaml]")
{
  const std::string yamlStr = R"(
name: project
kind: root
description: Top-level project profile containing requirements and attachments
meta:
  alias: Project

fields:
  project_name:
    type: string
    required: true
    meta:
      alias: Project Name

  version:
    type: string
    required: true
    meta:
      alias: Version

  description:
    type: string
    required: false
    meta:
      alias: Description

  owner:
    type: string
    required: false
    meta:
      alias: Owner

children:
  requirements:
    node: requirement
    description: List of requirements belonging to this project

  attachments:
    node: attachment
    description: List of attachments directly associated with this project
)";

  YAML::Node node = YAML::Load(yamlStr);
  SchemaRegistry registry;
  YamlSchemaDecoder::decodeProfile(node, registry);

  auto *schema = registry.getSchema("project");
  REQUIRE(schema != nullptr);

  // Metadata
  REQUIRE(schema->profileName() == "project");
  YAML::Node metaNode = YAML::Load(schema->meta());
  REQUIRE(metaNode["alias"]);
  REQUIRE(metaNode["alias"].as<std::string>() == "Project");
  REQUIRE(schema->kind() == NodeKind::Root);
  REQUIRE(schema->description().find("Top-level project") != std::string::npos);

  // Fields
  const auto &fields = schema->fields();

  // project_name
  auto *f1 = dynamic_cast<StringFieldSchema *>(fields.at("project_name").get());
  REQUIRE(f1 != nullptr);
  REQUIRE(f1->required());
  {
    YAML::Node meta = YAML::Load(f1->meta());
    REQUIRE(meta["alias"]);
    REQUIRE(meta["alias"].as<std::string>() == "Project Name");
  }

  // version
  auto *f2 = dynamic_cast<StringFieldSchema *>(fields.at("version").get());
  REQUIRE(f2 != nullptr);
  REQUIRE(f2->required());
  {
    YAML::Node meta = YAML::Load(f2->meta());
    REQUIRE(meta["alias"]);
    REQUIRE(meta["alias"].as<std::string>() == "Version");
  }

  // description
  auto *f3 = dynamic_cast<StringFieldSchema *>(fields.at("description").get());
  REQUIRE(f3 != nullptr);
  REQUIRE_FALSE(f3->required());
  {
    YAML::Node meta = YAML::Load(f3->meta());
    REQUIRE(meta["alias"]);
    REQUIRE(meta["alias"].as<std::string>() == "Description");
  }

  // owner
  auto *f4 = dynamic_cast<StringFieldSchema *>(fields.at("owner").get());
  REQUIRE(f4 != nullptr);
  REQUIRE_FALSE(f4->required());
  {
    YAML::Node meta = YAML::Load(f4->meta());
    REQUIRE(meta["alias"]);
    REQUIRE(meta["alias"].as<std::string>() == "Owner");
  }

  // Children
  const auto &children = schema->children();
  REQUIRE(children.find("requirements") != children.end());
  REQUIRE(children.find("attachments") != children.end());
  REQUIRE(children.at("requirements")->profileName() == "requirement");
  REQUIRE(children.at("attachments")->profileName() == "attachment");
}

// ------------------- REQUIREMENT -------------------
TEST_CASE("YamlSchemaDecoder decodes requirement.yaml correctly", "[yaml]")
{
  const std::string yamlStr = R"(
name: requirement
kind: node
description: Defines a requirement with links to tests and components
meta:
  alias: Requirement

fields:
  title:
    type: string
    required: true
    meta:
      alias: Title

  priority:
    type: enum
    values: [low, medium, high]
    default: medium
    description: Priority level

  rationale:
    type: string
    required: false
    description: Reasoning behind requirement

  active:
    type: boolean

  specs:
    type: object
    fields:
      manufacturer:
        type: string
        default: ToorConnect
      warranty_years:
        type: integer
        default: 3

  tags:
    type: array
    items:
      type: string

  references:
    type: array
    items:
      type: reference
      target: attachment
    description: References from this requirement to attachments
)";

  YAML::Node node = YAML::Load(yamlStr);
  SchemaRegistry registry;
  YamlSchemaDecoder::decodeProfile(node, registry);

  auto *schema = registry.getSchema("requirement");
  REQUIRE(schema != nullptr);

  // Metadata
  REQUIRE(schema->profileName() == "requirement");
  YAML::Node metaNode = YAML::Load(schema->meta());
  REQUIRE(metaNode["alias"]);
  REQUIRE(metaNode["alias"].as<std::string>() == "Requirement");
  REQUIRE(schema->kind() == NodeKind::Node);

  // Fields
  const auto &fields = schema->fields();

  auto *title = dynamic_cast<StringFieldSchema *>(fields.at("title").get());
  REQUIRE(title != nullptr);
  REQUIRE(title->required());
  {
    YAML::Node meta = YAML::Load(title->meta());
    REQUIRE(meta["alias"]);
    REQUIRE(meta["alias"].as<std::string>() == "Title");
  }

  auto *priority = dynamic_cast<EnumFieldSchema *>(fields.at("priority").get());
  REQUIRE(priority != nullptr);
  REQUIRE(priority->values().size() == 3);
  REQUIRE(priority->defaultValue().value() == "medium");

  auto *rationale = dynamic_cast<StringFieldSchema *>(fields.at("rationale").get());
  REQUIRE(rationale != nullptr);
  REQUIRE_FALSE(rationale->required());

  auto *active = dynamic_cast<BooleanFieldSchema *>(fields.at("active").get());
  REQUIRE(active != nullptr);

  auto *specs = dynamic_cast<ObjectFieldSchema *>(fields.at("specs").get());
  REQUIRE(specs != nullptr);
  const auto &nested = specs->fields();
  REQUIRE(nested.size() == 2);

  auto *manufacturer = dynamic_cast<StringFieldSchema *>(nested[0].get());
  REQUIRE(manufacturer->defaultValue().value() == "ToorConnect");

  auto *warranty = dynamic_cast<IntegerFieldSchema *>(nested[1].get());
  REQUIRE(warranty->defaultValue().value() == 3);

  auto *tags = dynamic_cast<ArrayFieldSchema *>(fields.at("tags").get());
  REQUIRE(isType<StringFieldSchema>(tags->items()));

  auto *refs = dynamic_cast<ArrayFieldSchema *>(fields.at("references").get());
  auto *refField = dynamic_cast<const ReferenceFieldSchema *>(refs->items());
  REQUIRE(refField->target() == "attachment");
}

// ------------------- ATTACHMENT -------------------
TEST_CASE("YamlSchemaDecoder decodes attachment.yaml correctly", "[yaml]")
{
  const std::string yamlStr = R"(
name: attachment
kind: node
description: Defines an attachment linked to a requirement or project
meta:
  alias: Attachment

fields:
  filename:
    type: string
    required: true
    meta:
      alias: File Name

  filetype:
    type: enum
    values: [pdf, docx, xlsx, png, jpg, txt]
    required: true
    meta:
      alias: File Type

  path:
    type: string
    required: true
    meta:
      alias: File Path

  description:
    type: string
    required: false
    meta:
      alias: Description

  tags:
    type: array
    items:
      type: string
    meta:
      alias: Tags
)";

  YAML::Node node = YAML::Load(yamlStr);
  SchemaRegistry registry;
  YamlSchemaDecoder::decodeProfile(node, registry);

  auto *schema = registry.getSchema("attachment");
  REQUIRE(schema != nullptr);

  // Metadata
  REQUIRE(schema->profileName() == "attachment");
  YAML::Node metaNode = YAML::Load(schema->meta());
  REQUIRE(metaNode["alias"]);
  REQUIRE(metaNode["alias"].as<std::string>() == "Attachment");
  REQUIRE(schema->kind() == NodeKind::Node);

  // Fields
  const auto &fields = schema->fields();
  REQUIRE(dynamic_cast<StringFieldSchema *>(fields.at("filename").get()) != nullptr);
  REQUIRE(dynamic_cast<EnumFieldSchema *>(fields.at("filetype").get()) != nullptr);
  REQUIRE(dynamic_cast<StringFieldSchema *>(fields.at("path").get()) != nullptr);
  REQUIRE(dynamic_cast<StringFieldSchema *>(fields.at("description").get()) != nullptr);
  REQUIRE(dynamic_cast<ArrayFieldSchema *>(fields.at("tags").get()) != nullptr);
}

// ------------------- MULTIPLE PROFILES -------------------
TEST_CASE("YamlSchemaDecoder decodes multiple profiles together", "[yaml]")
{
  const std::string yamlProject = R"(
name: project
kind: root
description: Root schema
children:
  requirements:
    node: requirement
)";
  const std::string yamlRequirement = R"(
name: requirement
kind: node
description: Requirement schema
fields:
  title:
    type: string
)";
  const std::string yamlAttachment = R"(
name: attachment
kind: node
description: Attachment schema
fields:
  filename:
    type: string
)";

  YAML::Node n1 = YAML::Load(yamlProject);
  YAML::Node n2 = YAML::Load(yamlRequirement);
  YAML::Node n3 = YAML::Load(yamlAttachment);

  SchemaRegistry registry;
  YamlSchemaDecoder::decodeProfiles({n1, n2, n3}, registry);

  REQUIRE(registry.getSchema("project") != nullptr);
  REQUIRE(registry.getSchema("requirement") != nullptr);
  REQUIRE(registry.getSchema("attachment") != nullptr);

  REQUIRE(registry.root() != nullptr);
  REQUIRE(registry.root()->profileName() == "project");
}

// ------------------- ERROR CASES -------------------
TEST_CASE("YamlSchemaDecoder throws on unsupported field type", "[yaml]")
{
  const std::string yamlStr = R"(
name: invalid
kind: node
description: Invalid schema with unsupported type
fields:
  wrong:
    type: blob
)";
  YAML::Node node = YAML::Load(yamlStr);
  SchemaRegistry registry;

  REQUIRE_THROWS_AS(YamlSchemaDecoder::decodeProfile(node, registry), std::runtime_error);
}

TEST_CASE("YamlSchemaDecoder throws on enum without values", "[yaml]")
{
  const std::string yamlStr = R"(
name: invalid_enum
kind: node
description: Invalid schema with enum missing values
fields:
  bad_enum:
    type: enum
)";
  YAML::Node node = YAML::Load(yamlStr);
  SchemaRegistry registry;

  REQUIRE_THROWS_AS(YamlSchemaDecoder::decodeProfile(node, registry), std::runtime_error);
}

TEST_CASE("YamlSchemaDecoder throws on array without items", "[yaml]")
{
  const std::string yamlStr = R"(
name: invalid_array
kind: node
description: Invalid schema with array missing items
fields:
  bad_array:
    type: array
)";
  YAML::Node node = YAML::Load(yamlStr);
  SchemaRegistry registry;

  REQUIRE_THROWS_AS(YamlSchemaDecoder::decodeProfile(node, registry), std::runtime_error);
}

TEST_CASE("YamlSchemaDecoder throws on reference without target", "[yaml]")
{
  const std::string yamlStr = R"(
name: invalid_ref
kind: node
description: Invalid schema with reference missing target
fields:
  bad_ref:
    type: reference
)";
  YAML::Node node = YAML::Load(yamlStr);
  SchemaRegistry registry;

  REQUIRE_THROWS_AS(YamlSchemaDecoder::decodeProfile(node, registry), std::runtime_error);
}

TEST_CASE("YamlSchemaDecoder throws on duplicate schema names", "[yaml]")
{
  const std::string yamlStr = R"(
name: duplicate
kind: node
description: First schema
)";
  YAML::Node node = YAML::Load(yamlStr);
  SchemaRegistry registry;

  YamlSchemaDecoder::decodeProfile(node, registry);
  REQUIRE_THROWS_AS(YamlSchemaDecoder::decodeProfile(node, registry), std::runtime_error);
}

TEST_CASE("YamlSchemaDecoder throws when adding multiple roots", "[yaml][root]")
{
  const std::string yaml1 = R"(
name: project
kind: root
description: Root project
)";
  const std::string yaml2 = R"(
name: workspace
kind: root
description: Another root
)";
  YAML::Node n1 = YAML::Load(yaml1);
  YAML::Node n2 = YAML::Load(yaml2);

  SchemaRegistry registry;
  YamlSchemaDecoder::decodeProfile(n1, registry);

  REQUIRE_THROWS_WITH(
      YamlSchemaDecoder::decodeProfile(n2, registry),
      Catch::Matchers::ContainsSubstring("Multiple root schemas detected"));
}

TEST_CASE("YamlSchemaDecoder rejects field names containing '$'", "[yaml][invalid][fieldname]")
{
  const std::string yamlStr = R"(
name: bad_fieldname
kind: node
description: Schema with illegal '$' character in field name
fields:
  invalid$name:
    type: string
)";
  YAML::Node node = YAML::Load(yamlStr);
  SchemaRegistry registry;

  REQUIRE_THROWS_WITH(
      YamlSchemaDecoder::decodeProfile(node, registry),
      Catch::Matchers::ContainsSubstring("Invalid field name: '$'"));
}
