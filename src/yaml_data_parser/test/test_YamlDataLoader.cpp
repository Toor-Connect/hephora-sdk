// src/data/test/test_YamlDataLoader.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <unordered_map>
#include <yaml-cpp/yaml.h>

#include "YamlDataLoader.h"

using Catch::Matchers::ContainsSubstring;

TEST_CASE("YamlDataLoader::loadFromSources returns dictionary filename -> YAML::Node and tags nodes",
          "[YamlDataLoader][multi]")
{
    std::unordered_map<std::string, std::string> files = {
        {"requirement/R-0001.yaml", R"(
_profile: requirement
_id: R-0001
_label: "REQ-1 Brake latency"
_parent_id: P-0001

title: "Brake latency under 100 ms"
priority: high
active: true
)"},
        {"attachment/A-0001.yaml", R"(
_profile: attachment
_id: A-0001
_label: "Spec PDF"
_parent_id: P-0001
filename: "spec.pdf"
filetype: pdf
path: "/docs/spec.pdf"
)"}};

    auto dict = YamlDataLoader::loadFromSources(files);
    REQUIRE(dict.size() == 2);
    REQUIRE(dict.count("requirement/R-0001.yaml") == 1);
    REQUIRE(dict.count("attachment/A-0001.yaml") == 1);

    REQUIRE(dict.at("requirement/R-0001.yaml")["_profile"].as<std::string>() == "requirement");
    REQUIRE(dict.at("attachment/A-0001.yaml")["_profile"].as<std::string>() == "attachment");

    // Tags also carry the filename for diagnostics
    REQUIRE(dict.at("requirement/R-0001.yaml").Tag().find("requirement/R-0001.yaml") != std::string::npos);
    REQUIRE(dict.at("attachment/A-0001.yaml").Tag().find("attachment/A-0001.yaml") != std::string::npos);
}

TEST_CASE("YamlDataLoader::loadFromSources throws on YAML parse error",
          "[YamlDataLoader][errors]")
{
    std::unordered_map<std::string, std::string> files = {
        {"bad.yaml", "key: [1, 2"} // missing closing bracket → parser exception
    };

    REQUIRE_THROWS_WITH(
        YamlDataLoader::loadFromSources(files),
        ContainsSubstring("YAML parse error in [bad.yaml]"));
}

TEST_CASE("YamlDataLoader keeps filename->doc pairing")
{
    std::unordered_map<std::string, std::string> src = {
        {"project/P-1.yaml", "_profile: project\n_id: P-1\n"},
        {"attachment/A-1.yaml", "_profile: attachment\n_id: A-1\n"},
        {"requirement/R-1.yaml", "_profile: requirement\n_id: R-1\n"},
    };
    auto docs = YamlDataLoader::loadFromSources(src);
    REQUIRE(docs.at("project/P-1.yaml")["_profile"].as<std::string>() == "project");
    REQUIRE(docs.at("attachment/A-1.yaml")["_profile"].as<std::string>() == "attachment");
    REQUIRE(docs.at("requirement/R-1.yaml")["_profile"].as<std::string>() == "requirement");
}
