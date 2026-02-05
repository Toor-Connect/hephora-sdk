#include <catch2/catch_test_macros.hpp>
#include "StringFieldSchema.h"
#include "IntegerFieldSchema.h"
#include "BooleanFieldSchema.h"
#include "EnumFieldSchema.h"
#include "ObjectFieldSchema.h"
#include "ArrayFieldSchema.h"
#include "ReferenceFieldSchema.h"

#include <memory>

// ------------------- STRING -------------------
TEST_CASE("StringFieldSchema works", "[FieldSchema]")
{
    StringFieldSchema f("title", true);
    REQUIRE(f.type() == FieldType::String);
    REQUIRE(f.name() == "title");
    REQUIRE(f.required() == true);
    REQUIRE(f.meta().empty());
    REQUIRE(f.description().empty());
    REQUIRE_FALSE(f.defaultValue().has_value());

    StringFieldSchema f2("desc", false, "Description", "Optional description", "Default text");
    REQUIRE(f2.defaultValue().has_value());
    REQUIRE(f2.defaultValue().value() == "Default text");
}

// ------------------- INTEGER -------------------
TEST_CASE("IntegerFieldSchema works", "[FieldSchema]")
{
    IntegerFieldSchema f("age", false, "Age", "User age in years");
    REQUIRE(f.type() == FieldType::Integer);
    REQUIRE(f.name() == "age");
    REQUIRE(f.required() == false);
    REQUIRE(f.meta() == "Age");
    REQUIRE(f.description() == "User age in years");
    REQUIRE_FALSE(f.defaultValue().has_value());

    IntegerFieldSchema f2("years", false, "Years", "Years of service", 5);
    REQUIRE(f2.defaultValue().has_value());
    REQUIRE(f2.defaultValue().value() == 5);
}

// ------------------- BOOLEAN -------------------
TEST_CASE("BooleanFieldSchema works", "[FieldSchema]")
{
    BooleanFieldSchema f("active", true);
    REQUIRE(f.type() == FieldType::Boolean);
    REQUIRE(f.required());
    REQUIRE_FALSE(f.defaultValue().has_value());

    BooleanFieldSchema f2("enabled", false, "Enabled", "Flag", true);
    REQUIRE(f2.defaultValue().has_value());
    REQUIRE(f2.defaultValue().value() == true);
}

// ------------------- ENUM -------------------
TEST_CASE("EnumFieldSchema stores values", "[FieldSchema]")
{
    EnumFieldSchema f("priority", true, {"low", "medium", "high"});
    REQUIRE(f.type() == FieldType::Enum);
    REQUIRE(f.values().size() == 3);
    REQUIRE(f.values()[1] == "medium");
    REQUIRE_FALSE(f.defaultValue().has_value());

    EnumFieldSchema f2("priority2", true, {"low", "medium", "high"}, "Priority", "Level", "medium");
    REQUIRE(f2.defaultValue().has_value());
    REQUIRE(f2.defaultValue().value() == "medium");
}

// ------------------- OBJECT -------------------
TEST_CASE("ObjectFieldSchema holds nested fields", "[FieldSchema]")
{
    std::vector<std::unique_ptr<FieldSchema>> fields;
    fields.push_back(std::make_unique<StringFieldSchema>("manufacturer", false));
    fields.push_back(std::make_unique<IntegerFieldSchema>("warranty", false));

    ObjectFieldSchema f("specs", false, std::move(fields));
    REQUIRE(f.type() == FieldType::Object);
    REQUIRE(f.fields().size() == 2);
    REQUIRE(f.fields()[0]->name() == "manufacturer");
    REQUIRE(f.fields()[1]->type() == FieldType::Integer);
}

// ------------------- ARRAY -------------------
TEST_CASE("ArrayFieldSchema wraps items", "[FieldSchema]")
{
    auto item = std::make_unique<StringFieldSchema>("tag", false);
    ArrayFieldSchema f("tags", false, std::move(item));
    REQUIRE(f.type() == FieldType::Array);
    REQUIRE(f.items()->type() == FieldType::String);
}

// ------------------- REFERENCE -------------------
TEST_CASE("ReferenceFieldSchema stores target", "[FieldSchema]")
{
    ReferenceFieldSchema f("ref", true, "attachment");
    REQUIRE(f.type() == FieldType::Reference);
    REQUIRE(f.target() == "attachment");
}

// ------------------- CONSTRUCTOR VALIDATION -------------------
TEST_CASE("ObjectFieldSchema can contain arrays of objects with only primitives", "[FieldSchema][Constraints]")
{
    // person object with only primitive fields
    std::vector<std::unique_ptr<FieldSchema>> personFields;
    personFields.push_back(std::make_unique<StringFieldSchema>("name", true));
    auto personObject = std::make_unique<ObjectFieldSchema>("person", true, std::move(personFields));

    // array of person objects
    auto arr = std::make_unique<ArrayFieldSchema>("people", true, std::move(personObject));

    // project object containing array of people → should NOT throw
    std::vector<std::unique_ptr<FieldSchema>> rootFields;
    rootFields.push_back(std::move(arr));

    REQUIRE_NOTHROW(ObjectFieldSchema("root", true, std::move(rootFields)));
}

TEST_CASE("ArrayFieldSchema of objects cannot contain arrays inside", "[FieldSchema][Constraints]")
{
    // Inner object contains an array → invalid
    auto invalidInnerArray = std::make_unique<ArrayFieldSchema>(
        "tags", true, std::make_unique<StringFieldSchema>("tag", true));

    std::vector<std::unique_ptr<FieldSchema>> innerFields;
    innerFields.push_back(std::move(invalidInnerArray));
    auto invalidObject = std::make_unique<ObjectFieldSchema>("item", true, std::move(innerFields));

    // Try to wrap that invalid object inside an array → should throw
    REQUIRE_THROWS_AS(
        ArrayFieldSchema("items", true, std::move(invalidObject)),
        std::invalid_argument);
}

TEST_CASE("ArrayFieldSchema of objects with only primitives is allowed", "[FieldSchema][Constraints]")
{
    // Object with only primitives inside is valid
    std::vector<std::unique_ptr<FieldSchema>> objFields;
    objFields.push_back(std::make_unique<StringFieldSchema>("name", true));
    objFields.push_back(std::make_unique<IntegerFieldSchema>("age", true));

    auto validObject = std::make_unique<ObjectFieldSchema>("person", true, std::move(objFields));

    // Wrapping that object in an array is valid (should not throw)
    REQUIRE_NOTHROW(ArrayFieldSchema("people", true, std::move(validObject)));
}

TEST_CASE("ObjectFieldSchema with array of primitives is allowed", "[FieldSchema][Constraints]")
{
    auto arr = std::make_unique<ArrayFieldSchema>(
        "tags", true, std::make_unique<StringFieldSchema>("tag", true));

    std::vector<std::unique_ptr<FieldSchema>> rootFields;
    rootFields.push_back(std::move(arr));

    // Allowed: objects can contain arrays of primitives
    REQUIRE_NOTHROW(ObjectFieldSchema("root", true, std::move(rootFields)));
}

// ------------------- VALIDATION WITH ALLOWED CONSTRUCTS -------------------

TEST_CASE("Invalid schema: Nested array inside array of objects throws", "[FieldSchema][Constraints]")
{
    // Inner object: { name: string, tags: [string] } → not allowed (array inside object)
    auto innerArray = std::make_unique<ArrayFieldSchema>(
        "tags", true, std::make_unique<StringFieldSchema>("tag", true));

    std::vector<std::unique_ptr<FieldSchema>> badMemberFields;
    badMemberFields.push_back(std::make_unique<StringFieldSchema>("name", true));
    badMemberFields.push_back(std::move(innerArray));
    auto badMemberObject = std::make_unique<ObjectFieldSchema>("member", true, std::move(badMemberFields));

    // Try to wrap that object in an array → should throw
    REQUIRE_THROWS_AS(
        ArrayFieldSchema("members", true, std::move(badMemberObject)),
        std::invalid_argument);
}