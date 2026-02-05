#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp> // for ContainsSubstring
#include "SchemaManager.h"
#include "SQLiteSchemaExporter.h"
#include <iostream> // for std::cout

using Catch::Matchers::ContainsSubstring;

//
// ✅ VALID SCHEMA TESTS
//
TEST_CASE("SQLiteSchemaExporter generates schema for Hephora example", "[SQLiteSchemaExporter][valid]")
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
  priority:   { type: enum, values: [low, medium, high], default: medium }
  rationale:  { type: string, required: false }
  active:     { type: boolean }

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
  filetype: { type: enum, values: [pdf, docx, xlsx, png, jpg, txt], required: true }
  path:     { type: string, required: true }
  description: { type: string, required: false }
  tags:
    type: array
    items: { type: string }
)"}};

  mgr.loadSources(files);
  SQLiteSchemaExporter exporter;

  std::string sql = exporter.exportSchema(mgr.registry());

  // Debug print of full SQL
  std::cout << "\n================= Generated SQL =================\n"
            << sql
            << "\n================================================\n";

  SECTION("Tables exist")
  {
    REQUIRE(sql.find("CREATE TABLE project") != std::string::npos);
    REQUIRE(sql.find("CREATE TABLE requirement") != std::string::npos);
    REQUIRE(sql.find("CREATE TABLE attachment") != std::string::npos);
  }

  SECTION("Project table has parent tracking fields")
  {
    REQUIRE(sql.find("_parent_id TEXT") != std::string::npos);

    // Non-root tables will now include both _parent_id and _parent_profile (no hard FK)
    REQUIRE(sql.find("_parent_profile TEXT") != std::string::npos);
  }

  SECTION("Requirement table fields")
  {
    REQUIRE(sql.find("title TEXT") != std::string::npos);
    REQUIRE(sql.find("priority TEXT") != std::string::npos);
    REQUIRE(sql.find("CHECK(priority IN ('low','medium','high'))") != std::string::npos);
  }

  SECTION("Requirement object specs flattened")
  {
    REQUIRE(sql.find("specs$manufacturer TEXT DEFAULT 'ToorConnect'") != std::string::npos);
    REQUIRE(sql.find("specs$warranty_years INTEGER DEFAULT 3") != std::string::npos);
  }

  SECTION("Requirement tags array table")
  {
    REQUIRE(sql.find("CREATE TABLE requirement$tags") != std::string::npos);
    REQUIRE(sql.find("value TEXT") != std::string::npos);
  }

  SECTION("Requirement references array table")
  {
    REQUIRE(sql.find("CREATE TABLE requirement$references") != std::string::npos);
    REQUIRE(sql.find("FOREIGN KEY(attachment$id) REFERENCES attachment(_id) ON DELETE SET NULL") != std::string::npos);
  }

  SECTION("Attachment table fields")
  {
    REQUIRE(sql.find("filename TEXT") != std::string::npos);
    REQUIRE(sql.find("filetype TEXT") != std::string::npos);
    REQUIRE(sql.find("CHECK(filetype IN ('pdf','docx','xlsx','png','jpg','txt'))") != std::string::npos);
    REQUIRE(sql.find("path TEXT") != std::string::npos);
  }

  SECTION("Attachment tags array table")
  {
    REQUIRE(sql.find("CREATE TABLE attachment$tags") != std::string::npos);
    REQUIRE(sql.find("value TEXT") != std::string::npos);
  }
}

//
// ❌ EXPORTER ERROR TESTS
//
TEST_CASE("SQLiteSchemaExporter rejects nested arrays", "[SQLiteSchemaExporter][exporter][errors]")
{
  SchemaManager mgr;

  std::unordered_map<std::string, std::string> files = {
      {"invalid.yaml", R"(
name: invalid
kind: root
description: Root with invalid nested arrays
fields:
  matrix:
    type: array
    items:
      type: array
      items: { type: integer }
)"}};

  mgr.loadSources(files);
  SQLiteSchemaExporter exporter;

  REQUIRE_THROWS_WITH(exporter.exportSchema(mgr.registry()), ContainsSubstring("Nested arrays"));
}

TEST_CASE("SQLiteSchemaExporter rejects nested arrays inside objects", "[SQLiteSchemaExporter][exporter][errors]")
{
  SchemaManager mgr;

  std::unordered_map<std::string, std::string> files = {
      {"invalid_obj.yaml", R"(
name: invalid_obj
kind: root
description: Root with invalid nested arrays
fields:
  container:
    type: object
    fields:
      values:
        type: array
        items:
          type: array
          items: { type: string }
)"}};

  mgr.loadSources(files);
  SQLiteSchemaExporter exporter;

  REQUIRE_THROWS_WITH(exporter.exportSchema(mgr.registry()),
                      ContainsSubstring("Nested arrays (arrays-of-arrays)"));
}

//
// ❌ DECODER ERROR TESTS
//
TEST_CASE("SchemaManager rejects unknown field type", "[SQLiteSchemaExporter][decoder][errors]")
{
  SchemaManager mgr;

  std::unordered_map<std::string, std::string> files = {
      {"badfield.yaml", R"(
name: badfield
kind: root
description: Schema with unsupported field type
fields:
  crazy: { type: float }
)"}};

  REQUIRE_THROWS_WITH(mgr.loadSources(files), ContainsSubstring("Unsupported field type"));
}

TEST_CASE("SchemaManager rejects missing profile name", "[SQLiteSchemaExporter][decoder][errors]")
{
  SchemaManager mgr;

  std::unordered_map<std::string, std::string> files = {
      {"noname.yaml", R"(
kind: root
fields:
  title: { type: string }
)"}};

  REQUIRE_THROWS_WITH(mgr.loadSources(files), ContainsSubstring("name"));
}

TEST_CASE("SchemaManager rejects invalid enum without values", "[SQLiteSchemaExporter][decoder][errors]")
{
  SchemaManager mgr;

  std::unordered_map<std::string, std::string> files = {
      {"bad_enum.yaml", R"(
name: bad_enum
kind: root
description: Enum with no values
fields:
  status: { type: enum }
)"}};

  REQUIRE_THROWS_WITH(mgr.loadSources(files), ContainsSubstring("Enum field missing values"));
}

//
// ✅ COMPLEX OBJECT / ARRAY STRUCTURE TESTS
//
TEST_CASE("SQLiteSchemaExporter supports nested object fields (one level)", "[SQLiteSchemaExporter][object][nested]")
{
  SchemaManager mgr;

  std::unordered_map<std::string, std::string> files = {
      {"nested_object.yaml", R"(
name: nested_object
kind: root
description: Valid schema with nested object (one level deep)
fields:
  meta:
    type: object
    fields:
      info:
        type: object
        fields:
          name: { type: string }
          version: { type: integer }
)"}};

  mgr.loadSources(files);
  SQLiteSchemaExporter exporter;

  std::string sql = exporter.exportSchema(mgr.registry());

  // Should flatten as meta$info$name and meta$info$version columns
  REQUIRE(sql.find("meta$info$name TEXT") != std::string::npos);
  REQUIRE(sql.find("meta$info$version INTEGER") != std::string::npos);
}

TEST_CASE("SQLiteSchemaExporter supports arrays inside objects", "[SQLiteSchemaExporter][object][array]")
{
  SchemaManager mgr;

  std::unordered_map<std::string, std::string> files = {
      {"array_in_object.yaml", R"(
name: array_in_object
kind: root
description: Object containing an array of strings
fields:
  container:
    type: object
    fields:
      tags:
        type: array
        items: { type: string }
)"}};

  mgr.loadSources(files);
  SQLiteSchemaExporter exporter;

  std::string sql = exporter.exportSchema(mgr.registry());

  // Should generate a side table for array_in_object$container$tags
  REQUIRE(sql.find("CREATE TABLE array_in_object$container$tags") != std::string::npos);
  REQUIRE(sql.find("value TEXT") != std::string::npos);
}

TEST_CASE("SQLiteSchemaExporter rejects array-within-object-within-array", "[SQLiteSchemaExporter][deep][invalid]")
{
  SchemaManager mgr;

  std::unordered_map<std::string, std::string> files = {
      {"invalid_triple.yaml", R"(
name: invalid_triple
kind: root
description: Array of objects where each object contains an array
fields:
  things:
    type: array
    items:
      type: object
      fields:
        inner_array:
          type: array
          items: { type: string }
)"}};

  REQUIRE_THROWS_WITH(mgr.loadSources(files),
                      ContainsSubstring("nested arrays inside objects"));
}

TEST_CASE("YamlSchemaDecoder rejects field names containing '$'", "[YamlSchemaDecoder][validation][invalid]")
{
  SchemaManager mgr;

  std::unordered_map<std::string, std::string> files = {
      {"bad_dollar.yaml", R"(
name: bad_dollar
kind: root
description: Field name contains '$'
fields:
  invalid$name: { type: string }
)"}};

  REQUIRE_THROWS_WITH(mgr.loadSources(files),
                      ContainsSubstring("Invalid field name: '$'"));
}

TEST_CASE("SQLiteSchemaExporter supports array of objects with scalar fields", "[SQLiteSchemaExporter][array][object][valid]")
{
  SchemaManager mgr;

  std::unordered_map<std::string, std::string> files = {
      {"array_object_valid.yaml", R"(
name: array_object_valid
kind: root
description: Array of objects with scalar fields (allowed)
fields:
  people:
    type: array
    items:
      type: object
      fields:
        name: { type: string, required: true }
        age:  { type: integer }
        active: { type: boolean}
)"}};

  mgr.loadSources(files);
  SQLiteSchemaExporter exporter;

  std::string sql = exporter.exportSchema(mgr.registry());

  // Debug print for manual review (optional)
  std::cout << "\n[DEBUG] SQL for array_object_valid:\n"
            << sql << std::endl;

  // ✅ Expected: a side table array_object_valid$people
  REQUIRE(sql.find("CREATE TABLE array_object_valid$people") != std::string::npos);

  // ✅ Expected: columns for subfields (name, age, active)
  REQUIRE(sql.find("name TEXT NOT NULL") != std::string::npos);
  REQUIRE(sql.find("age INTEGER") != std::string::npos);
  REQUIRE(sql.find("active INTEGER CHECK(active IN (0,1))") != std::string::npos);
}

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "SchemaManager.h"
#include "SQLiteSchemaExporter.h"
#include <iostream>

using Catch::Matchers::ContainsSubstring;

TEST_CASE("SQLiteSchemaExporter exports complete Hephora V-Model schemas without crashing", "[Hephora][SQLiteSchemaExporter][integration][full]")
{
  SchemaManager mgr;
  SQLiteSchemaExporter exporter;

  // All schema definitions from the log (filename + YAML)
  std::unordered_map<std::string, std::string> files = {
      {"attachment.yaml", R"(name: attachment
kind: node
description: Profile
description: Defines a file attachment
meta:
  alias: Attachment

fields:
  filetype:
    type: string
    required: false
    description: Type of the file (e.g., pdf, docx, png)
    meta:
      alias: File Type

  path:
    type: string
    required: false
    description: Path to the file, including filename
    meta:
      alias: File Path

  description:
    type: string
    required: false
    description: Description of the attachment
    meta:
      alias: Description

  date_updated:
    type: string
    required: false
    description: Date when the attachment was last updated
    meta:
      alias: Date Updated

  version:
    type: string
    required: false
    description: Version of the attachment
    meta:
      alias: Version
)"},

      {"project.yaml", R"(name: project
kind: root
description: V-Model project profile
meta:
  alias: V-Model Project

fields:
  version:
    type: string
    required: false
    meta:
      alias: Version

  description:
    type: string
    required: false
    meta:
      alias: Description
  client:
    type: object
    fields:
      name: { type: string }
      project_name: { type: string }
children:
  stakeholder_requirements_groups: { node: stakeholder_requirements_group }
  sys_requirements_groups: { node: sys_requirements_group }
  sw_requirements_groups: { node: sw_requirements_group }
  sw_architectures: { node: sw_architecture }
  sw_design: { node: sw_design }
  sw_unit_test_strategy: { node: sw_unit_test_strategy }
  sw_integration_test_strategy: { node: sw_integration_test_strategy }
  sw_qualification_test_strategy: { node: sw_qualification_test_strategy }
  attachments: { node: attachment }
)"},

      {"stakeholder_requirement.yaml", R"(name: stakeholder_requirement
kind: node
description: Profile
meta:
  alias: Stakeholder Requirement
fields:
  brief: { type: string }
  details: { type: string }
  rationale: { type: string }
  priority: { type: enum, values: [low, medium, high], default: medium }
  milestone: { type: string }
  type:
    type: array
    items: { type: enum, values: [functional, non-functional, regulatory, safety, security] }
  acceptance_criteria: { type: string }
  status: { type: enum, values: [proposed, approved, implemented, verified, rejected], default: proposed }
  date_updated: { type: string }
  dependencies:
    type: array
    items: { type: reference, target: stakeholder_requirement }
  notes:
    type: array
    items:
      type: object
      fields:
        date: { type: string }
        content: { type: string }
  reference_sources:
    type: array
    items: { type: reference, target: attachment }
)"},

      {"stakeholder_requirements_group.yaml", R"(name: stakeholder_requirements_group
kind: node
description: Profile
meta:
  alias: Stakeholder Requirements Group
fields:
  description: { type: string }
children:
  stakeholder_requirements: { node: stakeholder_requirement }
)"},

      {"sw_architecture.yaml", R"(name: sw_architecture
kind: node
description: Profile
meta:
  alias: Software Architecture
fields:
  description: { type: string }
  architectural_decisions: { type: string }
  status: { type: enum, values: [draft, proposed, approved, rejected] }
  attachments:
    type: array
    items: { type: reference, target: attachment }
children:
  components: { node: sw_component }
  interfaces: { node: sw_interface }
  data_structures: { node: sw_data_structure }
)"},

      {"sw_component.yaml", R"(name: sw_component
kind: node
description: Profile
meta:
  alias: Software Component
fields:
  description: { type: string }
  sw_requirements:
    type: array
    items: { type: reference, target: sw_requirement }
  parent_reference:
    type: reference
    target: sw_component
children:
  attachments: { node: attachment }
)"},

      {"sw_data_structure.yaml", R"(name: sw_data_structure
kind: node
description: Profile
meta:
  alias: Software Data Structure
fields:
  description: { type: string }
  fields:
    type: array
    items:
      type: object
      fields:
        name: { type: string }
        data_type: { type: reference, target: sw_data_structure }
        unit: { type: string }
        resolution: { type: string }
        range: { type: string }
        multiplicity: { type: string }
)"},

      {"sw_design.yaml", R"(name: sw_design
kind: node
description: Profile
meta:
  alias: Software Design
fields:
  description: { type: string }
children:
  sw_units: { node: sw_unit }
  sw_relationships: { node: sw_relationship }
  attachments: { node: attachment }
)"},

      {"sw_requirement.yaml", R"(name: sw_requirement
kind: node
description: Profile
meta:
  alias: Software Requirement
fields:
  brief: { type: string }
  details: { type: string }
  rationale: { type: string }
  acceptance_criteria: { type: string }
  sys_requirement_ref:
    type: object
    fields:
      references:
        type: array
        items: { type: reference, target: sys_requirement }
      identical: { type: boolean }
  attachments:
    type: array
    items: { type: reference, target: attachment }
)"},

      {"sw_requirements_group.yaml", R"(name: sw_requirements_group
kind: node
description: Profile
meta:
  alias: Software Requirements Group
fields:
  description: { type: string }
children:
  sw_requirements: { node: sw_requirement }
)"},

      {"sw_relationship.yaml", R"(name: sw_relationship
kind: node
description: Profile
meta:
  alias: Software Relationship
fields:
  type: { type: enum, values: [dependency, association, aggregation, composition, realization] }
  source:
    type: object
    fields:
      ref: { type: reference, target: sw_unit }
      multiplicity: { type: string }
  target:
    type: object
    fields:
      ref: { type: reference, target: sw_unit }
      multiplicity: { type: string }
  direction: { type: enum, values: [unidirectional, bidirectional] }
  description: { type: string }
)"},

      {"sw_interface.yaml", R"(name: sw_interface
kind: node
description: Profile
meta:
  alias: Software Interface
fields:
  sw_requirements:
    type: array
    items: { type: reference, target: sw_requirement }
  description: { type: string }
  provided_by:
    type: array
    items: { type: reference, target: sw_component }
  required_by:
    type: array
    items: { type: reference, target: sw_component }
  sw_data_structures:
    type: array
    items: { type: reference, target: sw_data_structure }
  data_direction: { type: enum, values: [to provided, to required, bidirectional] }
  communication:
    type: object
    fields:
      mode: { type: enum, values: [synchronous, asynchronous] }
      type: { type: string }
children:
  attachments: { node: attachment }
)"},

      {"sw_unit.yaml", R"(name: sw_unit
kind: node
description: Profile
meta:
  alias: Software Unit
fields:
  description: { type: string }
  sw_component_refs:
    type: array
    items: { type: reference, target: sw_component }
children:
  sw_data_structures: { node: sw_data_structure }
  attachments: { node: attachment }
  methods: { node: sw_unit_method }
  relationships: { node: sw_unit_relationship }
  attributes: { node: sw_unit_attribute }
)"},

      {"sw_unit_attribute.yaml", R"(name: sw_unit_attribute
kind: node
description: Profile
meta:
  alias: Software Unit Attribute
fields:
  name: { type: string }
  description: { type: string }
  data_type: { type: reference, target: sw_data_structure }
  multiplicity: { type: string }
  scope: { type: enum, values: [public, private, protected] }
)"},

      {"sw_unit_method.yaml", R"(name: sw_unit_method
kind: node
description: Profile
meta:
  alias: Software Unit Method
fields:
  name: { type: string }
  description: { type: string }
  scope: { type: enum, values: [public, private, protected] }
  parameters:
    type: array
    items:
      type: object
      fields:
        name: { type: string }
        description: { type: string }
        data_type: { type: reference, target: sw_data_structure }
        direction: { type: enum, values: [in, out, inout] }
        multiplicity: { type: string }
  return:
    type: object
    fields:
      data_type: { type: reference, target: sw_data_structure }
      description: { type: string }
)"},

      {"sw_unit_relationship.yaml", R"(name: sw_unit_relationship
kind: node
description: Profile
meta:
  alias: Software Unit Relationship
fields:
  type: { type: enum, values: [dependency, association, aggregation, composition, realization] }
  target: { type: reference, target: sw_unit }
  source_multiplicity: { type: string }
  target_multiplicity: { type: string }
  description: { type: string }
)"},

      {"sw_unit_test_case.yaml", R"(name: sw_unit_test_case
kind: node
description: Profile
meta:
  alias: Test Case
fields:
  description: { type: string }
  preconditions:
    type: array
    items: { type: string }
  steps:
    type: array
    items: { type: string }
  expected_result: { type: string }
  status: { type: enum, values: [Not Tested, Passed, Failed] }
)"},

      {"sw_unit_test_plan.yaml", R"(name: sw_unit_test_plan
kind: node
description: Profile
meta:
  alias: Test Plan
fields:
  description: { type: string }
  sw_unit: { type: reference, target: sw_unit }
children:
  test_cases: { node: sw_unit_test_case }
  evidences: { node: attachment }
)"},

      {"sw_unit_test_strategy.yaml", R"(name: sw_unit_test_strategy
kind: node
description: Profile
meta:
  alias: Test Strategy
fields:
  description: { type: string }
  tools:
    type: array
    items: { type: string }
  environment: { type: string }
children:
  test_plans: { node: sw_unit_test_plan }
)"},

      {"sw_integration_test_case.yaml", R"(name: sw_integration_test_case
kind: node
description: Profile
fields:
  description: { type: string }
  preconditions:
    type: array
    items: { type: string }
  steps:
    type: array
    items: { type: string }
  expected_result: { type: string }
  status: { type: enum, values: [Not Tested, Passed, Failed] }
)"},

      {"sw_integration_test_plan.yaml", R"(name: sw_integration_test_plan
kind: node
description: Profile
fields:
  description: { type: string }
  sw_components:
    type: array
    items: { type: reference, target: sw_component }
children:
  test_cases: { node: sw_integration_test_case }
  evidences: { node: attachment }
)"},

      {"sw_integration_test_strategy.yaml", R"(name: sw_integration_test_strategy
kind: node
description: Profile
fields:
  description: { type: string }
  tools:
    type: array
    items: { type: string }
  environment: { type: string }
children:
  test_plans: { node: sw_integration_test_plan }
)"},

      {"sw_qualification_test_case.yaml", R"(name: sw_qualification_test_case
kind: node
description: Profile
fields:
  description: { type: string }
  preconditions:
    type: array
    items: { type: string }
  steps:
    type: array
    items: { type: string }
  expected_result: { type: string }
  status: { type: enum, values: [Not Tested, Passed, Failed] }
)"},

      {"sw_qualification_test_plan.yaml", R"(name: sw_qualification_test_plan
kind: node
description: Profile
fields:
  description: { type: string }
children:
  test_cases: { node: sw_qualification_test_case }
  evidences: { node: attachment }
)"},

      {"sw_qualification_test_strategy.yaml", R"(name: sw_qualification_test_strategy
kind: node
description: Profile
fields:
  description: { type: string }
  tools:
    type: array
    items: { type: string }
  environment: { type: string }
children:
  test_plans: { node: sw_qualification_test_plan }
)"},

      {"sys_requirement.yaml", R"(name: sys_requirement
kind: node
description: Profile
fields:
  brief: { type: string }
  details: { type: string }
  rationale: { type: string }
  acceptance_criteria: { type: string }
  stakeholder_ref:
    type: array
    items: { type: reference, target: stakeholder }
  attachments:
    type: array
    items: { type: reference, target: attachment }
)"},

      {"sys_requirements_group.yaml", R"(name: sys_requirements_group
kind: node
description: Profile
fields:
  description: { type: string }
children:
  sys_requirements: { node: sys_requirement }
)"}};

  REQUIRE_NOTHROW(mgr.loadSources(files));

  std::string sql;
  REQUIRE_NOTHROW(sql = exporter.exportSchema(mgr.registry()));

  // Debug output
  std::cout << "\n==================== HEPHORA FULL SCHEMA EXPORT ====================\n";
  std::cout << sql.substr(0, 800) << "\n... (truncated, total bytes = " << sql.size() << ")\n";
  std::cout << "===================================================================\n";

  // ---- Collect all table names expected ----
  std::vector<std::string> expectedTables;
  for (const auto &[name, schemaPtr] : mgr.registry().schemas())
  {
    expectedTables.push_back(name); // main table
    for (const auto &[fieldName, fieldPtr] : schemaPtr->fields())
    {
      // Add array side tables: profile$fieldName
      if (fieldPtr->type() == FieldType::Array)
        expectedTables.push_back(name + "$" + fieldName);

      // Check for arrays nested inside objects (object-of-arrays)
      if (fieldPtr->type() == FieldType::Object)
      {
        const auto *obj = dynamic_cast<const ObjectFieldSchema *>(fieldPtr.get());
        for (const auto &sub : obj->fields())
        {
          if (sub->type() == FieldType::Array)
            expectedTables.push_back(name + "$" + fieldName + "$" + sub->name());
        }
      }
    }
  }

  // ---- Assert each CREATE TABLE exists ----
  for (const auto &tbl : expectedTables)
  {
    INFO("Checking table: " << tbl);
    REQUIRE(sql.find("CREATE TABLE " + tbl) != std::string::npos);
  }

  std::cout << "\n[Hephora Full Integration Test] ✅ Verified "
            << expectedTables.size()
            << " tables (main + side) successfully generated.\n";
}
