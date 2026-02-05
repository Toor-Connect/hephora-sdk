// src/data_backend/sqlite/test/test_SQLiteDataBackend.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

#include "SQLiteDataBackend.h"
#include "SQLiteSchemaExporter.h"

#include "YamlSchemaLoader.h"
#include "YamlSchemaDecoder.h"
#include "SchemaRegistry.h"

#include "NodeAddress.h"
#include "FieldValue.h"
#include "Query.h"

#include <yaml-cpp/yaml.h>
#include <iostream>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::Equals;

// ---------- helpers to dump NodeSnapshot as YAML ----------
static YAML::Node toYamlFV(const FieldValue &v)
{
  YAML::Node n;
  std::visit(
      [&](auto &&arg)
      {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::monostate>)
        {
          // leave null
        }
        else if constexpr (std::is_same_v<T, int>)
        {
          n = arg;
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
          n = arg;
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
          n = arg;
        }
        else if constexpr (std::is_same_v<T, ArrayData>)
        {
          YAML::Node seq(YAML::NodeType::Sequence);
          for (const auto &item : arg)
            seq.push_back(toYamlFV(item));
          n = seq;
        }
        else if constexpr (std::is_same_v<T, ObjectData>)
        {
          YAML::Node map(YAML::NodeType::Map);
          for (const auto &kv : arg)
            map[kv.first] = toYamlFV(kv.second);
          n = map;
        }
      },
      static_cast<const std::variant<std::monostate, int, bool, std::string, ArrayData, ObjectData> &>(v));
  return n;
}

static std::string snapshotToYaml(const NodeSnapshot &s)
{
  if (!s.key.id.has_value())
    throw std::runtime_error("snapshotToYaml: _id is missing");

  YAML::Node root(YAML::NodeType::Map);
  root["_profile"] = s.key.profile;
  root["_id"] = *s.key.id;

  if (s.parent_id.has_value())
  {
    if (s.parent_id->empty())
      root["_parent_id"] = YAML::Null;
    else
      root["_parent_id"] = *s.parent_id;
  }

  if (s.label.has_value())
    root["_label"] = *s.label;

  for (const auto &kv : s.fields)
    root[kv.first] = toYamlFV(kv.second);

  YAML::Emitter out;
  out << root;
  return std::string(out.c_str());
}

static void dumpYaml(const char *tag, const NodeSnapshot &s)
{
  std::cout << "\n==== " << tag << " ====\n"
            << snapshotToYaml(s) << "\n";
}

// ---------- complex schema (adds 'user', embeds refs in an object, array-of-objects, self-refs) ----------
static SchemaRegistry build_registry_complex()
{
  SchemaRegistry reg;

  std::unordered_map<std::string, std::string> files = {
      {"project.yaml", R"(
name: project
kind: root
alias: Project
description: Top-level project
fields:
  project_name: { type: string, required: true }
  version:      { type: string, required: true }
children:
  requirements: { node: requirement }
)"},
      {"user.yaml", R"(
name: user
kind: node
alias: User
description: Person that can own / create items
fields:
  username: { type: string, required: true }
  role:     { type: enum, values: [admin, dev, qa], default: dev }
)"},
      {"requirement.yaml", R"(
name: requirement
kind: node
alias: Requirement
description: Complex requirement
fields:
  title:     { type: string, required: true }
  priority:  { type: enum, values: [low, medium, high], default: medium }
  active:    { type: boolean }

  # object with embedded references
  meta:
    type: object
    fields:
      owner:      { type: reference, target: user }
      created_by: { type: reference, target: user }
      severity:   { type: enum, values: [low, medium, high], default: medium }

  # array of simple objects (no nested arrays)
  checklist:
    type: array
    items:
      type: object
      fields:
        text:   { type: string, required: true }
        done:   { type: boolean, default: false }
        weight: { type: integer, default: 1 }

  # self-references as an array of refs
  deps:
    type: array
    items: { type: reference, target: requirement }
)"}};

  auto nodes = YamlSchemaLoader::loadFromSources(files);
  YamlSchemaDecoder::decodeProfiles(nodes, reg);
  return reg;
}

// ---------- schema ----------
static SchemaRegistry build_registry()
{
  SchemaRegistry reg;

  std::unordered_map<std::string, std::string> files = {
      {"project.yaml", R"(
name: project
kind: root
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

  auto nodes = YamlSchemaLoader::loadFromSources(files);
  YamlSchemaDecoder::decodeProfiles(nodes, reg);
  return reg;
}

// ---- Variant registry: like build_registry_complex() but meta.owner is REQUIRED ----
static SchemaRegistry build_registry_complex_required_owner()
{
  SchemaRegistry reg;
  std::unordered_map<std::string, std::string> files = {
      {"project.yaml", R"(
name: project
kind: root
alias: Project
description: Top-level project
fields:
  project_name: { type: string, required: true }
  version:      { type: string, required: true }
children:
  requirements: { node: requirement }
)"},
      {"user.yaml", R"(
name: user
kind: node
alias: User
description: User of the system
fields:
  username: { type: string, required: true }
  role:     { type: enum, values: [admin, dev, qa], default: dev }
)"},
      {"requirement.yaml", R"(
name: requirement
kind: node
alias: Requirement
description: Requirement of the system
fields:
  title:     { type: string, required: true }
  priority:  { type: enum, values: [low, medium, high], default: medium }
  active:    { type: boolean }

  meta:
    type: object
    fields:
      owner:      { type: reference, target: user, required: true }  # <— required
      created_by: { type: reference, target: user }
      severity:   { type: enum, values: [low, medium, high], default: medium }

  checklist:
    type: array
    items:
      type: object
      fields:
        text:   { type: string, required: true }
        done:   { type: boolean, default: false }
        weight: { type: integer, default: 1 }

  deps:
    type: array
    items: { type: reference, target: requirement }
)"}};
  auto nodes = YamlSchemaLoader::loadFromSources(files);
  YamlSchemaDecoder::decodeProfiles(nodes, reg);
  return reg;
}

// ---------- flexible builders (no hardcoded tags/refs/specs) ----------
static NodeSnapshot make_attachment(const std::string &filename,
                                    const std::string &filetype,
                                    const std::string &path,
                                    const std::string &parent = {},
                                    const ArrayData &tags = {})
{
  NodeSnapshot n;
  n.key.profile = "attachment";
  n.parent_id = parent;
  n.fields["filename"] = std::string(filename);
  n.fields["filetype"] = std::string(filetype);
  n.fields["path"] = std::string(path);
  if (!tags.empty())
    n.fields["tags"] = tags;
  return n;
}

static NodeSnapshot make_project(const std::string &name,
                                 const std::string &version)
{
  NodeSnapshot n;
  n.key.profile = "project";
  n.fields["project_name"] = std::string(name);
  n.fields["version"] = std::string(version);
  return n;
}

static NodeSnapshot make_requirement(const std::string &title,
                                     const std::string &priority,
                                     bool active,
                                     const std::string &parent,
                                     const ObjectData &specs,
                                     const ArrayData &tags,
                                     const ArrayData &references)
{
  NodeSnapshot n;
  n.key.profile = "requirement";
  n.parent_id = parent;

  n.fields["title"] = std::string(title);
  n.fields["priority"] = std::string(priority);
  n.fields["active"] = active;

  if (!specs.empty())
    n.fields["specs"] = specs;
  if (!tags.empty())
    n.fields["tags"] = tags;
  if (!references.empty())
    n.fields["references"] = references;

  return n;
}

static NodeSnapshot make_user(const std::string &username,
                              const std::string &role = "dev")
{
  NodeSnapshot n;
  n.key.profile = "user";
  n.fields["username"] = std::string(username);
  n.fields["role"] = std::string(role);
  return n;
}

static NodeSnapshot make_project2(const std::string &name,
                                  const std::string &version)
{
  NodeSnapshot n;
  n.key.profile = "project";
  n.fields["project_name"] = std::string(name);
  n.fields["version"] = std::string(version);
  return n;
}

static NodeSnapshot make_requirement_complex(
    const std::string &title,
    const std::string &priority,
    bool active,
    const std::string &parent_project_id,
    // meta.*
    const std::string &owner_user_id,      // empty = null
    const std::string &created_by_user_id, // empty = null
    const std::string &severity,
    // checklist (array of object rows)
    const std::vector<std::tuple<std::string, bool, int>> &checklistItems,
    // deps (array of requirement ids)
    const std::vector<std::string> &depsIds)
{
  NodeSnapshot n;
  n.key.profile = "requirement";
  n.parent_id = parent_project_id;

  n.fields["title"] = std::string(title);
  n.fields["priority"] = std::string(priority);
  n.fields["active"] = active;

  // meta object
  ObjectData meta;
  if (!owner_user_id.empty())
    meta["owner"] = std::string(owner_user_id);
  if (!created_by_user_id.empty())
    meta["created_by"] = std::string(created_by_user_id);
  meta["severity"] = std::string(severity);
  n.fields["meta"] = meta;

  // checklist (array of objects)
  ArrayData checklist;
  for (const auto &row : checklistItems)
  {
    ObjectData o;
    o["text"] = std::string(std::get<0>(row));
    o["done"] = (bool)std::get<1>(row);
    o["weight"] = (int)std::get<2>(row);
    checklist.emplace_back(o);
  }
  if (!checklist.empty())
    n.fields["checklist"] = checklist;

  // deps (array of refs)
  if (!depsIds.empty())
  {
    ArrayData deps;
    for (const auto &rid : depsIds)
      deps.emplace_back(std::string(rid));
    n.fields["deps"] = deps;
  }

  return n;
}

// ---------- tests ----------
TEST_CASE("SQLiteDataBackend: init + CRUD + arrays + refs + children", "[SQLite][backend]")
{
  auto reg = build_registry();

  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  REQUIRE_NOTHROW(backend.init(reg));

  // Insert project
  auto P1 = make_project("Demo Project", "1.0");
  backend.begin();
  REQUIRE_NOTHROW(backend.upsert(P1));
  backend.commit();

  // Dump project YAML from DB
  {
    auto p = backend.fetch({"project", P1.key.id.value()});
    REQUIRE(p.has_value());
    dumpYaml("project after insert", *p);
    std::cout << "project fields:";
    for (auto &kv : p->fields)
      std::cout << " " << kv.first;
    std::cout << "\n";
  }

  // Insert attachments (explicit tags)
  ArrayData a1tags;
  a1tags.emplace_back(std::string("doc"));
  ArrayData a2tags;
  a2tags.emplace_back(std::string("img"));
  auto A1 = make_attachment("Spec.pdf", "pdf", "/files/spec.pdf", P1.key.id.value(), a1tags);
  auto A2 = make_attachment("Design.png", "png", "/files/design.png", P1.key.id.value(), a2tags);
  backend.begin();
  backend.upsert(A1);
  backend.upsert(A2);
  backend.commit();

  // Dump attachments
  {
    auto a1 = backend.fetch({"attachment", A1.key.id.value()});
    auto a2 = backend.fetch({"attachment", A2.key.id.value()});
    REQUIRE(a1.has_value());
    REQUIRE(a2.has_value());
    dumpYaml("attachment A-1 after insert", *a1);
    dumpYaml("attachment A-2 after insert", *a2);
  }

  // Insert requirement (explicit specs/tags/refs)
  ObjectData specs;
  specs["manufacturer"] = std::string("ToorConnect");
  specs["warranty_years"] = 3;

  ArrayData reqTags;
  reqTags.emplace_back(std::string("safety"));
  reqTags.emplace_back(std::string("latency"));
  ArrayData reqRefs;
  reqRefs.emplace_back(A1.key.id.value());
  reqRefs.emplace_back(A2.key.id.value());

  auto R1 = make_requirement("Brake latency under 100 ms", "high", true, P1.key.id.value(),
                             specs, reqTags, reqRefs);
  backend.begin();
  backend.upsert(R1);
  backend.commit();

  // Dump requirement after insert
  {
    auto r = backend.fetch({"requirement", R1.key.id.value()});
    REQUIRE(r.has_value());
    dumpYaml("requirement after insert", *r);
  }

  REQUIRE(backend.exists({"requirement", R1.key.id.value()}));
  auto fetched = backend.fetch({"requirement", R1.key.id.value()});
  REQUIRE(fetched.has_value());
  CHECK(fetched->key.id == R1.key.id);
  CHECK(fetched->parent_id == P1.key.id);

  REQUIRE(fetched->fields.count("specs") == 1);
  REQUIRE(fetched->fields.at("specs").isObject());
  auto specsOut = fetched->fields.at("specs").asObject();
  CHECK(specsOut.at("manufacturer").asString() == "ToorConnect");
  CHECK(specsOut.at("warranty_years").asInteger() == 3);

  REQUIRE(fetched->fields.at("tags").isArray());
  auto tags = fetched->fields.at("tags").asArray();
  REQUIRE(tags.size() == 2);
  CHECK(tags[0].asString() == "safety");

  REQUIRE(fetched->fields.at("references").isArray());
  auto refs = fetched->fields.at("references").asArray();
  REQUIRE(refs.size() == 2);
  CHECK(refs[0].asString() == A1.key.id.value());
  CHECK(refs[1].asString() == A2.key.id.value());

  auto kids = backend.childrenOf({"project", P1.key.id.value()}, "requirement");
  REQUIRE(kids.size() == 1);
  CHECK(kids[0].id == R1.key.id.value());

  auto outbound = backend.refsFrom({"requirement", R1.key.id.value()}, "references");
  REQUIRE(outbound.size() == 2);
  CHECK(outbound[0].profile == "attachment");

  auto inbound = backend.refsTo({"attachment", A2.key.id.value()});
  REQUIRE_FALSE(inbound.empty());
  bool seenR1 = false;
  for (auto &k : inbound)
    if (k.profile == "requirement" && k.id == R1.key.id.value())
      seenR1 = true;
  CHECK(seenR1);

  // Delete A-1
  backend.begin();
  backend.remove({"attachment", A1.key.id.value()});
  backend.commit();

  // Dump requirement after deleting A-1 (references should drop nulls)
  {
    auto r2 = backend.fetch({"requirement", R1.key.id.value()});
    REQUIRE(r2.has_value());
    dumpYaml("requirement after deleting attachment", *r2);
  }

  auto fetched2 = backend.fetch({"requirement", R1.key.id.value()});
  REQUIRE(fetched2.has_value());
  auto refs2 = fetched2->fields.at("references").asArray();
  REQUIRE(refs2.size() == 1);
  CHECK(refs2[0].asString() == A2.key.id.value());
}

TEST_CASE("SQLiteDataBackend: QueryDNF + LIKE ops + IN + transactions", "[SQLite][query][txn]")
{
  auto reg = build_registry();
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  backend.begin();
  auto p = make_project("Demo", "1.0");
  backend.upsert(p);

  // R-1 with tag "safety" so it matches filters in the test
  ObjectData specs1;
  specs1["manufacturer"] = std::string("ToorConnect");
  specs1["warranty_years"] = 3;
  ArrayData r1tags;
  r1tags.emplace_back(std::string("safety"));
  ArrayData r1refs; // none
  auto r1 = make_requirement("Brake latency under 100 ms", "high", true, p.key.id.value(), specs1,
                             r1tags, r1refs);
  backend.upsert(r1);

  // R-2 with different priority and no "safety" tag
  ObjectData specs2;
  specs2["manufacturer"] = std::string("ToorConnect");
  specs2["warranty_years"] = 3;
  ArrayData r2tags; // none
  ArrayData r2refs;
  auto r2 = make_requirement("Dashboard refresh rate", "medium", false, p.key.id.value(), specs2,
                             r2tags, r2refs);
  backend.upsert(r2);
  backend.commit();

  QueryDNF q;
  q.profile = "requirement";
  q.any_of = {
      {FieldFilter{"priority", FieldOp::EQ, std::string("high")},
       FieldFilter{"active", FieldOp::EQ, true}},
      {FieldFilter{"tags", FieldOp::CONTAINS, std::string("safety")},
       FieldFilter{"active", FieldOp::EQ, true}}};
  q.order_by = {"priority", "title"};
  q.ascending = true;

  auto rows = backend.find(q);
  REQUIRE(rows.size() == 1);
  CHECK(rows[0].key.id == r1.key.id.value());

  QueryDNF q2;
  q2.profile = "requirement";
  q2.any_of = {{FieldFilter{"title", FieldOp::PREFIX, std::string("Brake")}}};
  auto rows2 = backend.find(q2);
  REQUIRE(rows2.size() == 1);
  CHECK(rows2[0].key.id == r1.key.id.value());

  ArrayData set;
  set.emplace_back(std::string("low"));
  set.emplace_back(std::string("medium"));
  QueryDNF q3;
  q3.profile = "requirement";
  q3.any_of = {{FieldFilter{"priority", FieldOp::IN, FieldValue(set)}}};
  auto rows3 = backend.find(q3);
  REQUIRE(rows3.size() == 1);
  CHECK(rows3[0].key.id == r2.key.id.value());

  backend.begin();
  // demonstrate rollback
  auto R3 = make_requirement("Temp change", "low", true, p.key.id.value(),
                             /*specs*/ ObjectData{}, /*tags*/ ArrayData{}, /*refs*/ ArrayData{});
  backend.upsert(R3);
  backend.rollback();
  CHECK_FALSE(backend.exists(R3.key));

  auto R4 = make_requirement("Temp change", "low", true, p.key.id.value(),
                             /*specs*/ ObjectData{}, /*tags*/ ArrayData{}, /*refs*/ ArrayData{});
  backend.begin();
  backend.upsert(R4);
  backend.commit();
  CHECK(backend.exists(R4.key));
}

TEST_CASE("SQLiteDataBackend: complex shapes (object refs, array-of-objects, self-refs, FKs)", "[SQLite][complex]")
{
  auto reg = build_registry_complex();

  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  REQUIRE_NOTHROW(backend.init(reg));

  // Project
  backend.begin();
  auto p2 = make_project2("Big Project", "2.0");
  backend.upsert(p2);

  // Users
  auto u1 = make_user("alice", "admin");
  auto u2 = make_user("bob", "qa");

  backend.upsert(u1);
  backend.upsert(u2);
  backend.commit();

  // R-10 with meta.owner = U-1, meta.created_by = U-2, checklist of 2 rows, no deps

  std::vector<std::tuple<std::string, bool, int>> cl = {
      {"Brake latency test", false, 1},
      {"Thermal run", true, 2}};
  auto R10 = make_requirement_complex("Latency budget satisfied", "high", true, p2.key.id.value(),
                                      /*owner*/ u1.key.id.value(), /*created_by*/ u2.key.id.value(), /*severity*/ "high",
                                      cl, /*deps*/ {});
  backend.begin();
  backend.upsert(R10);
  backend.commit();

  // Fetch and check roundtrip (object refs & array-of-objects)
  {
    auto r = backend.fetch(R10.key);
    REQUIRE(r.has_value());
    dumpYaml("R-10 after insert (complex)", *r);

    REQUIRE(r->fields.count("meta") == 1);
    auto meta = r->fields.at("meta").asObject();
    CHECK(meta.at("owner").asString() == u1.key.id.value());
    CHECK(meta.at("created_by").asString() == u2.key.id.value());
    CHECK(meta.at("severity").asString() == "high");

    REQUIRE(r->fields.count("checklist") == 1);
    auto cl = r->fields.at("checklist").asArray();
    REQUIRE(cl.size() == 2);
    CHECK(cl[0].asObject().at("text").asString() == "Brake latency test");
    CHECK(cl[0].asObject().at("done").asBoolean() == false);
    CHECK(cl[0].asObject().at("weight").asInteger() == 1);
    CHECK(cl[1].asObject().at("text").asString() == "Thermal run");
    CHECK(cl[1].asObject().at("done").asBoolean() == true);
    CHECK(cl[1].asObject().at("weight").asInteger() == 2);
  }

  std::vector<std::tuple<std::string, bool, int>> cl2 = {
      {"Render graphs", false, 1}};
  auto R11 = make_requirement_complex("Dashboard KPIs", "medium", false, p2.key.id.value(),
                                      /*owner*/ "", /*created_by*/ u2.key.id.value(), /*severity*/ "medium",
                                      cl2, /*deps*/ {R10.key.id.value()});

  // R-11 depends on R-10 (array of self-refs)
  {

    backend.begin();
    backend.upsert(R11);
    backend.commit();

    auto r11 = backend.fetch(R11.key);
    REQUIRE(r11.has_value());
    auto deps = r11->fields.at("deps").asArray();
    REQUIRE(deps.size() == 1);
    CHECK(deps[0].asString() == R10.key.id.value());
  }

  // Delete owner U-1 → meta.owner must become NULL (kept out of meta map); created_by remains U-2
  backend.begin();
  backend.remove(u1.key);
  backend.commit();

  {
    auto r = backend.fetch(R10.key);
    REQUIRE(r.has_value());
    dumpYaml("R-10 after deleting owner U-1", *r);

    REQUIRE(r->fields.count("meta") == 1);
    auto meta = r->fields.at("meta").asObject();

    // owner should be absent after ON DELETE SET NULL
    CHECK(meta.find("owner") == meta.end());
    // created_by still present
    CHECK(meta.at("created_by").asString() == u2.key.id.value());
  }

  // Delete R-10 → R-11.deps should drop nulls (read filters NULLs)
  backend.begin();
  backend.remove(R10.key);
  backend.commit();

  {
    auto r11 = backend.fetch(R11.key);
    REQUIRE(r11.has_value());
    auto deps = r11->fields.at("deps").asArray();
    CHECK(deps.empty()); // our reader ignores nulls in ref arrays
  }

  // Query: object subfield filter (meta.created_by == u2.key.id.value())
  {
    QueryDNF q;
    q.profile = "requirement";
    q.any_of = {{FieldFilter{"meta.created_by", FieldOp::EQ, std::string(u2.key.id.value())}}};
    auto rows = backend.find(q);
    // After deletions, only R-11 should match
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].key.id == R11.key.id.value());
  }

  // Partial update: replace checklist on R-11 (keep label/parent)
  {
    std::vector<std::tuple<std::string, bool, int>> cl3 = {
        {"Render graphs", true, 2},
        {"Publish snapshot", false, 1},
        {"Notify channel", false, 1}};
    auto patch = make_requirement_complex("Dashboard KPIs", "medium", false, p2.key.id.value(),
                                          /*owner*/ "", /*created_by*/ u2.key.id.value(), /*severity*/ "medium",
                                          cl3, /*deps*/ {});
    patch.key = R11.key; // keep same key
    backend.begin();
    backend.upsert(patch);
    backend.commit();

    auto r11 = backend.fetch(R11.key);
    REQUIRE(r11.has_value());
    auto cl = r11->fields.at("checklist").asArray();
    REQUIRE(cl.size() == 3);
    CHECK(cl[0].asObject().at("done").asBoolean() == true);
  }
}

TEST_CASE("SQLiteDataBackend: defaults + required enforcement", "[SQLite][defaults][required]")
{
  auto reg = build_registry_complex();
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  // Insert minimal project + users
  backend.begin();
  auto p = make_project2("DefaultsProj", "9.0");
  auto u = make_user("zoe", "qa");
  backend.upsert(p);
  backend.upsert(u);
  backend.commit();

  // Insert requirement omitting priority and omitting meta entirely
  // Expect: priority defaults to 'medium'; meta.severity defaults to 'medium'
  {
    NodeSnapshot r;
    r.key.profile = "requirement";
    r.parent_id = p.key.id;
    r.fields["title"] = std::string("Minimal");
    // no priority, no meta, no arrays
    backend.begin();
    backend.upsert(r);
    backend.commit();

    auto got = backend.fetch(r.key);
    REQUIRE(got.has_value());
    // priority default
    REQUIRE(got->fields.count("priority") == 1);
    CHECK(got->fields.at("priority").asString() == "medium");
    // meta.severity default — meta may be synthesized because of flattened column
    REQUIRE(got->fields.count("meta") == 1);
    auto meta = got->fields.at("meta").asObject();
    CHECK(meta.at("severity").asString() == "medium");
    // owner/created_by absent
    CHECK(meta.find("owner") == meta.end());
    CHECK(meta.find("created_by") == meta.end());
  }

  // Required in array-of-objects: checklist[].text is required -> missing should fail
  {
    NodeSnapshot r;
    r.key.profile = "requirement";
    r.parent_id = p.key.id;
    r.fields["title"] = std::string("Bad row");
    ArrayData checklist;
    ObjectData row;
    // row["text"] missing on purpose
    row["done"] = false;
    row["weight"] = 1;
    checklist.emplace_back(row);
    r.fields["checklist"] = checklist;

    backend.begin();
    REQUIRE_THROWS(backend.upsert(r)); // NOT NULL constraint on text
    backend.rollback();
    CHECK_FALSE(backend.exists(r.key));
  }
}

TEST_CASE("SQLiteDataBackend: partial update preserves arrays and object subfields", "[SQLite][upsert][partial]")
{
  auto reg = build_registry();
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  backend.begin();
  auto p = make_project("Proj", "1.0");
  backend.upsert(p);

  // Insert requirement with tags + specs
  ObjectData specs;
  specs["manufacturer"] = std::string("Acme");
  specs["warranty_years"] = 5;
  ArrayData tags;
  tags.emplace_back(std::string("alpha"));
  tags.emplace_back(std::string("beta"));
  auto r = make_requirement("Title", "low", true, p.key.id.value(), specs, tags, /*refs*/ ArrayData{});
  backend.upsert(r);
  backend.commit();

  // Patch ONLY title (no arrays/specs provided) -> arrays/specs must remain as-is
  NodeSnapshot patch;
  patch.key = r.key;
  patch.parent_id = p.key.id.value();
  patch.fields["title"] = std::string("Title v2");

  backend.begin();
  backend.upsert(patch);
  backend.commit();

  auto got = backend.fetch(r.key);
  REQUIRE(got.has_value());
  CHECK(got->fields.at("title").asString() == "Title v2");
  // arrays preserved
  REQUIRE(got->fields.at("tags").isArray());
  auto tags2 = got->fields.at("tags").asArray();
  REQUIRE(tags2.size() == 2);
  CHECK(tags2[0].asString() == "alpha");
  // object preserved
  auto specs2 = got->fields.at("specs").asObject();
  CHECK(specs2.at("warranty_years").asInteger() == 5);
}

TEST_CASE("SQLiteDataBackend: required single reference blocks deletion (FK)", "[SQLite][FK][required-ref]")
{
  auto reg = build_registry_complex_required_owner();
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  backend.begin();
  auto p5 = make_project("FKProj", "1.0");
  auto u1 = make_user("alice", "admin");
  auto u2 = make_user("bob", "qa");
  backend.upsert(p5);
  backend.upsert(u1);
  backend.upsert(u2);

  // requirement with required owner
  auto r = make_requirement_complex("Needs owner", "high", true, p5.key.id.value(),
                                    /*owner*/ u1.key.id.value(), /*created_by*/ u2.key.id.value(), /*severity*/ "high",
                                    /*checklist*/ {{"t", false, 1}}, /*deps*/ {});
  backend.upsert(r);
  backend.commit();

  // Attempt to delete U-1 should fail (NOT NULL + ON DELETE SET NULL incompat -> FK error)
  backend.begin();
  REQUIRE_THROWS(backend.remove(u1.key));
  backend.rollback();

  // Still intact
  auto got = backend.fetch(r.key);
  REQUIRE(got.has_value());
  auto meta = got->fields.at("meta").asObject();
  CHECK(meta.at("owner").asString() == u1.key.id.value());
}

TEST_CASE("SQLiteDataBackend: cascading delete of parent removes children and array rows", "[SQLite][cascade]")
{
  auto reg = build_registry();
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  backend.begin();
  auto p7 = make_project("CascadeProj", "1.0");
  backend.upsert(p7);

  ObjectData specs;
  specs["manufacturer"] = std::string("ToorConnect");
  specs["warranty_years"] = 2;

  ArrayData tags;
  tags.emplace_back(std::string("x"));
  tags.emplace_back(std::string("y"));

  auto r = make_requirement("T", "low", true, p7.key.id.value(), specs, tags, ArrayData{});
  backend.upsert(r);
  backend.commit();

  // Sanity
  REQUIRE(backend.exists(r.key));
  auto before = backend.fetch(r.key);
  REQUIRE(before.has_value());
  REQUIRE(before->fields.at("tags").asArray().size() == 2);

  // Delete parent project -> child requirement and its arrays must be gone
  backend.begin();
  backend.remove(p7.key);
  backend.commit();

  CHECK_FALSE(backend.exists(r.key));
  auto after = backend.fetch(r.key);
  CHECK_FALSE(after.has_value());
}

TEST_CASE("SQLiteDataBackend: query coverage (NULL ops, IN/NOT_IN, order, paging)", "[SQLite][query]")
{
  auto reg = build_registry_complex();
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  backend.begin();
  auto p8 = make_project2("QProj", "1.0");
  auto u1 = make_user("alice", "admin");
  auto u2 = make_user("bob", "qa");
  backend.upsert(p8);
  backend.upsert(u1);
  backend.upsert(u2);

  // Three requirements, with different priorities and meta.created_by/owner
  auto R1 = make_requirement_complex("A", "low", true, p8.key.id.value(),
                                     /*owner*/ "", /*created_by*/ u2.key.id.value(), /*severity*/ "low",
                                     /*checklist*/ {{"c1", false, 1}}, /*deps*/ {});
  auto R2 = make_requirement_complex("B", "medium", false, p8.key.id.value(),
                                     /*owner*/ u1.key.id.value(), /*created_by*/ "", /*severity*/ "medium",
                                     /*checklist*/ {}, /*deps*/ {});
  auto R3 = make_requirement_complex("C", "high", true, p8.key.id.value(),
                                     /*owner*/ u1.key.id.value(), /*created_by*/ u2.key.id.value(), /*severity*/ "high",
                                     /*checklist*/ {}, /*deps*/ {});
  backend.upsert(R1);
  backend.upsert(R2);
  backend.upsert(R3);
  backend.commit();

  // IS_NULL on meta.owner (flattened)
  {
    QueryDNF q;
    q.profile = "requirement";
    q.any_of = {{FieldFilter{"meta.owner", FieldOp::IS_NULL, FieldValue{}}}};
    auto rows = backend.find(q);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].key.id == R1.key.id.value());
  }

  // NOT_NULL on meta.created_by
  {
    QueryDNF q;
    q.profile = "requirement";
    q.any_of = {{FieldFilter{"meta.created_by", FieldOp::NOT_NULL, FieldValue{}}}};
    auto rows = backend.find(q);
    // R-Q1 and R-Q3 have created_by
    REQUIRE(rows.size() == 2);
  }

  // IN / NOT_IN on priority
  {
    ArrayData inSet;
    inSet.emplace_back(std::string("low"));
    inSet.emplace_back(std::string("high"));
    QueryDNF qIn;
    qIn.profile = "requirement";
    qIn.any_of = {{FieldFilter{"priority", FieldOp::IN, FieldValue(inSet)}}};
    auto inRows = backend.find(qIn);
    REQUIRE(inRows.size() == 2);

    ArrayData notInSet;
    notInSet.emplace_back(std::string("low"));
    notInSet.emplace_back(std::string("high"));
    QueryDNF qNotIn;
    qNotIn.profile = "requirement";
    qNotIn.any_of = {{FieldFilter{"priority", FieldOp::NOT_IN, FieldValue(notInSet)}}};
    auto notInRows = backend.find(qNotIn);
    REQUIRE(notInRows.size() == 1);
    CHECK(notInRows[0].key.id == R2.key.id.value());
  }

  // ORDER BY title DESC with LIMIT/OFFSET
  {
    QueryDNF q;
    q.profile = "requirement";
    q.any_of = {{}}; // match all
    q.order_by = {"title"};
    q.ascending = false; // C, B, A
    q.limit = 2;
    q.offset = 1; // skip C -> expect [B, A]
    auto rows = backend.find(q);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].key.id == R2.key.id.value());
    CHECK(rows[1].key.id == R1.key.id.value());
  }
}

TEST_CASE("SQLiteDataBackend: explicit NULL vs omission on scalars & object subfields", "[SQLite][nulls][partial]")
{
  auto reg = build_registry(); // simple project/requirement/attachment schema
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  // Seed a project
  backend.begin();
  auto p10 = make_project("NullProj", "1.0");
  backend.upsert(p10);
  backend.commit();

  ObjectData specs;
  specs["manufacturer"] = std::string("Acme");
  specs["warranty_years"] = 5;

  ArrayData tags; // not used here
  backend.begin();
  auto r = make_requirement("Base", "high", true, p10.key.id.value(), specs, tags, /*refs*/ ArrayData{});
  backend.upsert(r);
  backend.commit();

  // Insert a requirement with priority and specs.manufacturer set
  {
    auto got = backend.fetch(r.key);
    REQUIRE(got.has_value());
    CHECK(got->fields.at("priority").asString() == "high");
    auto s = got->fields.at("specs").asObject();
    CHECK(s.at("manufacturer").asString() == "Acme");
    CHECK(s.at("warranty_years").asInteger() == 5);
  }

  // Patch that OMITS both 'priority' and 'specs' -> both should be preserved
  {
    NodeSnapshot patch;
    patch.key = r.key;
    patch.parent_id = r.parent_id;
    patch.fields["title"] = std::string("Base v2");

    backend.begin();
    backend.upsert(patch);
    backend.commit();

    auto got = backend.fetch(r.key);
    REQUIRE(got.has_value());
    // priority preserved
    REQUIRE(got->fields.count("priority") == 1);
    CHECK(got->fields.at("priority").asString() == "high");
    // specs preserved
    REQUIRE(got->fields.count("specs") == 1);
    auto s = got->fields.at("specs").asObject();
    CHECK(s.at("manufacturer").asString() == "Acme");
    CHECK(s.at("warranty_years").asInteger() == 5);
  }

  // Patch with EXPLICIT NULL for scalar 'priority' -> should clear column, field disappears on fetch
  {
    NodeSnapshot patch;
    patch.key = r.key;
    patch.parent_id = r.parent_id;
    patch.fields["title"] = std::string("Base v3");
    patch.fields["priority"] = FieldValue{}; // explicit NULL

    backend.begin();
    backend.upsert(patch);
    backend.commit();

    auto got = backend.fetch(r.key);
    REQUIRE(got.has_value());
    CHECK(got->fields.find("priority") == got->fields.end()); // cleared
  }

  // Patch with EXPLICIT NULL for object subfield 'specs.manufacturer'
  // Keep specs present and set manufacturer to null, leaving warranty_years intact.
  {
    ObjectData specsPatch;
    specsPatch["manufacturer"] = FieldValue{}; // explicit NULL
    // omit warranty_years -> preserved
    NodeSnapshot patch;
    patch.key = r.key;
    patch.parent_id = r.parent_id;
    patch.fields["title"] = std::string("Base v4");
    patch.fields["specs"] = specsPatch;

    backend.begin();
    backend.upsert(patch);
    backend.commit();

    auto got = backend.fetch(r.key);
    REQUIRE(got.has_value());
    REQUIRE(got->fields.count("specs") == 1);
    auto s = got->fields.at("specs").asObject();
    // manufacturer cleared -> absent from reconstructed object
    CHECK(s.find("manufacturer") == s.end());
    // warranty_years preserved
    CHECK(s.at("warranty_years").asInteger() == 5);
  }
}

TEST_CASE("SQLiteDataBackend: arrays — omission vs empty vs explicit NULL", "[SQLite][arrays][partial]")
{
  auto reg = build_registry(); // project/requirement/attachment schema
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  // Seed project + attachments (so 'references' can point to something)
  backend.begin();
  auto pA = make_project("ArrProj", "1.0");
  backend.upsert(pA);
  auto A1 = make_attachment("Spec.pdf", "pdf", "/f/spec.pdf", pA.key.id.value(), /*tags*/ ArrayData{});
  auto A2 = make_attachment("Design.png", "png", "/f/design.png", pA.key.id.value(), /*tags*/ ArrayData{});
  backend.upsert(A1);
  backend.upsert(A2);
  backend.commit();

  ObjectData specs;
  specs["manufacturer"] = std::string("Acme");
  specs["warranty_years"] = 1;

  ArrayData tags;
  tags.emplace_back(std::string("x"));
  tags.emplace_back(std::string("y"));

  ArrayData refs;
  refs.emplace_back(std::string(A1.key.id.value()));
  refs.emplace_back(std::string(A2.key.id.value()));

  backend.begin();
  auto rArr = make_requirement("Arrays base", "medium", true, pA.key.id.value(), specs, tags, refs);
  backend.upsert(rArr);
  backend.commit();

  // Insert requirement with tags ["x","y"] and references [A1.key.id.value(),A2.key.id.value()]
  {

    auto got = backend.fetch(rArr.key);
    REQUIRE(got.has_value());
    REQUIRE(got->fields.at("tags").isArray());
    REQUIRE(got->fields.at("references").isArray());
    CHECK(got->fields.at("tags").asArray().size() == 2);
    CHECK(got->fields.at("references").asArray().size() == 2);
  }

  // 1) Omit arrays in patch -> both arrays must be preserved
  {
    NodeSnapshot patch;
    patch.key = rArr.key;
    patch.parent_id = rArr.parent_id;
    patch.fields["title"] = std::string("Arrays base v2"); // only scalar change

    backend.begin();
    backend.upsert(patch);
    backend.commit();

    auto got = backend.fetch(rArr.key);
    REQUIRE(got.has_value());
    auto t = got->fields.at("tags").asArray();
    auto r = got->fields.at("references").asArray();
    CHECK(t.size() == 2); // preserved
    CHECK(r.size() == 2); // preserved
  }

  // 2) Provide empty array for 'tags' -> side table purged, fetch returns empty
  {
    NodeSnapshot patch;
    patch.key = rArr.key;
    patch.parent_id = rArr.parent_id;
    patch.fields["title"] = std::string("Arrays base v3");
    patch.fields["tags"] = ArrayData{}; // explicit empty -> clear

    backend.begin();
    backend.upsert(patch);
    backend.commit();

    auto got = backend.fetch(rArr.key);
    REQUIRE(got.has_value());
    auto t = got->fields.at("tags").asArray();
    CHECK(t.empty()); // cleared
    // references remained untouched (omitted)
    CHECK(got->fields.at("references").asArray().size() == 2);
  }

  // 3) Provide EXPLICIT NULL for 'references' -> treated as present but not array: purge happens, writer no-ops -> cleared
  {
    NodeSnapshot patch;
    patch.key = rArr.key;
    patch.parent_id = rArr.parent_id;
    patch.fields["title"] = std::string("Arrays base v4");
    patch.fields["references"] = FieldValue{}; // explicit null -> clear refs

    backend.begin();
    backend.upsert(patch);
    backend.commit();

    auto got = backend.fetch(rArr.key);
    REQUIRE(got.has_value());
    auto r = got->fields.at("references").asArray();
    CHECK(r.empty()); // cleared
    // tags remained as we left them (empty)
    CHECK(got->fields.at("tags").asArray().empty());
  }
}

TEST_CASE("SQLiteDataBackend: single ref in object — omission vs explicit NULL + FK SET NULL", "[SQLite][ref][object-subfield]")
{
  auto reg = build_registry_complex(); // has requirement.meta.{owner,created_by}
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  // Seed project + users
  backend.begin();
  auto pSr = make_project2("SingleRefProj", "1.0");
  auto u1 = make_user("alice", "admin");
  auto u2 = make_user("bob", "qa");
  backend.upsert(pSr);
  backend.upsert(u1);
  backend.upsert(u2);
  backend.commit();

  auto rSr = make_requirement_complex("Ref test", "medium", true, pSr.key.id.value(),
                                      /*owner*/ u1.key.id.value(), /*created_by*/ u2.key.id.value(), /*severity*/ "medium",
                                      /*checklist*/ {}, /*deps*/ {});

  // Insert requirement with both refs set
  {
    backend.begin();
    backend.upsert(rSr);
    backend.commit();

    auto got = backend.fetch(rSr.key);
    REQUIRE(got.has_value());
    auto meta = got->fields.at("meta").asObject();
    CHECK(meta.at("owner").asString() == u1.key.id.value());
    CHECK(meta.at("created_by").asString() == u2.key.id.value());
  }

  // 1) Omit meta.owner in patch (only touch created_by) -> owner must be preserved
  {
    NodeSnapshot patch;
    patch.key = rSr.key;
    patch.parent_id = rSr.parent_id;

    ObjectData metaPatch;
    metaPatch["created_by"] = std::string(u2.key.id.value()); // unchanged, but proves partial object update
    patch.fields["meta"] = metaPatch;

    backend.begin();
    backend.upsert(patch);
    backend.commit();

    auto got = backend.fetch(rSr.key);
    REQUIRE(got.has_value());
    auto meta = got->fields.at("meta").asObject();
    // owner preserved (was omitted in patch)
    CHECK(meta.at("owner").asString() == u1.key.id.value());
    CHECK(meta.at("created_by").asString() == u2.key.id.value());
  }

  // 2) Explicit NULL for meta.owner -> clears the reference
  {
    NodeSnapshot patch;
    patch.key = rSr.key;
    patch.parent_id = rSr.parent_id;

    ObjectData metaPatch;
    metaPatch["owner"] = FieldValue{}; // explicit null -> bind NULL
    patch.fields["meta"] = metaPatch;

    backend.begin();
    backend.upsert(patch);
    backend.commit();

    auto got = backend.fetch(rSr.key);
    REQUIRE(got.has_value());
    auto meta = got->fields.at("meta").asObject();
    // owner should now be absent (cleared)
    CHECK(meta.find("owner") == meta.end());
    // created_by remains from before
    CHECK(meta.at("created_by").asString() == u2.key.id.value());
  }

  // 3) Delete U-2 (created_by target) -> ON DELETE SET NULL should clear meta.created_by
  backend.begin();
  backend.remove(u2.key);
  backend.commit();

  {
    auto got = backend.fetch(rSr.key);
    REQUIRE(got.has_value());
    auto meta = got->fields.at("meta").asObject();
    // created_by should now be absent due to FK SET NULL
    CHECK(meta.find("created_by") == meta.end());
    // owner already null from step 2
    CHECK(meta.find("owner") == meta.end());
  }
}

TEST_CASE("SQLiteDataBackend: single ref in object — explicit NULL clears ref and allows later FK deletion", "[SQLite][ref][object-subfield][nullify]")
{
  auto reg = build_registry_complex(); // requirement.meta.{owner,created_by}
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  // Seed project + users
  backend.begin();
  auto pSr2 = make_project2("NullifyRefProj", "1.0");
  auto u1 = make_user("alice", "admin");
  auto u2 = make_user("bob", "qa");
  backend.upsert(pSr2);
  backend.upsert(u1);
  backend.upsert(u2);
  backend.commit();

  auto rSr2 = make_requirement_complex("Ref test 2", "medium", true, pSr2.key.id.value(),
                                       /*owner*/ u1.key.id.value(), /*created_by*/ u2.key.id.value(), /*severity*/ "medium",
                                       /*checklist*/ {}, /*deps*/ {});

  // Insert requirement with both refs set
  {

    backend.begin();
    backend.upsert(rSr2);
    backend.commit();

    auto got = backend.fetch(rSr2.key);
    REQUIRE(got.has_value());
    auto meta = got->fields.at("meta").asObject();
    CHECK(meta.at("owner").asString() == u1.key.id.value());
    CHECK(meta.at("created_by").asString() == u2.key.id.value());
  }

  // Explicitly NULL-out meta.created_by in a patch
  {
    NodeSnapshot patch;
    patch.key = rSr2.key;
    patch.parent_id = rSr2.parent_id;

    ObjectData metaPatch;
    metaPatch["created_by"] = FieldValue{}; // explicit NULL
    patch.fields["meta"] = metaPatch;

    backend.begin();
    REQUIRE_NOTHROW(backend.upsert(patch));
    backend.commit();

    auto got = backend.fetch(rSr2.key);
    REQUIRE(got.has_value());
    auto meta = got->fields.at("meta").asObject();

    // created_by was explicitly cleared → absent from reconstructed object
    CHECK(meta.find("created_by") == meta.end());
    // owner untouched
    CHECK(meta.at("owner").asString() == u1.key.id.value());
  }

  // Deleting U-2 now should succeed cleanly (column is already NULL)
  backend.begin();
  REQUIRE_NOTHROW(backend.remove(u2.key));
  backend.commit();

  // Row still valid, meta.created_by remains absent
  auto finalRow = backend.fetch(rSr2.key);
  REQUIRE(finalRow.has_value());
  auto meta2 = finalRow->fields.at("meta").asObject();
  CHECK(meta2.find("created_by") == meta2.end());
  CHECK(meta2.at("owner").asString() == u1.key.id.value());
}

TEST_CASE("SQLiteDataBackend: refsTo covers object-embedded single refs and array-of-refs", "[SQLite][refsTo]")
{
  auto reg = build_registry_complex(); // has user + requirement(meta.owner/meta.created_by + deps[])
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  // Seed project + users
  backend.begin();
  auto pRt = make_project2("RefsToProj", "1.0");
  auto u1 = make_user("alice", "admin");
  auto u2 = make_user("bob", "qa");

  backend.upsert(pRt);
  backend.upsert(u1);
  backend.upsert(u2);
  backend.commit();

  // Base requirement that others can depend on (no owner/created_by)
  auto Rbase = make_requirement_complex("Base", "low", true, pRt.key.id.value(),
                                        /*owner*/ "", /*created_by*/ "", /*severity*/ "low",
                                        /*checklist*/ {}, /*deps*/ {});
  // Requirement that:
  //  - has object-embedded single ref: meta.owner=U-1
  //  - depends on R-BASE via array-of-refs (deps[])

  backend.begin();
  backend.upsert(Rbase);
  auto Rdep = make_requirement_complex("Depends", "medium", true, pRt.key.id.value(),
                                       /*owner*/ u1.key.id.value(), /*created_by*/ u2.key.id.value(), /*severity*/ "medium",
                                       /*checklist*/ {}, /*deps*/ {Rbase.key.id.value()});
  backend.upsert(Rdep);
  backend.commit();

  // 1) refsTo on user U-1 should include requirement R-DEP (because meta.owner=U-1)
  {
    auto rev = backend.refsTo(u1.key);
    bool found = false;
    for (const auto &k : rev)
      if (k.profile == "requirement" && k.id == Rdep.key.id.value())
        found = true;
    REQUIRE(found);
  }

  // 2) refsTo on requirement R-BASE should include requirement R-DEP (because deps[] contains R-BASE)
  {
    auto rev = backend.refsTo(Rbase.key);
    bool found = false;
    for (const auto &k : rev)
      if (k.profile == "requirement" && k.id == Rdep.key.id.value())
        found = true;
    REQUIRE(found);
  }
}

TEST_CASE("SQLiteDataBackend: refsFrom on self-ref array returns correct targets", "[SQLite][refsFrom]")
{
  auto reg = build_registry_complex(); // requirement has deps: array<reference requirement>
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  // Seed project
  backend.begin();
  auto pRf = make_project2("RefsFromProj", "1.0");
  backend.upsert(pRf);
  backend.commit();

  // Create base requirement R-BASE (no deps)
  auto Rbase = make_requirement_complex("Base", "low", true, pRf.key.id.value(),
                                        /*owner*/ "", /*created_by*/ "", /*severity*/ "low",
                                        /*checklist*/ {}, /*deps*/ {});

  backend.begin();
  backend.upsert(Rbase);
  // Create dependent requirement R-DEP with deps=["R-BASE"]
  auto Rdep = make_requirement_complex("Depends", "medium", true, pRf.key.id.value(),
                                       /*owner*/ "", /*created_by*/ "", /*severity*/ "medium",
                                       /*checklist*/ {}, /*deps*/ {Rbase.key.id.value()});
  backend.upsert(Rdep);
  backend.commit();

  // refsFrom should point from R-DEP.deps -> R-BASE
  auto out = backend.refsFrom(Rdep.key, "deps");
  REQUIRE(out.size() == 1);
  CHECK(out[0].profile == "requirement");
  CHECK(out[0].id == Rbase.key.id.value());
}

TEST_CASE("SQLiteDataBackend: explicit NULL clears non-required object sub-ref", "[SQLite][ref][patch-null]")
{
  auto reg = build_registry_complex(); // created_by is NOT required
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  // Seed project + users
  backend.begin();
  auto pNr = make_project2("NullRefProj", "1.0");
  auto u1 = make_user("alice", "admin");
  auto u2 = make_user("bob", "qa");
  backend.upsert(pNr);
  backend.upsert(u1);
  backend.upsert(u2);
  backend.commit();

  auto R = make_requirement_complex("Clearable created_by", "medium", true, pNr.key.id.value(),
                                    /*owner*/ "", /*created_by*/ u2.key.id.value(), /*severity*/ "medium",
                                    /*checklist*/ {}, /*deps*/ {});

  // Insert requirement with created_by = U-2
  {
    backend.begin();
    backend.upsert(R);
    backend.commit();

    // Sanity: refsTo(U-2) should include R-NR
    auto rev0 = backend.refsTo(u2.key);
    bool seen0 = false;
    for (const auto &k : rev0)
      if (k.profile == "requirement" && k.id == R.key.id.value())
        seen0 = true;
    REQUIRE(seen0);
  }

  // Patch with explicit NULL for meta.created_by (allowed: not required)
  {
    NodeSnapshot patch;
    patch.key = R.key;
    patch.parent_id = R.parent_id;

    ObjectData metaPatch;
    metaPatch["created_by"] = FieldValue{}; // explicit null
    patch.fields["meta"] = metaPatch;

    backend.begin();
    backend.upsert(patch);
    backend.commit();
  }

  // After patch: created_by should be absent in meta map, and refsTo(U-2) should no longer include R-NR
  {
    auto got = backend.fetch(R.key);
    REQUIRE(got.has_value());
    REQUIRE(got->fields.count("meta") == 1);
    auto meta = got->fields.at("meta").asObject();
    CHECK(meta.find("created_by") == meta.end()); // cleared → omitted

    auto rev = backend.refsTo(u2.key);
    bool stillThere = false;
    for (const auto &k : rev)
      if (k.profile == "requirement" && k.id == R.key.id.value())
        stillThere = true;
    CHECK_FALSE(stillThere);
  }
}

TEST_CASE("SQLiteDataBackend: QueryDNF CONTAINS on array<reference>", "[SQLite][query][contains-ref-array]")
{
  auto reg = build_registry(); // requirement.references: array<reference target=attachment>
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  // Seed project + two attachments
  backend.begin();
  auto pQc = make_project("QDnfContains", "1.0");
  backend.upsert(pQc);
  auto a1 = make_attachment("Spec.pdf", "pdf", "/files/spec.pdf", pQc.key.id.value());
  auto a2 = make_attachment("Design.png", "png", "/files/design.png", pQc.key.id.value());
  backend.upsert(a1);
  backend.upsert(a2);

  // Requirement that references only A-2
  ObjectData specs;
  specs["manufacturer"] = std::string("Acme");
  specs["warranty_years"] = 1;

  ArrayData refs;
  refs.emplace_back(std::string(a2.key.id.value())); // only A-2

  auto rRef = make_requirement("Ref A-2 only", "high", true, pQc.key.id.value(),
                               specs, /*tags*/ ArrayData{}, refs);
  backend.upsert(rRef);
  backend.commit();

  // references CONTAINS 'A-2' -> find R-REF
  {
    QueryDNF q;
    q.profile = "requirement";
    q.any_of = {{FieldFilter{"references", FieldOp::CONTAINS, std::string(a2.key.id.value())}}};
    auto rows = backend.find(q);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].key.id == rRef.key.id.value());
  }

  // references CONTAINS 'A-1' -> no match
  {
    QueryDNF q;
    q.profile = "requirement";
    q.any_of = {{FieldFilter{"references", FieldOp::CONTAINS, std::string(a1.key.id.value())}}};
    auto rows = backend.find(q);
    CHECK(rows.empty());
  }
}

TEST_CASE("SQLiteDataBackend: parentOf returns null for roots and parent id for children", "[SQLite][parentOf]")
{
  auto reg = build_registry(); // project (root) with requirement as child
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  // Seed a root project and a child requirement
  backend.begin();
  auto pPa = make_project("ParentOfProj", "1.0");
  backend.upsert(pPa);

  ObjectData specs;
  specs["manufacturer"] = std::string("Acme");
  specs["warranty_years"] = 2;

  ArrayData emptyArr;

  auto rPa = make_requirement("Child requirement", "high", true,
                              /*parent*/ pPa.key.id.value(), specs, emptyArr, emptyArr);

  backend.upsert(rPa);
  backend.commit();

  // 1) Root has no parent
  {
    auto p = backend.parentOf(pPa.key);
    CHECK_FALSE(p.has_value());
  }

  // 2) Child points to its parent
  {
    auto p = backend.parentOf(rPa.key);
    REQUIRE(p.has_value());
    CHECK(*p == pPa.key.id.value());
  }
}

TEST_CASE("SQLiteDataBackend: refsFrom returns array-of-refs and drops nulls after target delete", "[SQLite][refsFrom]")
{
  auto reg = build_registry_complex(); // has requirement.deps: array<reference requirement>
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  // Seed project + two requirements where R-B depends on R-A
  backend.begin();
  auto pRf = make_project2("RefsFromProj", "1.0");
  backend.upsert(pRf);
  auto R_A = make_requirement_complex("Base A", "low", true, pRf.key.id.value(),
                                      /*owner*/ "", /*created_by*/ "", /*severity*/ "low",
                                      /*checklist*/ {}, /*deps*/ {});

  backend.upsert(R_A);
  auto R_B = make_requirement_complex("Depends on A", "medium", true, pRf.key.id.value(),
                                      /*owner*/ "", /*created_by*/ "", /*severity*/ "medium",
                                      /*checklist*/ {}, /*deps*/ {R_A.key.id.value()});
  backend.upsert(R_B);
  backend.commit();

  // 1) refsFrom on R-B for field "deps" should include requirement R-A
  {
    auto out = backend.refsFrom(R_B.key, "deps");
    bool foundA = false;
    for (const auto &k : out)
      if (k.profile == "requirement" && k.id == R_A.key.id.value())
        foundA = true;
    REQUIRE(foundA);
  }

  // 2) Delete R-A -> outbound deps from R-B should drop NULLs (reader filters them)
  backend.begin();
  backend.remove(R_A.key);
  backend.commit();

  // refsFrom should now be empty
  {
    auto out = backend.refsFrom(R_B.key, "deps");
    REQUIRE(out.empty());
  }

  // And fetch(R-B) should show deps: [] too
  {
    auto got = backend.fetch(R_B.key);
    REQUIRE(got.has_value());
    REQUIRE(got->fields.count("deps") == 1);
    CHECK(got->fields.at("deps").asArray().empty());
  }
}

TEST_CASE("SQLiteDataBackend: single ref must point to existing target (FK enforced on insert)", "[SQLite][FK][insert]")
{
  auto reg = build_registry_complex(); // has requirement.meta.owner -> user
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  // Seed only project (no users created on purpose)
  backend.begin();
  auto pFk = make_project2("FKProj", "1.0");
  backend.upsert(pFk);
  backend.commit();

  // Try to insert requirement with owner = U-404 (does not exist) -> expect FK failure
  auto bad = make_requirement_complex("Needs valid owner", "high", true, pFk.key.id.value(),
                                      /*owner*/ "U-404", /*created_by*/ "", /*severity*/ "high",
                                      /*checklist*/ {{"row", false, 1}}, /*deps*/ {});
  backend.begin();
  REQUIRE_THROWS(backend.upsert(bad)); // FK should reject
  backend.rollback();

  // Row must not be present
  CHECK_FALSE(backend.exists(bad.key));
}

TEST_CASE("SQLiteDataBackend: childrenOf returns empty for non-parent profiles", "[SQLite][childrenOf][negative]")
{
  auto reg = build_registry(); // project has children: requirement, attachment
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  // Seed a project, one requirement, one attachment
  backend.begin();
  auto pCh = make_project("ChildrenProj", "1.0");
  backend.upsert(pCh);

  ObjectData specs;
  specs["manufacturer"] = std::string("Acme");
  specs["warranty_years"] = 1;

  auto rCh1 = make_requirement("Req1", "low", true, pCh.key.id.value(),
                               specs, /*tags*/ ArrayData{}, /*refs*/ ArrayData{});
  auto aCh1 = make_attachment("file.txt", "txt", "/tmp/file.txt", pCh.key.id.value());
  backend.upsert(rCh1);
  backend.upsert(aCh1);
  backend.commit();

  // Sanity: project has both children
  {
    auto reqKids = backend.childrenOf(pCh.key, "requirement");
    auto attKids = backend.childrenOf(pCh.key, "attachment");
    REQUIRE(reqKids.size() == 1);
    REQUIRE(attKids.size() == 1);
  }

  // Negative 1: attachments do not own children -> empty
  {
    auto none = backend.childrenOf(aCh1.key, "requirement");
    CHECK(none.empty());
  }

  // Negative 2: requirements do not own attachments -> empty
  {
    auto none = backend.childrenOf(rCh1.key, "attachment");
    CHECK(none.empty());
  }
}

TEST_CASE("SQLiteDataBackend: UTF-8 round-trip across label, string, object subfields, and array<string>", "[SQLite][utf8]")
{
  auto reg = build_registry(); // has project/requirement/attachment with strings, objects, arrays
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  // Some spicy UTF-8 payloads
  const std::string proj_name = "🚀 Proyéctø — 東京";
  const std::string req_title = "Bråké 🔧 — 编码";
  const std::string manuf = "Mañana 工厂";
  const std::string tag1 = "α";
  const std::string tag2 = "β";
  const std::string tag3 = "猫";
  const std::string att_file = "图纸📄.pdf";
  const std::string att_path = "/路径/文件.pdf";

  // Insert project with UTF-8 label/field
  backend.begin();
  auto P = make_project(proj_name, "1.0"); // label = name in builder
  backend.upsert(P);

  // Attachment under project with UTF-8 filename/path
  ArrayData attTags; // optional array<string> on attachment
  attTags.emplace_back(tag3);
  auto A = make_attachment(att_file, "pdf", att_path, P.key.id.value(), attTags);
  backend.upsert(A);

  // Requirement with UTF-8 title, object subfield, and array<string> tags
  ObjectData specs;
  specs["manufacturer"] = manuf; // object subfield (string)
  specs["warranty_years"] = 2;

  ArrayData tags; // array<string>
  tags.emplace_back(tag1);
  tags.emplace_back(tag2);
  tags.emplace_back(tag3);

  auto R = make_requirement(req_title, "high", true, P.key.id.value(),
                            specs, tags, /*refs*/ ArrayData{});
  backend.upsert(R);
  backend.commit();

  // ---- Fetch and verify round-trip on each node ----
  // Project
  {
    auto p = backend.fetch(P.key);
    REQUIRE(p.has_value());
    REQUIRE(p->fields.count("project_name") == 1);
    CHECK(p->fields.at("project_name").asString() == proj_name);
  }

  // Attachment
  {
    auto a = backend.fetch(A.key);
    REQUIRE(a.has_value());
    CHECK(a->fields.at("filename").asString() == att_file);
    CHECK(a->fields.at("path").asString() == att_path);

    REQUIRE(a->fields.at("tags").isArray());
    auto atags = a->fields.at("tags").asArray();
    REQUIRE(atags.size() == 1);
    CHECK(atags[0].asString() == tag3);
  }

  // Requirement
  {
    auto r = backend.fetch(R.key);
    REQUIRE(r.has_value());
    CHECK(r->fields.at("title").asString() == req_title);

    // object subfield
    REQUIRE(r->fields.at("specs").isObject());
    auto s = r->fields.at("specs").asObject();
    REQUIRE(s.count("manufacturer") == 1);
    CHECK(s.at("manufacturer").asString() == manuf);

    // array<string> tags
    REQUIRE(r->fields.at("tags").isArray());
    auto t = r->fields.at("tags").asArray();
    REQUIRE(t.size() == 3);
    CHECK(t[0].asString() == tag1);
    CHECK(t[1].asString() == tag2);
    CHECK(t[2].asString() == tag3);
  }

  // Sanity: childrenOf still works
  {
    auto reqKids = backend.childrenOf(P.key, "requirement");
    auto attKids = backend.childrenOf(P.key, "attachment");
    REQUIRE(reqKids.size() == 1);
    REQUIRE(attKids.size() == 1);
    CHECK(reqKids[0].id == R.key.id.value());
    CHECK(attKids[0].id == A.key.id.value());
  }
}

TEST_CASE("SQLiteDataBackend: DNF across types — object subfields + arrays + booleans + unicode LIKE", "[SQLite][query][dnf-mixed]")
{
  auto reg = build_registry_complex(); // has requirement.meta{owner,created_by}, tags (via simple schema earlier), and boolean 'active'
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  // Seed project + users
  backend.begin();
  auto pQm = make_project2("QueryMix", "1.0");
  auto uA = make_user("alice", "admin");
  auto uB = make_user("bob", "qa");
  backend.upsert(pQm);
  backend.upsert(uA);
  backend.upsert(uB);
  backend.commit();

  // Build some unicode strings (plain UTF-8 literals; source file must be UTF-8)
  const std::string title1 = "γ-latency check"; // Greek gamma
  const std::string title2 = "delta dash";
  const std::string title3 = "gamma graphs";
  const std::string catTag = "猫";

  // R1: owner=U-A, created_by=U-B, active=true, tags include "猫"

  std::vector<std::tuple<std::string, bool, int>> cl = {{"c1", false, 1}};
  auto r1 = make_requirement_complex(title1, "high", true, pQm.key.id.value(),
                                     /*owner*/ uA.key.id.value(), /*created_by*/ uB.key.id.value(), /*severity*/ "high",
                                     cl, /*deps*/ {});
  // add a tags array by patching in place (complex schema didn’t define 'tags'; we’ll piggyback on checklist-only)
  // Instead, encode the "猫" requirement via checklist text (for LIKE) and still test arrays via deps/obj if needed.
  // To truly test array CONTAINS, use deps as a ref array and set a dummy self ref we later ignore in filters.
  backend.begin();
  backend.upsert(r1);
  backend.commit();

  // add a small array-of-objects row carrying the unicode to be matched via CONTAINS on title later anyway
  // (Title already has unicode; we’ll rely on title CONTAINS below.)

  // R2: no owner/created_by, active=false

  auto r2 = make_requirement_complex(title2, "low", false, pQm.key.id.value(),
                                     /*owner*/ "", /*created_by*/ "", /*severity*/ "low",
                                     /*checklist*/ {}, /*deps*/ {});
  backend.begin();
  backend.upsert(r2);
  backend.commit();

  // R3: owner=U-A, created_by=NULL, active=true

  auto r3 = make_requirement_complex(title3, "medium", true, pQm.key.id.value(),
                                     /*owner*/ uA.key.id.value(), /*created_by*/ "", /*severity*/ "medium",
                                     /*checklist*/ {}, /*deps*/ {});
  backend.begin();
  backend.upsert(r3);
  backend.commit();

  // --- A) (meta.owner == U-A AND title CONTAINS 'γ') OR (title PREFIX 'delta' AND active == false)
  // Expect: R-M1 (first AND group) and R-M2 (second AND group)
  {
    QueryDNF q;
    q.profile = "requirement";
    q.any_of = {
        {FieldFilter{"meta.owner", FieldOp::EQ, std::string(uA.key.id.value())},
         FieldFilter{"title", FieldOp::CONTAINS, std::string("γ")}},
        {FieldFilter{"title", FieldOp::PREFIX, std::string("delta")},
         FieldFilter{"active", FieldOp::EQ, false}}};
    // No order required; just verify membership
    auto rows = backend.find(q);
    REQUIRE(rows.size() == 2);
    bool hasM1 = false, hasM2 = false;
    for (auto &r : rows)
    {
      if (r.key.id == r1.key.id)
        hasM1 = true;
      if (r.key.id == r2.key.id)
        hasM2 = true;
    }
    CHECK(hasM1);
    CHECK(hasM2);
  }

  // --- B) meta.created_by IS NULL AND meta.owner NOT NULL -> only R-M3
  {
    QueryDNF q;
    q.profile = "requirement";
    q.any_of = {{FieldFilter{"meta.created_by", FieldOp::IS_NULL, FieldValue{}},
                 FieldFilter{"meta.owner", FieldOp::NOT_NULL, FieldValue{}}}};
    auto rows = backend.find(q);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].key.id == r3.key.id);
  }

  // --- C) active == true AND title CONTAINS 'gamma' (ASCII substring) -> R-M3 only
  {
    QueryDNF q;
    q.profile = "requirement";
    q.any_of = {{FieldFilter{"active", FieldOp::EQ, true},
                 FieldFilter{"title", FieldOp::CONTAINS, std::string("gamma")}}};
    auto rows = backend.find(q);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].key.id == r3.key.id);
  }
}

TEST_CASE("SQLiteDataBackend: query edge cases — empty DNF, empty IN, and unknown fields", "[SQLite][query][edges]")
{
  auto reg = build_registry_complex();
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  // Seed minimal data
  backend.begin();
  auto pEc = make_project2("EdgeCases", "1.0");
  backend.upsert(pEc);
  // Two simple requirements with different titles/priorities/actives
  auto R1 = make_requirement_complex("Alpha spec", "low", true, pEc.key.id.value(),
                                     /*owner*/ "", /*created_by*/ "", /*severity*/ "low",
                                     /*checklist*/ {}, /*deps*/ {});
  auto R2 = make_requirement_complex("Beta spec", "high", false, pEc.key.id.value(),
                                     /*owner*/ "", /*created_by*/ "", /*severity*/ "high",
                                     /*checklist*/ {}, /*deps*/ {});
  backend.upsert(R1);
  backend.upsert(R2);
  backend.commit();

  // A) Empty any_of -> match all
  {
    QueryDNF q;
    q.profile = "requirement";
    // q.any_of is empty
    auto rows = backend.find(q);
    REQUIRE(rows.size() == 2);
  }

  // B) any_of contains an empty group and a real group -> empty group ignored
  // Only match titles with prefix "Alpha"
  {
    QueryDNF q;
    q.profile = "requirement";
    q.any_of = {
        {}, // empty group should be ignored
        {FieldFilter{"title", FieldOp::PREFIX, std::string("Alpha")}}};
    auto rows = backend.find(q);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].key.id == R1.key.id);
  }

  // C) IN with empty set -> matches nothing
  {
    ArrayData emptySet; // no values
    QueryDNF q;
    q.profile = "requirement";
    q.any_of = {{FieldFilter{"priority", FieldOp::IN, FieldValue(emptySet)}}};
    auto rows = backend.find(q);
    CHECK(rows.empty());
  }

  // D) NOT_IN with empty set -> matches everything
  {
    ArrayData emptySet;
    QueryDNF q;
    q.profile = "requirement";
    q.any_of = {{FieldFilter{"priority", FieldOp::NOT_IN, FieldValue(emptySet)}}};
    auto rows = backend.find(q);
    REQUIRE(rows.size() == 2);
  }

  // E) Unknown field -> never matches (defensive 1=0)
  {
    QueryDNF q;
    q.profile = "requirement";
    q.any_of = {{FieldFilter{"does_not_exist", FieldOp::EQ, std::string("x")}}};
    auto rows = backend.find(q);
    CHECK(rows.empty());
  }
}

TEST_CASE("SQLiteDataBackend: query safety on invalid/mismatched operator-field combos", "[SQLite][query][safety]")
{
  auto reg = build_registry(); // requirement has title (string), priority (enum), active (bool), specs.warranty_years (int), tags (array<string>), references (array<ref>)
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  // Seed project, attachments, and a requirement with various fields populated
  backend.begin();
  auto pQs = make_project2("QSProj", "1.0");
  backend.upsert(pQs);

  auto A1 = make_attachment("Spec.pdf", "pdf", "/p/spec.pdf", pQs.key.id.value());
  auto A2 = make_attachment("Img.png", "png", "/p/img.png", pQs.key.id.value());
  backend.upsert(A1);
  backend.upsert(A2);

  // R has: title, priority, active, specs(warranty_years=5), tags["alpha","beta"], references["A-1","A-2"]
  ObjectData specs;
  specs["manufacturer"] = std::string("Acme");
  specs["warranty_years"] = 5;
  ArrayData tags;
  tags.emplace_back(std::string("alpha"));
  tags.emplace_back(std::string("beta"));
  ArrayData refs;
  refs.emplace_back(std::string(A1.key.id.value()));
  refs.emplace_back(std::string(A2.key.id.value()));
  auto rQs = make_requirement("Title QS", "high", true, pQs.key.id.value(), specs, tags, refs);
  backend.upsert(rQs);
  backend.commit();

  // A) IS_NULL on an array field (tags) -> our builder emits 1=0 for non-CONTAINS on arrays
  {
    QueryDNF q;
    q.profile = "requirement";
    q.any_of = {{FieldFilter{"tags", FieldOp::IS_NULL, FieldValue{}}}};
    auto rows = backend.find(q);
    CHECK(rows.empty()); // defensive path, no matches
  }

  // B) CONTAINS on an array<reference> but with a non-string value (integer) -> should not crash, return empty
  {
    QueryDNF q;
    q.profile = "requirement";
    q.any_of = {{FieldFilter{"references", FieldOp::CONTAINS, FieldValue(123)}}};
    auto rows = backend.find(q);
    CHECK(rows.empty());
  }

  // C) PREFIX on a boolean column ('active') -> results should be empty (no crash)
  {
    QueryDNF q;
    q.profile = "requirement";
    q.any_of = {{FieldFilter{"active", FieldOp::PREFIX, std::string("t")}}};
    auto rows = backend.find(q);
    CHECK(rows.empty());
  }

  // D) GT on a boolean column ('active' > 1) -> nonsensical but should not crash; usually empty
  {
    QueryDNF q;
    q.profile = "requirement";
    q.any_of = {{FieldFilter{"active", FieldOp::GT, FieldValue(1)}}};
    auto rows = backend.find(q);
    CHECK(rows.empty());
  }

  // E) IN provided with a scalar instead of an array -> builder emits 1=0; empty result
  {
    QueryDNF q;
    q.profile = "requirement";
    q.any_of = {{FieldFilter{"title", FieldOp::IN, FieldValue(std::string("Title QS"))}}};
    auto rows = backend.find(q);
    CHECK(rows.empty());
  }
}

TEST_CASE("SQLiteDataBackend: regression — object with nested array not flattened as SQL column", "[SQLite][flattening][regression]")
{
  // Minimal schema that reproduces the original crash pattern
  SchemaRegistry reg;
  std::unordered_map<std::string, std::string> files = {
      {"sys_requirement.yaml", R"(
name: sys_requirement
kind: node
description: Sys requirement
fields:
  title: { type: string, required: true }
)"},
      {"sw_requirement.yaml", R"(
name: sw_requirement
kind: node
description: SW requirement
fields:
  title: { type: string, required: true }
  sys_requirement_ref:
    type: object
    fields:
      references:
        type: array
        items: { type: reference, target: sys_requirement }
      identical:
        type: boolean
)"}};

  auto nodes = YamlSchemaLoader::loadFromSources(files);
  YamlSchemaDecoder::decodeProfiles(nodes, reg);

  SQLiteDataBackend backend(":memory:", /*recreate*/ true);

  // If collectMainColumns() still flattened nested array, init() would succeed,
  // but a later query (find/list) would crash on nonexistent column.
  REQUIRE_NOTHROW(backend.init(reg));

  // Seed minimal data
  backend.begin();
  NodeSnapshot s1;
  s1.key.profile = "sys_requirement";
  s1.fields["title"] = std::string("System Req");
  backend.upsert(s1);

  NodeSnapshot swr;
  swr.key.profile = "sw_requirement";
  swr.fields["title"] = std::string("Software Req");
  ObjectData obj;
  obj["identical"] = true;
  ArrayData refs;
  refs.emplace_back(s1.key.id.value());
  obj["references"] = refs;
  swr.fields["sys_requirement_ref"] = obj;
  backend.upsert(swr);
  backend.commit();

  // Try a simple SELECT / find query — this used to crash with "no such column: t.sys_requirement_ref_references"
  QueryDNF q;
  q.profile = "sw_requirement";
  q.any_of = {{}};
  REQUIRE_NOTHROW(backend.find(q));
  auto rows = backend.find(q);
  REQUIRE(rows.size() == 1);
  CHECK(rows[0].key.id == swr.key.id.value());

  // Ensure array-of-ref field was stored and can be fetched correctly
  auto fetched = backend.fetch(swr.key);
  REQUIRE(fetched.has_value());
  REQUIRE(fetched->fields.count("sys_requirement_ref") == 1);
  auto refObj = fetched->fields.at("sys_requirement_ref").asObject();
  REQUIRE(refObj.count("references") == 1);
  auto arr = refObj.at("references").asArray();
  REQUIRE(arr.size() == 1);
  CHECK(arr[0].asString() == s1.key.id.value());
}

TEST_CASE("SQLiteDataBackend: refsTo/refsFrom for nested array<reference>", "[SQLite][refs][nested]")
{
  // ---------- Schema ----------
  SchemaRegistry reg;
  std::unordered_map<std::string, std::string> files = {
      {"sys_requirement.yaml", R"(
name: sys_requirement
kind: node
description: SYS requirement
fields:
  title: { type: string, required: true }
)"},
      {"sw_requirement.yaml", R"(
name: sw_requirement
kind: node
description: SW requirement
fields:
  title: { type: string, required: true }
  sys_requirement_ref:
    type: object
    fields:
      references:
        type: array
        items: { type: reference, target: sys_requirement }
      identical: { type: boolean }
)"}};

  auto nodes = YamlSchemaLoader::loadFromSources(files);
  YamlSchemaDecoder::decodeProfiles(nodes, reg);

  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  // ---------- Data ----------
  backend.begin();

  // One system requirement
  NodeSnapshot sys;
  sys.key.profile = "sys_requirement";
  sys.fields["title"] = std::string("System Req A");
  backend.upsert(sys);

  // One software requirement referencing it
  NodeSnapshot sw;
  sw.key.profile = "sw_requirement";
  sw.fields["title"] = std::string("Software Req X");
  ObjectData refObj;
  refObj["identical"] = false;
  ArrayData refs;
  refs.emplace_back(sys.key.id.value());
  refObj["references"] = refs;
  sw.fields["sys_requirement_ref"] = refObj;
  backend.upsert(sw);

  backend.commit();

  // ---------- Assertions ----------
  auto fwd = backend.refsFrom(sw.key, "sys_requirement_ref.references");
  REQUIRE(fwd.size() == 1);
  CHECK(fwd[0].profile == "sys_requirement");
  CHECK(fwd[0].id == sys.key.id.value());

  auto back = backend.refsTo(sys.key);
  REQUIRE(back.size() == 1);
  CHECK(back[0].profile == "sw_requirement");
  CHECK(back[0].id == sw.key.id.value());
}

TEST_CASE("SQLiteDataBackend: refsTo/refsFrom for object.reference", "[SQLite][refs][object-ref]")
{
  // ---------- Schema ----------
  SchemaRegistry reg;
  std::unordered_map<std::string, std::string> files = {
      {"user.yaml", R"(
name: user
kind: node
description: User node
fields:
  username: { type: string, required: true }
)"},
      {"requirement.yaml", R"(
name: requirement
kind: node
description: Requirement with single reference in object
fields:
  title: { type: string, required: true }
  meta:
    type: object
    fields:
      owner: { type: reference, target: user }
      severity: { type: string }
)"}};

  auto nodes = YamlSchemaLoader::loadFromSources(files);
  YamlSchemaDecoder::decodeProfiles(nodes, reg);

  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  // ---------- Data ----------
  backend.begin();
  // create user
  NodeSnapshot u1;
  u1.key.profile = "user";
  u1.fields["username"] = std::string("alice");
  backend.upsert(u1);

  // create requirement with meta.owner = u1
  NodeSnapshot r1;
  r1.key.profile = "requirement";
  r1.fields["title"] = std::string("REQ-1");
  ObjectData meta;
  meta["owner"] = std::string(u1.key.id.value());
  meta["severity"] = std::string("high");
  r1.fields["meta"] = meta;
  backend.upsert(r1);
  backend.commit();

  // ---------- Assertions ----------
  // 1) refsFrom(requirement, "meta.owner") → user
  auto fwd = backend.refsFrom(r1.key, "meta.owner");
  REQUIRE(fwd.size() == 1);
  CHECK(fwd[0].profile == "user");
  CHECK(fwd[0].id == u1.key.id.value());

  // 2) refsTo(user) → requirement
  auto back = backend.refsTo(u1.key);
  REQUIRE(back.size() == 1);
  CHECK(back[0].profile == "requirement");
  CHECK(back[0].id == r1.key.id.value());

  // 3) Delete user → FK ON DELETE SET NULL should nullify meta.owner
  backend.begin();
  backend.remove(u1.key);
  backend.commit();

  auto got = backend.fetch(r1.key);
  REQUIRE(got.has_value());
  REQUIRE(got->fields.count("meta") == 1);
  auto m = got->fields.at("meta").asObject();
  CHECK(m.find("owner") == m.end()); // cleared
  CHECK(m.at("severity").asString() == "high");
}

TEST_CASE("SQLiteDataBackend: cascading delete cleans up ref arrays and object-refs", "[SQLite][refs][cascade-cleanup]")
{
  // ---------- Schema ----------
  SchemaRegistry reg;
  std::unordered_map<std::string, std::string> files = {
      {"user.yaml", R"(
name: user
kind: node
description: User node
fields:
  username: { type: string, required: true }
)"},
      {"doc.yaml", R"(
name: doc
kind: node
description: Documentation node
fields:
  title: { type: string, required: true }
)"},
      {"requirement.yaml", R"(
name: requirement
kind: node
description: Requirement referencing both user and docs
fields:
  title: { type: string, required: true }

  meta:
    type: object
    fields:
      owner: { type: reference, target: user }
      severity: { type: string }

  attachments:
    type: array
    items: { type: reference, target: doc }
)"}};

  auto nodes = YamlSchemaLoader::loadFromSources(files);
  YamlSchemaDecoder::decodeProfiles(nodes, reg);

  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  // ---------- Seed ----------
  backend.begin();

  // targets
  NodeSnapshot user;
  user.key.profile = "user";
  user.fields["username"] = std::string("alice");
  backend.upsert(user);

  NodeSnapshot d1;
  d1.key.profile = "doc";
  d1.fields["title"] = std::string("Doc A");
  backend.upsert(d1);

  NodeSnapshot d2;
  d2.key.profile = "doc";
  d2.fields["title"] = std::string("Doc B");
  backend.upsert(d2);

  // referencer (requirement)
  NodeSnapshot req;
  req.key.profile = "requirement";
  req.fields["title"] = std::string("REQ-1");

  ObjectData meta;
  meta["owner"] = std::string(user.key.id.value());
  meta["severity"] = std::string("medium");
  req.fields["meta"] = meta;

  ArrayData attaches;
  attaches.emplace_back(std::string(d1.key.id.value()));
  attaches.emplace_back(std::string(d2.key.id.value()));
  req.fields["attachments"] = attaches;

  backend.upsert(req);
  backend.commit();

  // ---------- Sanity ----------
  auto fwdDocs = backend.refsFrom(req.key, "attachments");
  REQUIRE(fwdDocs.size() == 2);

  auto fwdOwner = backend.refsFrom(req.key, "meta.owner");
  REQUIRE(fwdOwner.size() == 1);
  CHECK(fwdOwner[0].profile == "user");

  // ---------- Act: delete the referencer node ----------
  backend.begin();
  backend.remove(req.key);
  backend.commit();

  // 1) Side-table for attachments should be empty
  {
    sqlite3 *raw = backend.rawHandle(); // or backend.db_ if exposed internally
    sqlite3_stmt *stmt = nullptr;

    // Use the new '$' separator (matches new SQLite exporter convention)
    const char *sql = "SELECT COUNT(*) FROM requirement$attachments;";

    REQUIRE(sqlite3_prepare_v2(raw, sql, -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    int count = sqlite3_column_int(stmt, 0);
    CHECK(count == 0);
    sqlite3_finalize(stmt);
  }

  // 2) Main targets (user, docs) remain unaffected
  CHECK(backend.exists(user.key));
  CHECK(backend.exists(d1.key));
  CHECK(backend.exists(d2.key));
}

TEST_CASE("SQLiteDataBackend: QueryDNF — object.array CONTAINS", "[SQLite][query][object-array]")
{
  // ---------- Schema ----------
  SchemaRegistry reg;
  std::unordered_map<std::string, std::string> files = {
      {"requirement.yaml", R"(
name: requirement
kind: node
description: Target requirement node
fields:
  title: { type: string, required: true }
)"},
      {"sw_requirement.yaml", R"(
name: sw_requirement
kind: node
parent: project
description: Software requirement referencing system requirements
fields:
  title: { type: string, required: true }
  priority: { type: string }
  sys_requirement_ref:
    type: object
    fields:
      references:
        type: array
        items: { type: reference, target: requirement }
)"},
      {"project.yaml", R"(
name: project
kind: root
description: Parent project node
fields:
  description: { type: string }
children:
  sw_requirements: { node: sw_requirement }
)"}}; // end files map

  auto nodes = YamlSchemaLoader::loadFromSources(files);
  YamlSchemaDecoder::decodeProfiles(nodes, reg);

  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  // ---------- Seed ----------
  backend.begin();

  // Project
  NodeSnapshot project;
  project.key = NodeKey{"project", "P-1"};
  project.fields["description"] = std::string("Main Project");
  backend.upsert(project);

  // Target requirements
  NodeSnapshot reqX;
  reqX.key = NodeKey{"requirement", "REQ-X"};
  reqX.fields["title"] = std::string("Brake latency <100ms");
  backend.upsert(reqX);

  NodeSnapshot reqY;
  reqY.key = NodeKey{"requirement", "REQ-Y"};
  reqY.fields["title"] = std::string("Thermal cutoff <80C");
  backend.upsert(reqY);

  // Software requirements (each referencing one target)
  NodeSnapshot sw1;
  sw1.key = NodeKey{"sw_requirement", "SW-1"};
  sw1.fields["title"] = std::string("CPU frequency control");
  sw1.fields["priority"] = std::string("medium");
  sw1.parent_id = std::string("P-1");
  {
    ArrayData refs;
    refs.emplace_back(std::string(reqX.key.id.value()));
    ObjectData sysref;
    sysref["references"] = refs;
    sw1.fields["sys_requirement_ref"] = sysref;
  }
  backend.upsert(sw1);

  NodeSnapshot sw2;
  sw2.key = NodeKey{"sw_requirement", "SW-2"};
  sw2.fields["title"] = std::string("Thermal safety monitor");
  sw2.fields["priority"] = std::string("low");
  sw2.parent_id = std::string("P-1");
  {
    ArrayData refs;
    refs.emplace_back(std::string(reqY.key.id.value()));
    ObjectData sysref;
    sysref["references"] = refs;
    sw2.fields["sys_requirement_ref"] = sysref;
  }
  backend.upsert(sw2);

  backend.commit();

  // ---------- Act: Query for object.array CONTAINS ----------
  QueryDNF q;
  q.profile = "sw_requirement";
  q.any_of = {{FieldFilter{"sys_requirement_ref.references", FieldOp::CONTAINS, FieldValue(std::string("REQ-X"))}}};

  auto matches = backend.find(q);

  // ---------- Assert ----------
  REQUIRE(matches.size() == 1);
  CHECK(matches[0].key.id == "SW-1");

  // Add second verification for REQ-Y
  QueryDNF q2;
  q2.profile = "sw_requirement";
  q2.any_of = {{FieldFilter{"sys_requirement_ref.references", FieldOp::CONTAINS, FieldValue(std::string("REQ-Y"))}}};

  auto matches2 = backend.find(q2);
  REQUIRE(matches2.size() == 1);
  CHECK(matches2[0].key.id == "SW-2");
}

TEST_CASE("SQLiteDataBackend: fetch restores object.array<reference> correctly", "[SQLite][fetch][object-array-ref]")
{
  // ---------- Schema ----------
  SchemaRegistry reg;
  std::unordered_map<std::string, std::string> files = {
      {"sys_requirement.yaml", R"(
name: sys_requirement
kind: node
description: System-level requirement
fields:
  title: { type: string, required: true }
)"},
      {"sw_requirement.yaml", R"(
name: sw_requirement
kind: node
parent: project
description: Software requirement referencing system requirements
fields:
  title: { type: string, required: true }
  sys_requirement_ref:
    type: object
    fields:
      references:
        type: array
        items: { type: reference, target: sys_requirement }
      identical: { type: boolean }
)"},
      {"project.yaml", R"(
name: project
kind: root
description: Parent project node
fields:
  description: { type: string }
children:
  sw_requirements: { node: sw_requirement }
)"}}; // end files map

  auto nodes = YamlSchemaLoader::loadFromSources(files);
  YamlSchemaDecoder::decodeProfiles(nodes, reg);

  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  // ---------- Seed ----------
  backend.begin();

  // Project
  NodeSnapshot project;
  project.key = NodeKey{"project", "P-OBJARR"};
  project.fields["description"] = std::string("Object-array fetch test");
  backend.upsert(project);

  // Two target system requirements
  NodeSnapshot reqA;
  reqA.key = NodeKey{"sys_requirement", "REQ-A"};
  reqA.fields["title"] = std::string("System requirement A");
  backend.upsert(reqA);

  NodeSnapshot reqB;
  reqB.key = NodeKey{"sys_requirement", "REQ-B"};
  reqB.fields["title"] = std::string("System requirement B");
  backend.upsert(reqB);

  // Software requirement referencing both A and B
  NodeSnapshot sw;
  sw.key = NodeKey{"sw_requirement", "SW-FETCH"};
  sw.parent_id = std::string("P-OBJARR");
  sw.fields["title"] = std::string("Fetch validation SW");

  ObjectData sysRef;
  sysRef["identical"] = false;
  ArrayData refs;
  refs.emplace_back(std::string(reqA.key.id.value()));
  refs.emplace_back(std::string(reqB.key.id.value()));
  sysRef["references"] = refs;
  sw.fields["sys_requirement_ref"] = sysRef;

  backend.upsert(sw);
  backend.commit();

  {
    sqlite3 *raw = backend.rawHandle();
    sqlite3_stmt *stmt = nullptr;

    // Updated column name to new `$` separator
    const char *sql = "SELECT sys_requirement_ref$identical FROM sw_requirement;";

    REQUIRE(sqlite3_prepare_v2(raw, sql, -1, &stmt, nullptr) == SQLITE_OK);
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
      int val = sqlite3_column_type(stmt, 0) == SQLITE_NULL ? -1 : sqlite3_column_int(stmt, 0);
      std::cout << "\nDB identical value: " << val << "\n";
    }
    sqlite3_finalize(stmt);
  }

  // ---------- Act ----------
  auto fetched = backend.fetch(sw.key);
  REQUIRE(fetched.has_value());

  // ---------- Assert object structure ----------
  REQUIRE(fetched->fields.count("sys_requirement_ref") == 1);
  const auto &obj = fetched->fields.at("sys_requirement_ref").asObject();

  {
    sqlite3 *raw = backend.rawHandle();
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT name, sql FROM sqlite_master WHERE type='table';";
    if (sqlite3_prepare_v2(raw, sql, -1, &stmt, nullptr) == SQLITE_OK)
    {
      std::cout << "\n=== SQLite Tables ===\n";
      while (sqlite3_step(stmt) == SQLITE_ROW)
      {
        const char *name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        const char *ddl = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        std::cout << (name ? name : "(null)") << ": "
                  << (ddl ? ddl : "(null)") << "\n";
      }
      std::cout << "=====================\n";
      sqlite3_finalize(stmt);
    }
    else
    {
      std::cerr << "Could not query sqlite_master!\n";
    }
  }

  // boolean field restored
  REQUIRE(obj.count("identical") == 1);
  CHECK(obj.at("identical").asBoolean() == false);

  // array-of-reference restored
  REQUIRE(obj.count("references") == 1);
  const auto &arr = obj.at("references").asArray();
  REQUIRE(arr.size() == 2);
  CHECK(arr[0].asString() == reqA.key.id.value());
  CHECK(arr[1].asString() == reqB.key.id.value());

  // ---------- Cross-check via find() ----------
  QueryDNF q;
  q.profile = "sw_requirement";
  q.any_of = {{FieldFilter{"title", FieldOp::EQ, std::string("Fetch validation SW")}}};
  auto found = backend.find(q);
  REQUIRE(found.size() == 1);
  CHECK(found[0].key.id == sw.key.id.value());

  // fetch() and find() are consistent
  auto refetched = backend.fetch(found[0].key);
  REQUIRE(refetched.has_value());
  auto obj2 = refetched->fields.at("sys_requirement_ref").asObject();
  auto arr2 = obj2.at("references").asArray();
  REQUIRE(arr2.size() == 2);
  CHECK(arr2[0].asString() == reqA.key.id.value());
  CHECK(arr2[1].asString() == reqB.key.id.value());
}

TEST_CASE("SQLiteDataBackend: nested object-within-object roundtrip", "[SQLite][object][nested]")
{
  // ---------- Schema ----------
  SchemaRegistry reg;
  std::unordered_map<std::string, std::string> files = {
      {"project.yaml", R"(
name: project
kind: root
description: Root project node
fields:
  description: { type: string }
children:
  configs: { node: config }
)"},
      {"config.yaml", R"(
name: config
kind: node
description: Node containing nested objects
fields:
  name: { type: string, required: true }
  settings:
    type: object
    fields:
      network:
        type: object
        fields:
          ip: { type: string }
          port: { type: integer }
      limits:
        type: object
        fields:
          cpu: { type: integer, default: 50 }
          mem: { type: integer, default: 256 }
)"}};

  auto nodes = YamlSchemaLoader::loadFromSources(files);
  YamlSchemaDecoder::decodeProfiles(nodes, reg);
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  // ---------- Seed ----------
  backend.begin();

  // Parent root project
  NodeSnapshot project;
  project.key = NodeKey{"project", "PRJ-OBJ"};
  project.fields["description"] = std::string("Project for nested test");
  backend.upsert(project);

  // Child config under project
  NodeSnapshot cfg;
  cfg.key.profile = "config";
  cfg.parent_id = project.key.id;
  cfg.fields["name"] = std::string("MainCfg");

  ObjectData settings;
  ObjectData net;
  net["ip"] = std::string("192.168.1.10");
  net["port"] = 8080;
  settings["network"] = net;

  ObjectData limits;
  limits["cpu"] = 80;
  limits["mem"] = 512;
  settings["limits"] = limits;
  cfg.fields["settings"] = settings;

  backend.upsert(cfg);
  backend.commit();

  // ---------- Fetch ----------
  auto got = backend.fetch(cfg.key);
  REQUIRE(got.has_value());
  const auto &s = got->fields.at("settings").asObject();
  const auto &netOut = s.at("network").asObject();
  CHECK(netOut.at("ip").asString() == "192.168.1.10");
  CHECK(netOut.at("port").asInteger() == 8080);

  const auto &limOut = s.at("limits").asObject();
  CHECK(limOut.at("cpu").asInteger() == 80);
  CHECK(limOut.at("mem").asInteger() == 512);

  // ---------- Patch partial (explicit null) ----------
  ObjectData patchSettings;
  ObjectData patchNet;
  patchNet["ip"] = FieldValue{}; // null clears ip
  patchSettings["network"] = patchNet;
  NodeSnapshot patch;
  patch.key = cfg.key;
  patch.fields["settings"] = patchSettings;

  backend.begin();
  backend.upsert(patch);
  backend.commit();

  auto after = backend.fetch(cfg.key);
  REQUIRE(after.has_value());
  const auto &s2 = after->fields.at("settings").asObject();
  const auto &net2 = s2.at("network").asObject();
  CHECK(net2.find("ip") == net2.end());       // cleared
  CHECK(net2.at("port").asInteger() == 8080); // preserved
}

TEST_CASE("SQLiteDataBackend: QueryDNF joins against side-tables (arrays, nested arrays)", "[SQLite][query][side-tables]")
{
  // ---------- Schema ----------
  SchemaRegistry reg;
  std::unordered_map<std::string, std::string> files = {
      {"project.yaml", R"(
name: project
kind: root
description: Root node
fields:
  title: { type: string }
children:
  tasks: { node: task }
)"},
      {"task.yaml", R"(
name: task
kind: node
description: Node with nested arrays
fields:
  title: { type: string }
  meta:
    type: object
    fields:
      tags:
        type: array
        items: { type: string }
      subitems:
        type: array
        items:
          type: object
          fields:
            name: { type: string }
            weight: { type: integer }
)"}};

  auto nodes = YamlSchemaLoader::loadFromSources(files);
  YamlSchemaDecoder::decodeProfiles(nodes, reg);
  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  // ---------- Seed ----------
  backend.begin();

  // Parent project
  NodeSnapshot prj;
  prj.key = NodeKey{"project", "P-TASKS"};
  prj.fields["title"] = std::string("Task Parent");
  backend.upsert(prj);

  // Child tasks
  NodeSnapshot t1;
  t1.key.profile = "task";
  t1.parent_id = prj.key.id;
  t1.fields["title"] = std::string("Alpha");
  ObjectData meta1;
  ArrayData tags1 = {std::string("ui"), std::string("frontend")};
  meta1["tags"] = tags1;
  ArrayData sub1;
  sub1.emplace_back(ObjectData{{"name", std::string("draw")}, {"weight", 3}});
  sub1.emplace_back(ObjectData{{"name", std::string("paint")}, {"weight", 2}});
  meta1["subitems"] = sub1;
  t1.fields["meta"] = meta1;
  backend.upsert(t1);

  NodeSnapshot t2;
  t2.key.profile = "task";
  t2.parent_id = prj.key.id;
  t2.fields["title"] = std::string("Beta");
  ObjectData meta2;
  ArrayData tags2 = {std::string("backend")};
  meta2["tags"] = tags2;
  ArrayData sub2;
  sub2.emplace_back(ObjectData{{"name", std::string("compile")}, {"weight", 1}});
  meta2["subitems"] = sub2;
  t2.fields["meta"] = meta2;
  backend.upsert(t2);
  backend.commit();

  // ---------- Queries ----------

  // CONTAINS in nested array (meta.tags[])
  {
    QueryDNF q;
    q.profile = "task";
    q.any_of = {{FieldFilter{"meta.tags", FieldOp::CONTAINS, std::string("frontend")}}};
    auto rows = backend.find(q);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].key.id == t1.key.id.value());
  }

  // PREFIX match inside object-array (meta.subitems.name)
  {
    QueryDNF q;
    q.profile = "task";
    q.any_of = {{FieldFilter{"meta.subitems.name", FieldOp::PREFIX, std::string("pa")}}};
    auto rows = backend.find(q);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].key.id == t1.key.id.value());
  }

  // GT on object-array numeric field (meta.subitems.weight)
  {
    QueryDNF q;
    q.profile = "task";
    q.any_of = {{FieldFilter{"meta.subitems.weight", FieldOp::GT, FieldValue(2)}}};
    auto rows = backend.find(q);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].key.id == t1.key.id.value());
  }
}

TEST_CASE("SQLiteDataBackend: refsTo/refsFrom for array<object> and object.array<reference>", "[SQLite][refs][array-object-ref]")
{
  // ---------- Schema ----------
  SchemaRegistry reg;
  std::unordered_map<std::string, std::string> files = {
      {"project.yaml", R"(
name: project
kind: root
description: Root node for docs and reports
fields:
  description: { type: string }
children:
  docs:    { node: doc }
  reports: { node: report }
)"},
      {"doc.yaml", R"(
name: doc
kind: node
description: Document node
fields:
  title: { type: string, required: true }
)"},
      {"report.yaml", R"(
name: report
kind: node
description: Report node containing valid references
fields:
  title: { type: string, required: true }

  # array<object> with single references (legal)
  sections:
    type: array
    items:
      type: object
      fields:
        heading: { type: string }
        main_ref: { type: reference, target: doc }

  # separate object containing array<reference> (also legal)
  appendix:
    type: object
    fields:
      attachments:
        type: array
        items: { type: reference, target: doc }
)"}};

  auto nodes = YamlSchemaLoader::loadFromSources(files);
  YamlSchemaDecoder::decodeProfiles(nodes, reg);

  SQLiteDataBackend backend(":memory:", /*recreate*/ true);
  backend.init(reg);

  // ---------- Seed ----------
  backend.begin();

  NodeSnapshot project;
  project.key = NodeKey{"project", "P-REFS"};
  project.fields["description"] = std::string("Root for doc/report hierarchy");
  backend.upsert(project);

  NodeSnapshot d1;
  d1.key.profile = "doc";
  d1.fields["title"] = std::string("Doc A");
  d1.parent_id = project.key.id;
  backend.upsert(d1);

  NodeSnapshot d2;
  d2.key.profile = "doc";
  d2.fields["title"] = std::string("Doc B");
  d2.parent_id = project.key.id;
  backend.upsert(d2);

  NodeSnapshot d3;
  d3.key.profile = "doc";
  d3.fields["title"] = std::string("Doc C");
  d3.parent_id = project.key.id;
  backend.upsert(d3);

  // Report node
  NodeSnapshot r;
  r.key.profile = "report";
  r.fields["title"] = std::string("Weekly Report");
  r.parent_id = project.key.id;

  // ---- sections (array<object.reference>) ----
  ArrayData sections;
  {
    ObjectData s1;
    s1["heading"] = std::string("Intro");
    s1["main_ref"] = std::string(d1.key.id.value());
    sections.emplace_back(s1);
  }
  {
    ObjectData s2;
    s2["heading"] = std::string("Conclusion");
    s2["main_ref"] = std::string(d3.key.id.value());
    sections.emplace_back(s2);
  }
  r.fields["sections"] = sections;

  // ---- appendix (object.array<reference>) ----
  ObjectData appendix;
  ArrayData attach;
  attach.emplace_back(std::string(d2.key.id.value())); // reference to Doc B
  appendix["attachments"] = attach;
  r.fields["appendix"] = appendix;

  backend.upsert(r);
  backend.commit();

  // ---------- Assertions ----------

  // 1) refsFrom(report, "sections.main_ref") → Doc A + Doc C
  {
    auto fwd = backend.refsFrom(r.key, "sections.main_ref");
    REQUIRE(fwd.size() == 2);
    std::vector<std::string> ids;
    for (auto &k : fwd)
      ids.push_back(k.id.value_or(""));
    using namespace Catch::Matchers;
    CHECK_THAT(ids, VectorContains(d1.key.id.value()));
    CHECK_THAT(ids, VectorContains(d3.key.id.value()));
  }

  // 2) refsFrom(report, "appendix.attachments") → Doc B
  {
    auto fwd = backend.refsFrom(r.key, "appendix.attachments");
    REQUIRE(fwd.size() == 1);
    CHECK(fwd[0].id == d2.key.id.value());
  }

  // 3) refsTo(Doc A) → report
  {
    auto back = backend.refsTo(d1.key);
    bool found = false;
    for (auto &k : back)
      if (k.profile == "report" && k.id == r.key.id.value())
        found = true;
    REQUIRE(found);
  }

  // 4) refsTo(Doc B) → report
  {
    auto back = backend.refsTo(d2.key);
    bool found = false;
    for (auto &k : back)
      if (k.profile == "report" && k.id == r.key.id.value())
        found = true;
    REQUIRE(found);
  }

  // 5) Delete Doc B → should clear it from appendix.attachments, keep others intact
  backend.begin();
  backend.remove(d2.key);
  backend.commit();

  auto got = backend.fetch(r.key);
  REQUIRE(got.has_value());
  REQUIRE(got->fields.count("appendix") == 1);
  const auto &app = got->fields.at("appendix").asObject();
  REQUIRE(app.count("attachments") == 1);
  CHECK(app.at("attachments").asArray().empty()); // cleared after delete

  // ensure sections intact
  REQUIRE(got->fields.count("sections") == 1);
  auto arr = got->fields.at("sections").asArray();
  REQUIRE(arr.size() == 2);
  const auto &s2 = arr[1].asObject();
  CHECK(s2.at("main_ref").asString() == d3.key.id.value());
}
