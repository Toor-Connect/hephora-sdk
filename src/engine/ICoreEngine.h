#pragma once
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "SchemaRegistry.h"
#include "NodeAddress.h"
#include "Query.h"
#include "IDataBackend.h"
#include "IDataCommit.h"
#include <yaml-cpp/yaml.h>

struct EngineConfig
{
    std::string base_path = "data/"; // base path for relative filenames
};

class ICoreEngine
{
public:
    virtual ~ICoreEngine() = default;

    // lifecycle
    virtual void init(const SchemaRegistry &registry) = 0;

    // single-ops (edit backend only)
    virtual void upsert(NodeSnapshot &node) = 0;
    virtual void remove(const NodeKey &key) = 0;

    // reads
    virtual bool exists(const NodeKey &key) const = 0;
    virtual std::optional<NodeSnapshot> fetch(const NodeKey &key) const = 0;
    virtual std::optional<std::string> parentOf(const NodeKey &key) const = 0;
    virtual std::vector<NodeKey> childrenOf(const NodeKey &parent, const std::string &childProfile) const = 0;
    virtual std::vector<NodeKey> refsFrom(const NodeKey &owner, const std::string &field) const = 0;
    virtual std::vector<NodeKey> refsTo(const NodeKey &target) const = 0;
    virtual std::vector<NodeSnapshot> find(const QueryDNF &q) const = 0;

    // YAML persistence (explicit!)
    virtual void flushPending() = 0; // write dirty_upserts_ / dirty_deletes_ via IDataCommit
    virtual void flushAll() = 0;     // write ALL current nodes (no deletions beyond pending)

    // Loading YAML Data
    virtual void loadData(const std::unordered_map<std::string, YAML::Node> &docs) = 0;

    virtual void resetBackend(void) = 0;
};
