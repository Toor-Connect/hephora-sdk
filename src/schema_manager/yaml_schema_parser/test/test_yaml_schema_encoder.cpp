#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <yaml-cpp/yaml.h>

#include "YamlSchemaEncoder.h"
#include "YamlSchemaDecoder.h"

TEST_CASE("YamlSchemaEncoder encodes NodeSchema correctly", "[yaml]")
{
    const std::string yamlStr = R"(
name: project
kind: root
description: Top-level project profile containing requirements and attachments
meta:
  alias: Project

fields:
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

    YamlSchemaEncoder encoder;
    YAML::Node encoded = encoder.encodeNodeSchema(*schema);

    // Metadata
    REQUIRE(encoded["name"].as<std::string>() == "project");
    REQUIRE(encoded["kind"].as<std::string>() == "root");
    REQUIRE(encoded["description"].as<std::string>().find("Top-level project") != std::string::npos);
    REQUIRE(encoded["meta"]);
    REQUIRE(encoded["meta"]["alias"].as<std::string>() == "Project");

    // Fields
    const auto &fields = encoded["fields"];

    REQUIRE(fields["version"]["type"].as<std::string>() == "string");
    REQUIRE(fields["version"]["required"].as<bool>() == true);
    REQUIRE(fields["version"]["meta"]);
    REQUIRE(fields["version"]["meta"]["alias"].as<std::string>() == "Version");

    REQUIRE(fields["description"]["type"].as<std::string>() == "string");
    REQUIRE(fields["description"]["required"].as<bool>() == false);
    REQUIRE(fields["description"]["meta"]);
    REQUIRE(fields["description"]["meta"]["alias"].as<std::string>() == "Description");

    REQUIRE(fields["owner"]["type"].as<std::string>() == "string");
    REQUIRE(fields["owner"]["required"].as<bool>() == false);
    REQUIRE(fields["owner"]["meta"]);
    REQUIRE(fields["owner"]["meta"]["alias"].as<std::string>() == "Owner");
}
