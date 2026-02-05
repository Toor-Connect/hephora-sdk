#include <catch2/catch_test_macros.hpp>
#include <yaml-cpp/yaml.h>
#include "YamlDataEncoder.h"
#include "FieldValue.h"
#include "NodeAddress.h"

TEST_CASE("YamlDataEncoder writes reserved keys and flat fields", "[YamlDataEncoder]")
{
    NodeSnapshot snap;
    snap.key = {"requirement", "R-42"};
    snap.label = "Latency";
    snap.parent_id = "P-1";
    snap.fields["title"] = FieldValue(std::string("Keep latency < 100ms"));
    snap.fields["active"] = FieldValue(true);
    snap.fields["tags"] = FieldValue(std::vector<FieldValue>{
        FieldValue(std::string("safety")),
        FieldValue(std::string("timing"))});
    snap.fields["specs"] = FieldValue(ObjectData{
        {"manufacturer", FieldValue(std::string("ToorConnect"))},
        {"warranty_years", FieldValue(3)}});

    YamlDataEncoder enc;
    YAML::Node n = enc.encodeNode(snap);

    // reserved header
    REQUIRE(n["_profile"]);
    REQUIRE(n["_id"]);
    CHECK(n["_profile"].as<std::string>() == "requirement");
    CHECK(n["_id"].as<std::string>() == "R-42");
    CHECK(n["_label"].as<std::string>() == "Latency");
    CHECK(n["_parent_id"].as<std::string>() == "P-1");

    // fields live at top-level, not under "fields"
    REQUIRE(n["title"]);
    REQUIRE(n["active"]);
    REQUIRE(n["tags"]);
    REQUIRE(n["specs"]);
    CHECK_FALSE(n["fields"]); // must NOT exist

    CHECK(n["title"].as<std::string>() == "Keep latency < 100ms");
    CHECK(n["active"].as<bool>() == true);
    REQUIRE(n["tags"].IsSequence());
    CHECK(n["tags"][0].as<std::string>() == "safety");
    CHECK(n["tags"][1].as<std::string>() == "timing");

    REQUIRE(n["specs"].IsMap());
    CHECK(n["specs"]["manufacturer"].as<std::string>() == "ToorConnect");
    CHECK(n["specs"]["warranty_years"].as<int>() == 3);
}
