#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp> // <-- needed for ContainsSubstring

#include "SchemaManager.h"
#include "NodeSchema.h"
#include "StringFieldSchema.h"
#include "IntegerFieldSchema.h"
#include "EnumFieldSchema.h"
#include "BooleanFieldSchema.h"
#include "ObjectFieldSchema.h"
#include "ArrayFieldSchema.h"
#include "ReferenceFieldSchema.h"

// Helper
template <typename T>
bool isType(const FieldSchema *field)
{
  return dynamic_cast<const T *>(field) != nullptr;
}

TEST_CASE("SchemaManager loads full example profiles", "[SchemaManager]")
{
  SchemaManager mgr;

  std::unordered_map<std::string, std::string> files = {
      {"project.yaml", R"(
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
)"},
      {"requirement.yaml", R"(
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
    default: true

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
)"},
      {"attachment.yaml", R"(
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
)"}};

  mgr.loadSources(files);

  SECTION("Project profile is loaded correctly")
  {
    auto *project = mgr.getSchema("project");
    REQUIRE(project != nullptr);
    YAML::Node metaNode = YAML::Load(project->meta());
    REQUIRE(metaNode["alias"]);
    REQUIRE(metaNode["alias"].as<std::string>() == "Project");
    REQUIRE(project->kind() == NodeKind::Root);

    const auto &fields = project->fields();
    auto *name = dynamic_cast<StringFieldSchema *>(fields.at("project_name").get());
    REQUIRE(name != nullptr);
    REQUIRE(name->required());
    REQUIRE_FALSE(name->defaultValue().has_value());

    auto *ver = dynamic_cast<StringFieldSchema *>(fields.at("version").get());
    REQUIRE(ver != nullptr);
    REQUIRE(ver->required());
    REQUIRE_FALSE(ver->defaultValue().has_value());

    auto *desc = dynamic_cast<StringFieldSchema *>(fields.at("description").get());
    REQUIRE(desc != nullptr);
    REQUIRE_FALSE(desc->required());
    REQUIRE_FALSE(desc->defaultValue().has_value());

    auto *owner = dynamic_cast<StringFieldSchema *>(fields.at("owner").get());
    REQUIRE(owner != nullptr);
    REQUIRE_FALSE(owner->required());
    REQUIRE_FALSE(owner->defaultValue().has_value());

    const auto &children = project->children();
    REQUIRE(children.find("requirements") != children.end());
    REQUIRE(children.find("attachments") != children.end());
  }

  SECTION("Requirement profile is loaded correctly (with defaults)")
  {
    auto *req = mgr.getSchema("requirement");
    REQUIRE(req != nullptr);
    YAML::Node metaNode = YAML::Load(req->meta());
    REQUIRE(metaNode["alias"]);
    REQUIRE(metaNode["alias"].as<std::string>() == "Requirement");

    const auto &fields = req->fields();

    auto *title = dynamic_cast<StringFieldSchema *>(fields.at("title").get());
    REQUIRE(title != nullptr);
    REQUIRE(title->required());
    REQUIRE_FALSE(title->defaultValue().has_value());

    auto *priority = dynamic_cast<const EnumFieldSchema *>(fields.at("priority").get());
    REQUIRE(priority != nullptr);
    REQUIRE(priority->values().size() == 3);
    REQUIRE(priority->defaultValue().has_value());
    REQUIRE(priority->defaultValue().value() == "medium");

    auto *rationale = dynamic_cast<StringFieldSchema *>(fields.at("rationale").get());
    REQUIRE(rationale != nullptr);
    REQUIRE_FALSE(rationale->required());
    REQUIRE_FALSE(rationale->defaultValue().has_value());

    auto *active = dynamic_cast<BooleanFieldSchema *>(fields.at("active").get());
    REQUIRE(active != nullptr);
    REQUIRE(active->defaultValue().has_value());
    REQUIRE(active->defaultValue().value() == true);

    auto *specs = dynamic_cast<const ObjectFieldSchema *>(fields.at("specs").get());
    REQUIRE(specs != nullptr);
    REQUIRE(specs->fields().size() == 2);

    auto *manufacturer = dynamic_cast<StringFieldSchema *>(specs->fields()[0].get());
    REQUIRE(manufacturer != nullptr);
    REQUIRE(manufacturer->defaultValue().has_value());
    REQUIRE(manufacturer->defaultValue().value() == "ToorConnect");

    auto *warranty = dynamic_cast<IntegerFieldSchema *>(specs->fields()[1].get());
    REQUIRE(warranty != nullptr);
    REQUIRE(warranty->defaultValue().has_value());
    REQUIRE(warranty->defaultValue().value() == 3);

    auto *tags = dynamic_cast<const ArrayFieldSchema *>(fields.at("tags").get());
    REQUIRE(tags != nullptr);
    REQUIRE(isType<StringFieldSchema>(tags->items()));

    auto *refs = dynamic_cast<const ArrayFieldSchema *>(fields.at("references").get());
    REQUIRE(refs != nullptr);
    auto *refField = dynamic_cast<const ReferenceFieldSchema *>(refs->items());
    REQUIRE(refField != nullptr);
    REQUIRE(refField->target() == "attachment");
  }

  SECTION("Attachment profile is loaded correctly")
  {
    auto *att = mgr.getSchema("attachment");
    REQUIRE(att != nullptr);
    YAML::Node metaNode = YAML::Load(att->meta());
    REQUIRE(metaNode["alias"]);
    REQUIRE(metaNode["alias"].as<std::string>() == "Attachment");

    const auto &fields = att->fields();
    auto *filename = dynamic_cast<StringFieldSchema *>(fields.at("filename").get());
    REQUIRE(filename != nullptr);
    REQUIRE(filename->required());
    REQUIRE_FALSE(filename->defaultValue().has_value());

    auto *filetype = dynamic_cast<const EnumFieldSchema *>(fields.at("filetype").get());
    REQUIRE(filetype != nullptr);
    REQUIRE(filetype->values().size() == 6);
    REQUIRE_FALSE(filetype->defaultValue().has_value());

    auto *path = dynamic_cast<StringFieldSchema *>(fields.at("path").get());
    REQUIRE(path != nullptr);
    REQUIRE(path->required());
    REQUIRE_FALSE(path->defaultValue().has_value());

    auto *desc = dynamic_cast<StringFieldSchema *>(fields.at("description").get());
    REQUIRE(desc != nullptr);
    REQUIRE_FALSE(desc->required());
    REQUIRE_FALSE(desc->defaultValue().has_value());

    auto *tags = dynamic_cast<const ArrayFieldSchema *>(fields.at("tags").get());
    REQUIRE(tags != nullptr);
    REQUIRE(isType<StringFieldSchema>(tags->items()));
  }

  SECTION("root() returns the single root profile")
  {
    auto *root = mgr.root();
    REQUIRE(root != nullptr);
    REQUIRE(root->profileName() == "project");
  }
}

TEST_CASE("SchemaManager clear removes all schemas", "[SchemaManager]")
{
  SchemaManager mgr;

  std::unordered_map<std::string, std::string> files = {
      {"project.yaml", R"(
name: project
kind: root
description: Root schema
)"}};

  mgr.loadSources(files);
  REQUIRE(mgr.hasSchema("project"));
  REQUIRE(mgr.root() != nullptr);

  mgr.clear();
  REQUIRE_FALSE(mgr.hasSchema("project"));
  REQUIRE(mgr.root() == nullptr);
}

TEST_CASE("SchemaManager validation catches errors", "[SchemaManager][Validation]")
{
  SECTION("Fails if no root schema is defined")
  {
    SchemaManager mgr;

    std::unordered_map<std::string, std::string> files = {
        {"requirement.yaml", R"(
name: requirement
kind: node
description: Requirement without root
fields:
  title:
    type: string
)"}};

    REQUIRE_THROWS_WITH(
        mgr.loadSources(files),
        Catch::Matchers::ContainsSubstring("No root schema defined"));
  }

  SECTION("Fails if child points to unknown schema")
  {
    SchemaManager mgr;

    std::unordered_map<std::string, std::string> files = {
        {"project.yaml", R"(
name: project
kind: root
description: Root with bad child
children:
  requirements:
    node: requirement
)"}};

    REQUIRE_THROWS_WITH(
        mgr.loadSources(files),
        Catch::Matchers::ContainsSubstring("has unknown child schema: 'requirement'"));
  }

  SECTION("Fails if reference points to unknown schema")
  {
    SchemaManager mgr;

    std::unordered_map<std::string, std::string> files = {
        {"project.yaml", R"(
name: project
kind: root
description: Root with bad reference
fields:
  ref:
    type: reference
    target: missing_target
)"}};

    REQUIRE_THROWS_WITH(
        mgr.loadSources(files),
        Catch::Matchers::ContainsSubstring("has reference field 'ref' pointing to unknown target schema: 'missing_target'"));
  }

  SECTION("Passes validation when all children and references are valid")
  {
    SchemaManager mgr;

    std::unordered_map<std::string, std::string> files = {
        {"project.yaml", R"(
name: project
kind: root
description: Valid root
children:
  requirements:
    node: requirement
)"},
        {"requirement.yaml", R"(
name: requirement
kind: node
description: Valid requirement
fields:
  title:
    type: string
  link:
    type: reference
    target: attachment
)"},
        {"attachment.yaml", R"(
name: attachment
kind: node
description: Valid attachment
fields:
  filename:
    type: string
)"}};

    REQUIRE_NOTHROW(mgr.loadSources(files));
    REQUIRE(mgr.root() != nullptr);
    REQUIRE(mgr.hasSchema("requirement"));
    REQUIRE(mgr.hasSchema("attachment"));
  }
}
