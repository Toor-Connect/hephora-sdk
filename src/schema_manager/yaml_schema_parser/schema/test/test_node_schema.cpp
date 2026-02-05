#include <catch2/catch_test_macros.hpp>

#include "NodeSchema.h"
#include "StringFieldSchema.h"
#include "IntegerFieldSchema.h"
#include "BooleanFieldSchema.h"
#include "EnumFieldSchema.h"
#include "ObjectFieldSchema.h"
#include "ArrayFieldSchema.h"
#include "ReferenceFieldSchema.h"

// ------------------- METADATA -------------------
TEST_CASE("NodeSchema metadata works", "[NodeSchema]")
{
    NodeSchema project("project", NodeKind::Root, "Top-level project", "Project");

    REQUIRE(project.profileName() == "project");
    REQUIRE(project.kind() == NodeKind::Root);
    REQUIRE(project.description() == "Top-level project");
    REQUIRE(project.meta() == "Project");
}

TEST_CASE("NodeSchema with non-default alias", "[NodeSchema]")
{
    NodeSchema req("requirement", NodeKind::Node, "Requirement node");

    REQUIRE(req.profileName() == "requirement");
    REQUIRE(req.meta().empty());
}

// ------------------- FIELDS -------------------
TEST_CASE("NodeSchema can add and retrieve fields", "[NodeSchema]")
{
    NodeSchema requirement("requirement", NodeKind::Node, "Requirement node");

    REQUIRE(requirement.addField(std::make_unique<StringFieldSchema>("title", true, "Title", "Main title", "Default Title")) == true);
    REQUIRE(requirement.addField(std::make_unique<BooleanFieldSchema>("active", false, "Active", "Activation flag", true)) == true);

    // Adding duplicate field name should fail
    REQUIRE(requirement.addField(std::make_unique<IntegerFieldSchema>("title", false)) == false);

    auto *titleField = requirement.getField("title");
    REQUIRE(titleField != nullptr);
    REQUIRE(titleField->type() == FieldType::String);
    REQUIRE(titleField->required());
    REQUIRE(titleField->meta() == "Title");
    REQUIRE(titleField->description() == "Main title");
    auto *stringField = dynamic_cast<StringFieldSchema *>(titleField);
    REQUIRE(stringField != nullptr);
    REQUIRE(stringField->defaultValue().has_value());
    REQUIRE(stringField->defaultValue().value() == "Default Title");

    auto *activeField = requirement.getField("active");
    REQUIRE(activeField != nullptr);
    REQUIRE(activeField->type() == FieldType::Boolean);
    REQUIRE_FALSE(activeField->required());
    REQUIRE(activeField->meta() == "Active");
    auto *boolField = dynamic_cast<BooleanFieldSchema *>(activeField);
    REQUIRE(boolField != nullptr);
    REQUIRE(boolField->defaultValue().has_value());
    REQUIRE(boolField->defaultValue().value() == true);

    auto *missing = requirement.getField("does_not_exist");
    REQUIRE(missing == nullptr);
}

TEST_CASE("NodeSchema can add and retrieve enum field with default", "[NodeSchema]")
{
    NodeSchema schema("requirement", NodeKind::Node, "Requirement node");

    REQUIRE(schema.addField(std::make_unique<EnumFieldSchema>(
                "priority", true, std::vector<std::string>{"low", "medium", "high"},
                "Priority", "Importance level", "medium")) == true);

    auto *priority = schema.getField("priority");
    REQUIRE(priority != nullptr);
    REQUIRE(priority->type() == FieldType::Enum);

    auto *enumField = dynamic_cast<EnumFieldSchema *>(priority);
    REQUIRE(enumField != nullptr);
    REQUIRE(enumField->values().size() == 3);
    REQUIRE(enumField->defaultValue().has_value());
    REQUIRE(enumField->defaultValue().value() == "medium");
}

// ------------------- CHILDREN -------------------
TEST_CASE("NodeSchema can add and retrieve children", "[NodeSchema]")
{
    auto project = std::make_shared<NodeSchema>("project", NodeKind::Root, "Top-level project");
    auto requirement = std::make_shared<NodeSchema>("requirement", NodeKind::Node, "Requirement node");
    auto attachment = std::make_shared<NodeSchema>("attachment", NodeKind::Node, "Attachment node");

    project->addChild("requirements", requirement);
    project->addChild("attachments", attachment);

    auto *reqChild = project->getChild("requirements");
    REQUIRE(reqChild != nullptr);
    REQUIRE(reqChild->profileName() == "requirement");

    auto *attChild = project->getChild("attachments");
    REQUIRE(attChild != nullptr);
    REQUIRE(attChild->profileName() == "attachment");

    auto *missing = project->getChild("does_not_exist");
    REQUIRE(missing == nullptr);
}

// ------------------- ENUM DEFAULT VALIDATION -------------------
TEST_CASE("EnumFieldSchema throws if default not in values", "[FieldSchema][EdgeCases]")
{
    REQUIRE_THROWS_AS(
        EnumFieldSchema("priority", true,
                        std::vector<std::string>{"low", "medium", "high"},
                        "Priority", "Importance level", "urgent"), // not in list
        std::invalid_argument);
}