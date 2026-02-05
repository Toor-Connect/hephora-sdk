// src/data/test/test_YamlDataDecoder.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <unordered_map>

#include "YamlSchemaLoader.h"
#include "YamlSchemaDecoder.h"
#include "SchemaRegistry.h"
#include "YamlDataLoader.h"
#include "YamlDataDecoder.h"
#include "FieldValue.h"

using Catch::Matchers::ContainsSubstring;

TEST_CASE("YamlDataDecoder decodes data to typed NodeInstance with metadata", "[YamlDataDecoder][valid]")
{
  // --- Load schema (3 profiles) ---
  SchemaRegistry reg;
  std::unordered_map<std::string, std::string> profileSources = {
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
  auto schemaNodes = YamlSchemaLoader::loadFromSources(profileSources);
  YamlSchemaDecoder::decodeProfiles(schemaNodes, reg);

  // --- Load data docs ---
  std::unordered_map<std::string, std::string> dataSources = {
      {"project/P-0001.yaml", R"(
_profile: project
_id: P-0001
_label: "Project Phoenix"
project_name: "Phoenix"
version: "1.0"
description: "Main project"
owner: "Alex"
)"},
      {"requirement/R-0001.yaml", R"(
_profile: requirement
_id: R-0001
_parent_id: P-0001
title: "Brake latency under 100 ms"
priority: high
active: true
specs:
  manufacturer: "ToorConnect"
  warranty_years: 3
tags: [safety, timing]
references: []
)"},
      {"attachment/A-0001.yaml", R"(
_profile: attachment
_id: A-0001
_parent_id: P-0001
filename: "spec.pdf"
filetype: pdf
path: "/docs/spec.pdf"
)"},
  };
  auto yamlDict = YamlDataLoader::loadFromSources(dataSources);

  // --- Decode to typed nodes ---
  auto decoded = YamlDataDecoder::decode(yamlDict, reg);
  REQUIRE(decoded.size() == 3);

  // Find requirement
  auto it = std::find_if(decoded.begin(), decoded.end(),
                         [](const auto &d)
                         { return d.profile == "requirement" && d.id == "R-0001"; });
  REQUIRE(it != decoded.end());
  const auto &req = *it;

  CHECK(req.parent_id == "P-0001");
  CHECK(req.instance.fields.count("title") == 1);
  CHECK(req.instance.fields.count("priority") == 1);
  CHECK(req.instance.fields.count("active") == 1);
  CHECK(req.instance.fields.count("specs") == 1);
  CHECK(req.instance.fields.count("tags") == 1);

  // Type spot checks via FieldValue helpers (assuming typical API)
  {
    const auto &title = req.instance.fields.at("title");
    REQUIRE(title.isString());
    CHECK(title.asString() == "Brake latency under 100 ms");
  }
  {
    const auto &pr = req.instance.fields.at("priority");
    REQUIRE(pr.isString());
    CHECK(pr.asString() == "high");
  }
  {
    const auto &act = req.instance.fields.at("active");
    REQUIRE(act.isBoolean());
    CHECK(act.asBoolean() == true);
  }
  {
    const auto &specs = req.instance.fields.at("specs");
    REQUIRE(specs.isObject());
    const auto &obj = specs.asObject();
    REQUIRE(obj.at("manufacturer").isString());
    CHECK(obj.at("manufacturer").asString() == "ToorConnect");
    REQUIRE(obj.at("warranty_years").isInteger());
    CHECK(obj.at("warranty_years").asInteger() == 3);
  }
  {
    const auto &tags = req.instance.fields.at("tags");
    REQUIRE(tags.isArray());
    const auto &arr = tags.asArray();
    REQUIRE(arr.size() == 2);
    CHECK(arr[0].isString());
    CHECK(arr[0].asString() == "safety");
  }
}

// src/yaml_data_parser/test/test_YamlDataDecoder.cpp
// ...same includes...

TEST_CASE("YamlDataDecoder errors: missing reserved fields / unknown profile / bad types", "[YamlDataDecoder][errors]")
{
  SchemaRegistry reg;
  // minimal schema with one profile for error tests (now includes description)
  std::unordered_map<std::string, std::string> profileSources = {
      {"requirement.yaml", R"(
name: requirement
kind: root
description: Minimal profile for error-path tests
fields:
  title: { type: string, required: true }
)"}};
  auto schemaNodes = YamlSchemaLoader::loadFromSources(profileSources);
  YamlSchemaDecoder::decodeProfiles(schemaNodes, reg);

  SECTION("Missing _profile")
  {
    std::unordered_map<std::string, std::string> badData = {
        {"x.yaml", R"(
_id: X-1
title: Hello
)"}};
    auto dict = YamlDataLoader::loadFromSources(badData);
    REQUIRE_THROWS_WITH(YamlDataDecoder::decode(dict, reg),
                        ContainsSubstring("Missing required '_profile'"));
  }

  SECTION("Unknown profile")
  {
    std::unordered_map<std::string, std::string> badData = {
        {"x.yaml", R"(
_profile: nope
_id: X-1
title: Hello
)"}};
    auto dict = YamlDataLoader::loadFromSources(badData);
    REQUIRE_THROWS_WITH(YamlDataDecoder::decode(dict, reg),
                        ContainsSubstring("Unknown profile 'nope'"));
  }

  SECTION("Unknown field")
  {
    std::unordered_map<std::string, std::string> badData = {
        {"x.yaml", R"(
_profile: requirement
_id: R-1
bogus: 123
)"}};
    auto dict = YamlDataLoader::loadFromSources(badData);
    REQUIRE_THROWS_WITH(YamlDataDecoder::decode(dict, reg),
                        ContainsSubstring("Unknown field 'bogus'"));
  }

  SECTION("Type error")
  {
    std::unordered_map<std::string, std::string> badData = {
        {"x.yaml", R"(
_profile: requirement
_id: R-1
title: [1,2,3]   # should be string
)"}};
    auto dict = YamlDataLoader::loadFromSources(badData);
    REQUIRE_THROWS_WITH(YamlDataDecoder::decode(dict, reg),
                        ContainsSubstring("Type error at requirement.title"));
  }
}

TEST_CASE("YamlDataDecoder preserves filename→node pairing", "[YamlDataDecoder][ordering]")
{
  // --- Build a minimal 3-profile registry: project, requirement, attachment ---
  SchemaRegistry reg;
  std::unordered_map<std::string, std::string> profileSources = {
      {"project.yaml", R"(
name: project
kind: root
meta:
  alias: Project
description: Top-level project
fields:
  project_name: { type: string, required: true }
  version:      { type: string, required: true }
children:
  requirements: { node: requirement, description: List of requirements }
  attachments:  { node: attachment, description: List of attachments }
)"},
      {"requirement.yaml", R"(
name: requirement
kind: node
meta:
  alias: Requirement
description: A requirement
fields:
  title:     { type: string, required: true }
  priority:  { type: enum, values: [low, medium, high], default: medium }
  active:    { type: boolean }
  specs:
    type: object
    fields:
      manufacturer:    { type: string, default: ToorConnect }
      warranty_years:  { type: integer, default: 3 }
  tags:
    type: array
    items: { type: string }
  references:
    type: array
    items: { type: reference, target: attachment }
)"},
      {"attachment.yaml", R"(
name: attachment
kind: node
meta:
  alias: Attachment
description: An attachment
fields:
  filename:    { type: string, required: true }
  filetype:    { type: enum, values: [pdf, docx, xlsx, png, jpg, txt], required: true }
  path:        { type: string, required: true }
  description: { type: string, required: false }
  tags:
    type: array
    items: { type: string }
)"},
  };
  auto schemaNodes = YamlSchemaLoader::loadFromSources(profileSources);
  YamlSchemaDecoder::decodeProfiles(schemaNodes, reg);

  // --- Three data docs, one for each profile ---
  std::unordered_map<std::string, std::string> src = {
      {"project/P-1.yaml",
       R"(_profile: project
_id: P-1
_label: "Phoenix"
project_name: "X"
version: "1"
)"},
      {"requirement/R-1.yaml",
       R"(_profile: requirement
_id: R-1
title: "T"
)"},
      {"attachment/A-1.yaml",
       R"(_profile: attachment
_id: A-1
filename: "f"
filetype: pdf
path: "/p"
)"},
  };

  auto docs = YamlDataLoader::loadFromSources(src);
  auto dec = YamlDataDecoder::decode(docs, reg);

  auto has = [&](const std::string &fn, const std::string &prof, const std::string &id)
  {
    return std::any_of(dec.begin(), dec.end(), [&](const auto &d)
                       { return d.source == fn && d.profile == prof && d.id == id; });
  };

  REQUIRE(has("project/P-1.yaml", "project", "P-1"));
  REQUIRE(has("requirement/R-1.yaml", "requirement", "R-1"));
  REQUIRE(has("attachment/A-1.yaml", "attachment", "A-1"));
}
