#include <catch2/catch_test_macros.hpp>
#include <unordered_map>
#include "YamlDataEncoder.h"
#include "YamlDataDecoder.h"
#include "YamlDataLoader.h"
#include "YamlSchemaLoader.h"
#include "YamlSchemaDecoder.h"
#include "SchemaRegistry.h"

TEST_CASE("YamlDataEncoder round-trips through YamlDataDecoder", "[YamlDataEncoder][roundtrip]")
{
    // minimal schema set (project + requirement)
    SchemaRegistry reg;
    std::unordered_map<std::string, std::string> prof = {
        {"project.yaml", R"(
name: project
kind: root
description: project description
fields:
  project_name: { type: string, required: true }
)"},
        {"requirement.yaml", R"(
name: requirement
kind: node
description: requirement description
fields:
  title: { type: string, required: true }
  active: { type: boolean }
  specs:
    type: object
    fields:
      manufacturer: { type: string }
      warranty_years: { type: integer }
)"}};
    auto schemaNodes = YamlSchemaLoader::loadFromSources(prof);
    YamlSchemaDecoder::decodeProfiles(schemaNodes, reg);

    // build a snapshot in memory
    NodeSnapshot snap;
    snap.key = {"requirement", "R-77"};
    snap.parent_id = "P-1";
    snap.fields["title"] = FieldValue(std::string("Brake latency"));
    snap.fields["active"] = FieldValue(true);
    snap.fields["specs"] = FieldValue(ObjectData{
        {"manufacturer", FieldValue(std::string("ToorConnect"))},
        {"warranty_years", FieldValue(5)}});

    // encode to YAML node
    YamlDataEncoder enc;
    YAML::Node y = enc.encodeNode(snap);

    // pretend it was loaded from a file
    std::unordered_map<std::string, YAML::Node> docs = {
        {"requirement/R-77.yaml", y}};

    // decode back
    auto decoded = YamlDataDecoder::decode(docs, reg);
    REQUIRE(decoded.size() == 1);
    const auto &dn = decoded.front();

    CHECK(dn.profile == "requirement");
    CHECK(dn.id == "R-77");
    CHECK(dn.parent_id == "P-1");
    REQUIRE(dn.instance.fields.count("title") == 1);
    CHECK(dn.instance.fields.at("title").asString() == "Brake latency");
    CHECK(dn.instance.fields.at("active").asBoolean() == true);

    const auto &specs = dn.instance.fields.at("specs").asObject();
    CHECK(specs.at("manufacturer").asString() == "ToorConnect");
    CHECK(specs.at("warranty_years").asInteger() == 5);
}
