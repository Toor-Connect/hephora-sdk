#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "NodeAddress.h"

using json = nlohmann::json;

TEST_CASE("NodeSnapshot::toJson - minimal node", "[NodeSnapshot]")
{
    NodeSnapshot n;
    n.key = {"requirement", "R-001"};

    auto output = json::parse(n.toJson());

    REQUIRE(output["profile"] == "requirement");
    REQUIRE(output["id"] == "R-001");
    REQUIRE(output.contains("fields"));
    REQUIRE(output["fields"].is_object());
    REQUIRE(output["fields"].empty());
}

TEST_CASE("NodeSnapshot::toJson - with label and parent", "[NodeSnapshot]")
{
    NodeSnapshot n;
    n.key = {"project", "P-123"};
    n.label = "Demo project";
    n.parent_id = "ROOT";

    auto output = json::parse(n.toJson());

    REQUIRE(output["profile"] == "project");
    REQUIRE(output["id"] == "P-123");
    REQUIRE(output["label"] == "Demo project");
    REQUIRE(output["parent"] == "ROOT");
}

TEST_CASE("NodeSnapshot::toJson - scalar fields", "[NodeSnapshot]")
{
    NodeSnapshot n;
    n.key = {"requirement", "R-001"};
    n.fields["priority"] = std::string("high");
    n.fields["active"] = true;
    n.fields["effort"] = 5;

    auto output = json::parse(n.toJson());
    auto f = output["fields"];

    REQUIRE(f["priority"] == "high");
    REQUIRE(f["active"] == true);
    REQUIRE(f["effort"] == 5);
}

TEST_CASE("NodeSnapshot::toJson - array field", "[NodeSnapshot]")
{
    NodeSnapshot n;
    n.key = {"requirement", "R-002"};
    ArrayData tags = {std::string("safety"), std::string("performance")};
    n.fields["tags"] = tags;

    auto output = json::parse(n.toJson());
    auto arr = output["fields"]["tags"];

    REQUIRE(arr.is_array());
    REQUIRE(arr.size() == 2);
    REQUIRE(arr[0] == "safety");
    REQUIRE(arr[1] == "performance");
}

TEST_CASE("NodeSnapshot::toJson - object field", "[NodeSnapshot]")
{
    NodeSnapshot n;
    n.key = {"requirement", "R-003"};
    ObjectData meta;
    meta["author"] = std::string("Narcís");
    meta["reviewed"] = false;
    n.fields["metadata"] = meta;

    auto output = json::parse(n.toJson());
    auto obj = output["fields"]["metadata"];

    REQUIRE(obj["author"] == "Narcís");
    REQUIRE(obj["reviewed"] == false);
}

TEST_CASE("NodeSnapshot::toJson - nested array of objects", "[NodeSnapshot]")
{
    NodeSnapshot n;
    n.key = {"requirement", "R-004"};

    ObjectData step1;
    step1["name"] = std::string("Init");
    step1["done"] = true;

    ObjectData step2;
    step2["name"] = std::string("Validate");
    step2["done"] = false;

    ArrayData steps = {step1, step2};
    n.fields["steps"] = steps;

    auto output = json::parse(n.toJson());
    auto arr = output["fields"]["steps"];

    REQUIRE(arr.is_array());
    REQUIRE(arr.size() == 2);
    REQUIRE(arr[0]["name"] == "Init");
    REQUIRE(arr[0]["done"] == true);
    REQUIRE(arr[1]["name"] == "Validate");
    REQUIRE(arr[1]["done"] == false);
}
