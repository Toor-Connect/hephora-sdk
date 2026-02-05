#pragma once
#include "SchemaRegistry.h"
#include <string>

class ISchemaExporter
{
public:
    virtual ~ISchemaExporter() = default;

    // Generate backend-specific schema representation
    virtual std::string exportSchema(const SchemaRegistry &registry) const = 0;
};
