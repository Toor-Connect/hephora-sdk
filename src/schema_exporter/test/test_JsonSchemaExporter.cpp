// test_json_schema_exporter.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp> // ContainsSubstring
#include "SchemaManager.h"
#include "JSONSchemaExporter.h"
#include <iostream>

using Catch::Matchers::ContainsSubstring;

//
// ✅ VALID SCHEMA TESTS (JSON Schema)
//
TEST_CASE("JsonSchemaExporter generates JSON Schema for Hephora example", "[JsonSchemaExporter][valid]")
{
    SchemaManager mgr;

    std::unordered_map<std::string, std::string> files = {
        // --- project.yaml ---
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
  requirements: { node: requirement, description: List of requirements belonging to this project }
  attachments:  { node: attachment, description: List of attachments directly associated with this project }
)"},
        // --- requirement.yaml ---
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

  rationale:
    type: string
    required: false

  active:
    type: boolean

  specs:
    type: object
    fields:
      manufacturer: { type: string, default: ToorConnect }
      warranty_years: { type: integer, default: 3 }

  tags:
    type: array
    items: { type: string }

  references:
    type: array
    items: { type: reference, target: attachment }
)"},
        // --- attachment.yaml ---
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

  path:
    type: string
    required: true

  description:
    type: string
    required: false

  tags:
    type: array
    items:
      type: string

)"}};

    mgr.loadSources(files);
    JSONSchemaExporter exporter;

    std::string json = exporter.exportSchema(mgr.registry());

    // Debug print of full JSON
    std::cout << "\n================= Generated JSON Schema =================\n"
              << json
              << "\n=========================================================\n";

    SECTION("Bundle structure exists")
    {
        REQUIRE_THAT(json, ContainsSubstring("\"$schema\""));
        REQUIRE_THAT(json, ContainsSubstring("\"$defs\""));
        REQUIRE_THAT(json, ContainsSubstring("\"$ref\":\"#/$defs/project\""));
    }

    SECTION("All profiles are defined under $defs")
    {
        REQUIRE_THAT(json, ContainsSubstring("\"project\""));
        REQUIRE_THAT(json, ContainsSubstring("\"requirement\""));
        REQUIRE_THAT(json, ContainsSubstring("\"attachment\""));
    }

    SECTION("Reserved properties exist on each profile")
    {
        REQUIRE_THAT(json, ContainsSubstring("\"_id\""));
        REQUIRE_THAT(json, ContainsSubstring("\"_label\""));
        REQUIRE_THAT(json, ContainsSubstring("\"_parent_id\""));
    }

    SECTION("Project: fields and required")
    {
        // Types
        REQUIRE_THAT(json, ContainsSubstring("\"project_name\""));
        REQUIRE_THAT(json, ContainsSubstring("\"version\""));
        REQUIRE_THAT(json, ContainsSubstring("\"description\""));
        REQUIRE_THAT(json, ContainsSubstring("\"owner\""));
        REQUIRE_THAT(json, ContainsSubstring("\"type\":\"string\""));

        // Required array includes _id, project_name, version
        REQUIRE_THAT(json, ContainsSubstring("\"required\""));
        REQUIRE_THAT(json, ContainsSubstring("\"_id\""));
        REQUIRE_THAT(json, ContainsSubstring("\"project_name\""));
        REQUIRE_THAT(json, ContainsSubstring("\"version\""));
    }

    SECTION("Project: children collections reference child profiles")
    {
        // requirements -> $ref requirement
        REQUIRE_THAT(json, ContainsSubstring("\"requirements\""));
        REQUIRE_THAT(json, ContainsSubstring("\"$ref\":\"#/$defs/requirement\""));

        // attachments -> $ref attachment
        REQUIRE_THAT(json, ContainsSubstring("\"attachments\""));
        REQUIRE_THAT(json, ContainsSubstring("\"$ref\":\"#/$defs/attachment\""));
    }

    SECTION("Requirement: enum, boolean, object, arrays, references")
    {
        // Enum mapping
        REQUIRE_THAT(json, ContainsSubstring("\"priority\""));
        REQUIRE_THAT(json, ContainsSubstring("\"enum\":[\"low\",\"medium\",\"high\"]"));

        // Boolean
        REQUIRE_THAT(json, ContainsSubstring("\"active\""));
        REQUIRE_THAT(json, ContainsSubstring("\"type\":\"boolean\""));

        // Object 'specs' with nested properties
        REQUIRE_THAT(json, ContainsSubstring("\"specs\""));
        REQUIRE_THAT(json, ContainsSubstring("\"manufacturer\""));
        REQUIRE_THAT(json, ContainsSubstring("\"warranty_years\""));
        REQUIRE_THAT(json, ContainsSubstring("\"type\":\"object\""));

        // Array 'tags' of strings
        REQUIRE_THAT(json, ContainsSubstring("\"tags\""));
        REQUIRE_THAT(json, ContainsSubstring("\"items\":{\"type\":\"string\""));

        // Array 'references' of string IDs with a description mentioning the target
        REQUIRE_THAT(json, ContainsSubstring("\"references\""));
        REQUIRE_THAT(json, ContainsSubstring("\"items\""));
        REQUIRE_THAT(json, ContainsSubstring("\"type\":\"string\""));
        REQUIRE_THAT(json, ContainsSubstring("ref to attachment._id"));
    }

    SECTION("Attachment: fields and enum")
    {
        REQUIRE_THAT(json, ContainsSubstring("\"filename\""));
        REQUIRE_THAT(json, ContainsSubstring("\"path\""));
        REQUIRE_THAT(json, ContainsSubstring("\"filetype\""));
        REQUIRE_THAT(json, ContainsSubstring("\"enum\":[\"pdf\",\"docx\",\"xlsx\",\"png\",\"jpg\",\"txt\"]"));

        // tags array of strings
        REQUIRE_THAT(json, ContainsSubstring("\"attachment\""));
        REQUIRE_THAT(json, ContainsSubstring("\"tags\""));
        REQUIRE_THAT(json, ContainsSubstring("\"items\":{\"type\":\"string\""));
    }
}
