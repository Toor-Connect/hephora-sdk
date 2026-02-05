#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <vector>
#include <stdexcept>

#include "SchemaRegistry.h"
#include "NodeSchema.h"

// --- Basic add + lookup ---
TEST_CASE("SchemaRegistry can add and lookup schemas", "[SchemaRegistry]")
{
    SchemaRegistry registry;

    auto project = std::make_shared<NodeSchema>("project", NodeKind::Root, "Top-level project");
    auto requirement = std::make_shared<NodeSchema>("requirement", NodeKind::Node, "Requirement node");

    REQUIRE(registry.addSchema(project) == true);
    REQUIRE(registry.addSchema(requirement) == true);

    SECTION("Lookup works for existing schemas")
    {
        auto *p = registry.getSchema("project");
        REQUIRE(p != nullptr);
        REQUIRE(p->profileName() == "project");
        REQUIRE(registry.hasSchema("project"));

        auto *r = registry.getSchema("requirement");
        REQUIRE(r != nullptr);
        REQUIRE(r->profileName() == "requirement");
        REQUIRE(registry.hasSchema("requirement"));
    }

    SECTION("Lookup returns nullptr for missing schema")
    {
        REQUIRE(registry.getSchema("missing") == nullptr);
        REQUIRE_FALSE(registry.hasSchema("missing"));
    }
}

// --- Duplicates ---
TEST_CASE("SchemaRegistry prevents duplicates", "[SchemaRegistry]")
{
    SchemaRegistry registry;

    auto requirement1 = std::make_shared<NodeSchema>("requirement", NodeKind::Node, "Requirement A");
    auto requirement2 = std::make_shared<NodeSchema>("requirement", NodeKind::Node, "Requirement B");

    REQUIRE(registry.addSchema(requirement1) == true);
    REQUIRE(registry.addSchema(requirement2) == false); // duplicate name rejected

    REQUIRE(registry.getSchema("requirement")->description() == "Requirement A");
}

// --- Root tracking ---
TEST_CASE("SchemaRegistry enforces a single root schema", "[SchemaRegistry][Root]")
{
    SchemaRegistry registry;

    auto project = std::make_shared<NodeSchema>("project", NodeKind::Root, "Project root");
    auto requirement = std::make_shared<NodeSchema>("requirement", NodeKind::Node, "Requirement node");

    REQUIRE(registry.addSchema(project) == true);
    REQUIRE(registry.root() == project.get());

    REQUIRE(registry.addSchema(requirement) == true);
    REQUIRE(registry.root()->profileName() == "project");

    // Adding a second root should throw
    auto attachment = std::make_shared<NodeSchema>("attachment", NodeKind::Root, "Attachment root");
    REQUIRE_THROWS_AS(registry.addSchema(attachment), std::invalid_argument);
}

// --- Clear registry ---
TEST_CASE("SchemaRegistry clear() removes all schemas and root", "[SchemaRegistry]")
{
    SchemaRegistry registry;

    auto project = std::make_shared<NodeSchema>("project", NodeKind::Root, "Project root");
    registry.addSchema(project);

    REQUIRE(registry.getSchema("project") != nullptr);
    REQUIRE(registry.root() != nullptr);

    registry.clear();

    REQUIRE(registry.getSchema("project") == nullptr);
    REQUIRE(registry.root() == nullptr);
    REQUIRE_FALSE(registry.hasSchema("project"));
}
