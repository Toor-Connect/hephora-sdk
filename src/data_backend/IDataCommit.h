#pragma once
#include <string>
#include "NodeAddress.h"

// Writes nodes back to YAML (the source of truth). The caller chooses the filename.
class IDataCommit
{
public:
    virtual ~IDataCommit() = default;

    // Serialize a node snapshot to YAML and write to "filename"
    virtual void writeNode(const std::string &filename,
                           const NodeSnapshot &node) = 0;

    // Delete the YAML file for the node
    virtual void deleteNode(const std::string &filename) = 0;
};
