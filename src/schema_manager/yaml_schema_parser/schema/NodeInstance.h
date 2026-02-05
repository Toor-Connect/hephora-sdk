#pragma once
#include <map>
#include <string>
#include <vector>
#include "FieldSchema.h"

struct NodeInstance
{
    std::map<std::string, FieldValue> fields;
    std::map<std::string, std::vector<NodeInstance>> children;
};
