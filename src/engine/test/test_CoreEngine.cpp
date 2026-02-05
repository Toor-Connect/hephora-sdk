#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

#include "CoreEngine.h"
#include "SchemaManager.h"
#include "SQLiteDataBackend.h"
#include "InMemoryDataCommit.h"
#include "YamlSchemaDecoder.h"

// ---------- schema ----------
static std::unordered_map<std::string, std::string> build_registry()
{
  std::unordered_map<std::string, std::string> files = {
      {"project.yaml", R"(
name: project
kind: root
alias: Project
description: Top-level project
fields:
  version:      { type: string, required: true }
children:
  requirements: { node: requirement, description: List of requirements }
  attachments:  { node: attachment, description: List of attachments }
)"},
      {"requirement.yaml", R"(
name: requirement
kind: node
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
)"}};
  return files;
}

static std::unordered_map<std::string, YAML::Node> data_docs()
{
  std::unordered_map<std::string, YAML::Node> docs = {
      {"project/Phoenix_543e8f4e.yaml", YAML::Load(R"(
_profile: project
_id: 543e8f4e-8f4e-4e8f-943e-8f4e8f4e8f4e
_label: Phoenix
version: 1.0
)")},
      {"attachment/A-1_987e6f5d.yaml", YAML::Load(R"(
_profile: attachment
_id: 987e6f5d-4c3b-2a1f-0e9d-8c7b6a5f4e3d
_label: A-1
_parent_id: 543e8f4e-8f4e-4e8f-943e-8f4e8f4e8f4e
filename: spec.pdf
filetype: pdf
path: /docs/spec.pdf
tags:
  []
)")},
      {"requirement/R-1_123e4567.yaml", YAML::Load(R"(
_profile: requirement
_id: 123e4567-e89b-12d3-a456-426614174000
_label: R-1
_parent_id: 543e8f4e-8f4e-4e8f-943e-8f4e8f4e8f4e
active: true
priority: high
references:
  - 987e6f5d-4c3b-2a1f-0e9d-8c7b6a5f4e3d
specs:
  manufacturer: ToorConnect
  warranty_years: 4
tags:
  []
title: Brake latency under 100 ms
)")}};
  return docs;
}

TEST_CASE("CoreEngine: basic instantiation")
{
  auto reg = build_registry();
  SchemaManager sm;
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);

  EngineConfig cfg;

  InMemoryDataCommit committer;

  CoreEngine engine(backend, &committer, cfg);

  REQUIRE_NOTHROW(sm.loadSources(reg));
  REQUIRE_NOTHROW(engine.init(sm.registry()));

  auto docs = data_docs();
  REQUIRE_NOTHROW(engine.loadData(docs));
}

TEST_CASE("CoreEngine: update label and flush")
{
  auto reg = build_registry();
  SchemaManager sm;
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);

  EngineConfig cfg;

  InMemoryDataCommit committer;

  CoreEngine engine(backend, &committer, cfg);

  REQUIRE_NOTHROW(sm.loadSources(reg));
  REQUIRE_NOTHROW(engine.init(sm.registry()));

  auto docs = data_docs();
  REQUIRE_NOTHROW(engine.loadData(docs));
  REQUIRE_NOTHROW(engine.flushAll());

  NodeSnapshot snap = {
      .key = {"requirement", "123e4567-e89b-12d3-a456-426614174000"},
      .label = "R-1-updated"};
  REQUIRE_NOTHROW(engine.upsert(snap));
  auto fetched = engine.fetch(snap.key);
  REQUIRE(fetched.has_value());
  REQUIRE(fetched->label.has_value());
  REQUIRE(fetched->label.value() == "R-1-updated");
  REQUIRE(committer.has("data/requirement/R-1_123e4567.yaml"));
  REQUIRE(!committer.has("data/requirement/R-1-updated_123e4567.yaml"));
  REQUIRE_NOTHROW(engine.flushPending());
  REQUIRE(committer.has("data/requirement/R-1-updated_123e4567.yaml"));
  REQUIRE(!committer.has("data/requirement/R-1_123e4567.yaml"));
}

TEST_CASE("CoreEngine: create, flush, delete and flush")
{
  auto reg = build_registry();
  SchemaManager sm;
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);

  EngineConfig cfg;

  InMemoryDataCommit committer;

  CoreEngine engine(backend, &committer, cfg);

  REQUIRE_NOTHROW(sm.loadSources(reg));
  REQUIRE_NOTHROW(engine.init(sm.registry()));

  auto docs = data_docs();
  REQUIRE_NOTHROW(engine.loadData(docs));
  REQUIRE_NOTHROW(engine.flushAll());

  NodeSnapshot snap = {
      .key = {"requirement", std::nullopt},
      .label = "R-2",
      .parent_id = "543e8f4e-8f4e-4e8f-943e-8f4e8f4e8f4e",
      .fields = {{"title", std::string("New requirement")}}};
  REQUIRE_NOTHROW(engine.upsert(snap));
  auto fetched = engine.fetch(snap.key);
  REQUIRE(fetched.has_value());
  REQUIRE(fetched->label.has_value());
  REQUIRE(fetched->label.value() == "R-2");
  auto id_string_file = fetched->key.id.value().substr(0, 8);

  REQUIRE(!committer.has("data/requirement/R-2_" + id_string_file + ".yaml"));
  REQUIRE_NOTHROW(engine.flushPending());
  REQUIRE(committer.has("data/requirement/R-2_" + id_string_file + ".yaml"));

  REQUIRE_NOTHROW(engine.remove(fetched->key));
  auto rem_fetched = engine.fetch(fetched->key);
  REQUIRE(!rem_fetched.has_value());
  REQUIRE(committer.has("data/requirement/R-2_" + id_string_file + ".yaml"));
  REQUIRE_NOTHROW(engine.flushPending());
  REQUIRE(!committer.has("data/requirement/R-2_" + id_string_file + ".yaml"));
}

TEST_CASE("CoreEngine: recursive deletion removes all children and their YAMLs")
{
  // --- Extend schema with a deeper child level (requirement → testcase)
  std::unordered_map<std::string, std::string> reg = {
      {"project.yaml", R"(
name: project
description: Node
kind: root
alias: Project
fields:
  version: { type: string, required: true }
children:
  requirements: { node: requirement }
  attachments:  { node: attachment }
)"},
      {"requirement.yaml", R"(
name: requirement
kind: node
description: Node
alias: Requirement
fields:
  title: { type: string, required: true }
children:
  testcases: { node: testcase }
)"},
      {"testcase.yaml", R"(
name: testcase
kind: node
description: Node
alias: TestCase
fields:
  title: { type: string, required: true }
)"},
      {"attachment.yaml", R"(
name: attachment
kind: node
description: Node
alias: Attachment
fields:
  filename: { type: string, required: true }
  filetype: { type: enum, values: [pdf, docx], required: true }
  path: { type: string, required: true }
)"}};

  SchemaManager sm;
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  EngineConfig cfg;
  InMemoryDataCommit committer;
  CoreEngine engine(backend, &committer, cfg);

  REQUIRE_NOTHROW(sm.loadSources(reg));
  REQUIRE_NOTHROW(engine.init(sm.registry()));

  // --- Seed some data manually ---
  NodeSnapshot project{
      .key = {"project", "643e8f4e-8f4e-4e8f-943e-8f4e8f4e8f4e"},
      .label = "Phoenix",
      .fields = {{"version", std::string("1.0")}}};

  NodeSnapshot req{
      .key = {"requirement", "ab3e8f4e-8f4e-4e8f-943e-8f4e8f4e8f4e"},
      .label = "R-1",
      .parent_id = "643e8f4e-8f4e-4e8f-943e-8f4e8f4e8f4e",
      .fields = {{"title", std::string("Brake latency under 100ms")}}};

  NodeSnapshot test{
      .key = {"testcase", "123e4567-e89b-12d3-a456-426614174000"},
      .label = "T-1",
      .parent_id = "ab3e8f4e-8f4e-4e8f-943e-8f4e8f4e8f4e",
      .fields = {{"title", std::string("Check latency below 100ms")}}};

  NodeSnapshot att{
      .key = {"attachment", "987e6f5d-4c3b-2a1f-0e9d-8c7b6a5f4e3d"},
      .label = "A-1",
      .parent_id = "643e8f4e-8f4e-4e8f-943e-8f4e8f4e8f4e",
      .fields = {{"filename", std::string("spec.pdf")},
                 {"filetype", std::string("pdf")},
                 {"path", std::string("/docs/spec.pdf")}}};

  REQUIRE_NOTHROW(engine.upsert(project));
  REQUIRE_NOTHROW(engine.upsert(req));
  REQUIRE_NOTHROW(engine.upsert(test));
  REQUIRE_NOTHROW(engine.upsert(att));

  // --- First flush to simulate files existing on disk ---
  REQUIRE_NOTHROW(engine.flushAll());
  REQUIRE(committer.has("data/project/Phoenix_643e8f4e.yaml"));
  REQUIRE(committer.has("data/requirement/R-1_ab3e8f4e.yaml"));
  REQUIRE(committer.has("data/testcase/T-1_123e4567.yaml"));
  REQUIRE(committer.has("data/attachment/A-1_987e6f5d.yaml"));

  // --- Now remove only the parent project ---
  REQUIRE_NOTHROW(engine.remove({"project", "643e8f4e-8f4e-4e8f-943e-8f4e8f4e8f4e"}));

  // After DB-side removal, none of these should exist in backend
  REQUIRE_FALSE(engine.exists({"requirement", "ab3e8f4e-8f4e-4e8f-943e-8f4e8f4e8f4e"}));
  REQUIRE_FALSE(engine.exists({"testcase", "123e4567-e89b-12d3-a456-426614174000"}));
  REQUIRE_FALSE(engine.exists({"attachment", "987e6f5d-4c3b-2a1f-0e9d-8c7b6a5f4e3d"}));
  REQUIRE_FALSE(engine.exists({"project", "643e8f4e-8f4e-4e8f-943e-8f4e8f4e8f4e"}));

  // Purge not yet flushed → files still exist
  REQUIRE(committer.has("data/project/Phoenix_643e8f4e.yaml"));
  REQUIRE(committer.has("data/requirement/R-1_ab3e8f4e.yaml"));
  REQUIRE(committer.has("data/testcase/T-1_123e4567.yaml"));
  REQUIRE(committer.has("data/attachment/A-1_987e6f5d.yaml"));

  // --- Flush pending deletions ---
  REQUIRE_NOTHROW(engine.flushPending());

  // All subtree YAMLs should now be gone
  REQUIRE_FALSE(committer.has("data/project/Phoenix_643e8f4e.yaml"));
  REQUIRE_FALSE(committer.has("data/requirement/R-1_ab3e8f4e.yaml"));
  REQUIRE_FALSE(committer.has("data/testcase/T-1_123e4567.yaml"));
  REQUIRE_FALSE(committer.has("data/attachment/A-1_987e6f5d.yaml"));
}

TEST_CASE("CoreEngine: removing a node updates referrers", "[CoreEngine][remove][refs]")
{
  // --- 1. Build schema with references (requirement → attachment)
  auto reg = build_registry();
  SchemaManager sm;
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  EngineConfig cfg;
  InMemoryDataCommit committer;
  CoreEngine engine(backend, &committer, cfg);

  REQUIRE_NOTHROW(sm.loadSources(reg));
  REQUIRE_NOTHROW(engine.init(sm.registry()));

  // --- 2. Seed data (project + attachment + requirement referencing attachment)
  auto docs = data_docs(); // includes project/P-1, attachment/A-1, requirement/R-1 referencing A-1
  REQUIRE_NOTHROW(engine.loadData(docs));
  REQUIRE_NOTHROW(engine.flushAll());

  // Ensure all YAMLs exist before deletion
  REQUIRE(committer.has("data/project/Phoenix_543e8f4e.yaml"));
  REQUIRE(committer.has("data/attachment/A-1_987e6f5d.yaml"));
  REQUIRE(committer.has("data/requirement/R-1_123e4567.yaml"));

  // --- 3. Remove attachment A-1 (this should nullify references in requirement R-1)
  REQUIRE_NOTHROW(engine.remove({"attachment", "987e6f5d-4c3b-2a1f-0e9d-8c7b6a5f4e3d"}));

  // Attachment is gone from DB
  REQUIRE_FALSE(engine.exists({"attachment", "987e6f5d-4c3b-2a1f-0e9d-8c7b6a5f4e3d"}));

  // --- 4. Flush pending changes
  REQUIRE_NOTHROW(engine.flushPending());

  // Deleted file must be purged
  REQUIRE_FALSE(committer.has("data/attachment/A-1_987e6f5d.yaml"));

  // Requirement should have been rewritten because its reference array changed
  REQUIRE(committer.has("data/requirement/R-1_123e4567.yaml"));

  // --- 5. Validate that in backend the requirement no longer references A-1
  auto reqSnap = engine.fetch({"requirement", "123e4567-e89b-12d3-a456-426614174000"});
  REQUIRE(reqSnap.has_value());
  auto it = reqSnap->fields.find("references");
  REQUIRE(it != reqSnap->fields.end());
  REQUIRE(it->second.isArray());
  REQUIRE(it->second.asArray().empty()); // reference array cleared by ON DELETE SET NULL
}

TEST_CASE("CoreEngine: removing a node updates nested references", "[CoreEngine][remove][refs][nested]")
{
  // --- 1. Extend schema with nested reference cases ---
  std::unordered_map<std::string, std::string> reg = {
      {"project.yaml", R"(
name: project
kind: root
description: Node
alias: Project
fields:
  version: { type: string, required: true }
children:
  requirements: { node: requirement }
  attachments:  { node: attachment }
)"},
      {"attachment.yaml", R"(
name: attachment
kind: node
description: Node
alias: Attachment
fields:
  filename: { type: string, required: true }
  filetype: { type: enum, values: [pdf, docx, png], required: true }
  path: { type: string, required: true }
)"},
      {"requirement.yaml", R"(
name: requirement
kind: node
description: Node
alias: Requirement
fields:
  title: { type: string, required: true }

  # Simple reference
  ref_simple:
    type: reference
    target: attachment

  # Object with a reference inside
  ref_object:
    type: object
    fields:
      note: { type: string }
      attachment_ref:
        type: reference
        target: attachment

  # Object with array<reference> inside
  ref_object_array:
    type: object
    fields:
      attachments:
        type: array
        items:
          type: reference
          target: attachment
)"}};

  SchemaManager sm;
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  EngineConfig cfg;
  InMemoryDataCommit committer;
  CoreEngine engine(backend, &committer, cfg);

  REQUIRE_NOTHROW(sm.loadSources(reg));
  REQUIRE_NOTHROW(engine.init(sm.registry()));

  // --- 2. Seed data ---
  NodeSnapshot project{
      .key = {"project", "483e8f4e-8f4e-4e8f-943e-8f4e8f4e8f4e"},
      .label = "Phoenix",
      .fields = {{"version", std::string("1.0")}}};

  NodeSnapshot att{
      .key = {"attachment", "987e6f5d-4c3b-2a1f-0e9d-8c7b6a5f4e3d"},
      .label = "A-1",
      .parent_id = "483e8f4e-8f4e-4e8f-943e-8f4e8f4e8f4e",
      .fields = {{"filename", std::string("spec.pdf")},
                 {"filetype", std::string("pdf")},
                 {"path", std::string("/docs/spec.pdf")}}};

  NodeSnapshot req{
      .key = {"requirement", "123e4567-e89b-12d3-a456-426614174000"},
      .label = "R-1",
      .parent_id = "483e8f4e-8f4e-4e8f-943e-8f4e8f4e8f4e",
      .fields = {
          {"title", std::string("Brake latency under 100 ms")},
          {"ref_simple", FieldValue(std::string("987e6f5d-4c3b-2a1f-0e9d-8c7b6a5f4e3d"))},
          {"ref_object",
           FieldValue(ObjectData{
               {"note", FieldValue(std::string("internal"))},
               {"attachment_ref", FieldValue(std::string("987e6f5d-4c3b-2a1f-0e9d-8c7b6a5f4e3d"))}})},
          {"ref_object_array",
           FieldValue(ObjectData{
               {"attachments",
                FieldValue(ArrayData{FieldValue(std::string("987e6f5d-4c3b-2a1f-0e9d-8c7b6a5f4e3d")), FieldValue(std::string("987e6f5d-4c3b-2a1f-0e9d-8c7b6a5f4e3d"))})}})}}};

  REQUIRE_NOTHROW(engine.upsert(project));
  REQUIRE_NOTHROW(engine.upsert(att));
  REQUIRE_NOTHROW(engine.upsert(req));
  REQUIRE_NOTHROW(engine.flushAll());

  REQUIRE(committer.has("data/attachment/A-1_987e6f5d.yaml"));
  REQUIRE(committer.has("data/requirement/R-1_123e4567.yaml"));

  // --- 3. Remove attachment A-1 ---
  REQUIRE_NOTHROW(engine.remove({"attachment", "987e6f5d-4c3b-2a1f-0e9d-8c7b6a5f4e3d"}));
  REQUIRE_FALSE(engine.exists({"attachment", "987e6f5d-4c3b-2a1f-0e9d-8c7b6a5f4e3d"}));

  // --- 4. Flush pending ---
  REQUIRE_NOTHROW(engine.flushPending());
  REQUIRE_FALSE(committer.has("data/attachment/A-1_987e6f5d.yaml"));
  REQUIRE(committer.has("data/requirement/R-1_123e4567.yaml"));

  // --- 5. Validate reference cleanup in requirement ---
  auto reqSnap = engine.fetch({"requirement", "123e4567-e89b-12d3-a456-426614174000"});
  REQUIRE(reqSnap.has_value());

  const auto &fields = reqSnap->fields;
  SECTION("simple reference cleared")
  {
    auto it = fields.find("ref_simple");
    if (it == fields.end())
    {
      SUCCEED("Field 'ref_simple' removed entirely (treated as null)");
    }
    else
    {
      REQUIRE(it->second.isNull());
    }
  }

  SECTION("object.reference cleared")
  {
    auto it = fields.find("ref_object");
    REQUIRE(it != fields.end());
    REQUIRE(it->second.isObject());
    const auto &obj = it->second.asObject();

    auto jt = obj.find("attachment_ref");
    if (jt == obj.end())
    {
      SUCCEED("Nested field 'attachment_ref' removed entirely (treated as null)");
    }
    else
    {
      REQUIRE(jt->second.isNull());
    }
  }

  SECTION("object.array<reference> cleared")
  {
    auto it = fields.find("ref_object_array");
    REQUIRE(it != fields.end());
    REQUIRE(it->second.isObject());
    const auto &obj = it->second.asObject();

    auto jt = obj.find("attachments");
    if (jt == obj.end())
    {
      SUCCEED("Nested array 'attachments' removed entirely (treated as empty)");
    }
    else
    {
      REQUIRE(jt->second.isArray());
      REQUIRE(jt->second.asArray().empty());
    }
  }
}

TEST_CASE("CoreEngine: Check loading data of reference field which are within children", "[CoreEngine][loadData][refs]")
{
  // --- Extend schema with a deeper child level (requirement → testcase)
  std::unordered_map<std::string, std::string> reg = {
      {"project.yaml", R"(
name: project
description: Node
kind: root
alias: Project
fields:
  main_requirement: { type: reference, target: requirement }
  version: { type: string, required: true }
children:
  requirements: { node: requirement }
  attachments:  { node: attachment }
)"},
      {"requirement.yaml", R"(
name: requirement
kind: node
description: Node
alias: Requirement
fields:
  title: { type: string, required: true }
children:
  testcases: { node: testcase }
)"},
      {"testcase.yaml", R"(
name: testcase
kind: node
description: Node
alias: TestCase
fields:
  title: { type: string, required: true }
)"},
      {"attachment.yaml", R"(
name: attachment
kind: node
description: Node
alias: Attachment
fields:
  filename: { type: string, required: true }
  filetype: { type: enum, values: [pdf, docx], required: true }
  path: { type: string, required: true }
)"}};

  std::unordered_map<std::string, std::string> data = {
      {"data/project/Phoenix_773e8f4e.yaml", R"(
_profile: project
_id: 773e8f4e-8f4e-4e8f-943e-8f4e8f4e8f4e
_label: Phoenix
main_requirement: c73e8f4e-8f4e-4e8f-943e-8f4e8f4e8f4e
version: 1.0
)"},
      {"data/attachment/A-1_3e8fa6f5d.yaml", R"(
_profile: attachment
_id: 3e8fa6f5d-4c3b-2a1f-0e9d-8c7b6a5f4e3d
_label: A-1
_parent_id: 773e8f4e-8f4e-4e8f-943e-8f4e8f4e8f4e
filename: spec.pdf
filetype: pdf
path: /docs/spec.pdf
)"},
      {"data/requirement/R-1_c73e8f4e.yaml", R"(
_profile: requirement
_id: c73e8f4e-8f4e-4e8f-943e-8f4e8f4e8f4e
_label: R-1
_parent_id: 773e8f4e-8f4e-4e8f-943e-8f4e8f4e8f4e
title: Brake latency under 100 ms
)"},
      {"data/testcase/T-1_73e8f4e.yaml", R"(
_profile: testcase
_id: 73e8f4e-8f4e-4e8f-943e-8f4e8f4e8f4e
_label: T-1
_parent_id: c73e8f4e-8f4e-4e8f-943e-8f4e8f4e8f4e
title: Check latency below 100ms
)"}};

  SchemaManager sm;
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  EngineConfig cfg;
  InMemoryDataCommit committer;
  CoreEngine engine(backend, &committer, cfg);

  REQUIRE_NOTHROW(sm.loadSources(reg));
  REQUIRE_NOTHROW(engine.init(sm.registry()));
  std::unordered_map<std::string, YAML::Node> docs;
  for (const auto &[k, v] : data)
  {
    docs[k] = YAML::Load(v);
  }

  REQUIRE_NOTHROW(engine.loadData(docs));
}

TEST_CASE("CoreEngine: Check loading data of reference field which are within children is incorrect", "[CoreEngine][loadData][refs]")
{
  // --- Extend schema with a deeper child level (requirement → testcase)
  std::unordered_map<std::string, std::string> reg = {
      {"project.yaml", R"(
name: project
description: Node
kind: root
alias: Project
fields:
  main_requirement: { type: reference, target: requirement }
  version: { type: string, required: true }
children:
  requirements: { node: requirement }
  attachments:  { node: attachment }
)"},
      {"requirement.yaml", R"(
name: requirement
kind: node
description: Node
alias: Requirement
fields:
  title: { type: string, required: true }
children:
  testcases: { node: testcase }
)"},
      {"testcase.yaml", R"(
name: testcase
kind: node
description: Node
alias: TestCase
fields:
  title: { type: string, required: true }
)"},
      {"attachment.yaml", R"(
name: attachment
kind: node
description: Node
alias: Attachment
fields:
  filename: { type: string, required: true }
  filetype: { type: enum, values: [pdf, docx], required: true }
  path: { type: string, required: true }
)"}};

  std::unordered_map<std::string, std::string> data = {
      {"data/project/Phoenix_973e8f4e.yaml", R"(
_profile: project
_id: 973e8f4e-8f4e-4e8f-943e-8f4e8f4e8f4e
_label: Phoenix
main_requirement: 2987e8f4e-8f4e-4e8f-943e-8c7b6a5f4e3d
version: 1.0
)"},
      {"data/attachment/A-1_87e6f5d.yaml", R"(
_profile: attachment
_id: 87e6f5d-4c3b-2a1f-0e9d-8c7b6a5f4e3d
_label: A-1
_parent_id: 973e8f4e-8f4e-4e8f-943e-8c7b6a5f4e3d
filename: spec.pdf
filetype: pdf
path: /docs/spec.pdf
)"},
      {"data/requirement/R-1_c73e8f4e.yaml", R"(
_profile: requirement
_id: 236e4567-e89b-12d3-a456-426614174000
_label: R-1
_parent_id: 973e8f4e-8f4e-4e8f-943e-8c7b6a5f4e3d
title: Brake latency under 100 ms
)"},
      {"data/testcase/T-1_d87e8f4e.yaml", R"(
_profile: testcase
_id: d87e8f4e-8f4e-4e8f-943e-8c7b6a5f4e3d
_label: T-1
_parent_id: 236e4567-e89b-12d3-a456-426614174000
title: Check latency below 100ms
)"}};

  SchemaManager sm;
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  EngineConfig cfg;
  InMemoryDataCommit committer;
  CoreEngine engine(backend, &committer, cfg);

  REQUIRE_NOTHROW(sm.loadSources(reg));
  REQUIRE_NOTHROW(engine.init(sm.registry()));
  std::unordered_map<std::string, YAML::Node> docs;
  for (const auto &[k, v] : data)
  {
    docs[k] = YAML::Load(v);
  }

  REQUIRE_THROWS(engine.loadData(docs));

  // After failed load, engine should be reset
  REQUIRE_THROWS(engine.exists({"project", "973e8f4e-8f4e-4e8f-943e-8c7b6a5f4e3d"}));
  REQUIRE_THROWS(engine.exists({"requirement", "236e4567-e89b-12d3-a456-426614174000"}));
  REQUIRE_THROWS(engine.exists({"testcase", "d87e8f4e-8f4e-4e8f-943e-8c7b6a5f4e3d"}));
  REQUIRE_THROWS(engine.exists({"attachment", "87e6f5d-4c3b-2a1f-0e9d-8c7b6a5f4e3d"}));
}