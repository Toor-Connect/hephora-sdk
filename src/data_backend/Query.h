// src/data_backend/Query.h
#pragma once
#include <string>
#include <vector>
#include "FieldValue.h"

// Operators that map cleanly to SQL or in-memory evaluators
enum class FieldOp
{
    EQ,
    NEQ,
    IN,
    NOT_IN,   // equality / set
    CONTAINS, // array contains value OR string contains substring (backend-defined)
    PREFIX,   // string starts with
    GT,
    GTE,
    LT,
    LTE, // numeric comparisons
    IS_NULL,
    NOT_NULL // presence checks (for optional/nullable columns)
};

struct FieldFilter
{
    std::string field; // flattened name, e.g. "priority", "specs.warranty_years", "tags"
    FieldOp op;
    FieldValue value; // For IN/NOT_IN use ArrayData within FieldValue; ignored for IS_NULL/NOT_NULL
};

// Disjunctive Normal Form: OR-of-ANDs
//   (a1 AND a2 ...) OR (b1 AND b2 ...) OR ...
struct QueryDNF
{
    std::string profile;                          // required
    std::vector<std::vector<FieldFilter>> any_of; // each inner vector is an AND group
    std::vector<std::string> order_by;            // flattened field names
    bool ascending = true;
    int limit = 0; // 0 = no limit
    int offset = 0;
};
