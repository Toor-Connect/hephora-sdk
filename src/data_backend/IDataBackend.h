#pragma once
#include <optional>
#include <vector>
#include "SchemaRegistry.h"
#include "NodeAddress.h"
#include "Query.h"

// Pluggable data backend: stores/query nodes described by the SchemaRegistry.
// Implementations may be SQLite, in-memory, Postgres, etc.
class IDataBackend
{
public:
    virtual ~IDataBackend() = default;

    // ---- lifecycle / transactions ----
    virtual void init(const SchemaRegistry &registry) = 0; // ensure storage matches registry (DDL etc.)
    virtual void begin() = 0;
    virtual void commit() = 0;
    virtual void rollback() = 0;

    // ---- mutations ----
    virtual void upsert(NodeSnapshot &node) = 0;
    virtual void remove(const NodeKey &key) = 0;

    // ---- point reads ----
    virtual bool exists(const NodeKey &key) const = 0;
    virtual std::optional<NodeSnapshot> fetch(const NodeKey &key) const = 0;
    // Validate referential integrity and other constraints after bulk load
    virtual void validateData() const = 0;
    // Enable or disable reference (foreign key) checks
    virtual void setReferenceChecksEnabled(bool enabled) = 0;
    virtual std::optional<std::string> parentOf(const NodeKey &key) const = 0;

    // ---- relationships ----
    // Children of a given parent, filtered by child profile
    virtual std::vector<NodeKey> childrenOf(const NodeKey &parent,
                                            const std::string &childProfile) const = 0;

    // Outbound references from a node via a reference field (supports ref or ref-array fields)
    virtual std::vector<NodeKey> refsFrom(const NodeKey &key,
                                          const std::string &field) const = 0;

    // Inbound references to a node (reverse edges across all profiles/fields)
    virtual std::vector<NodeKey> refsTo(const NodeKey &key) const = 0;

    // ---- queries ----
    // Disjunctive query with ordering and pagination
    virtual std::vector<NodeSnapshot> find(const QueryDNF &q) const = 0;

    virtual void reset(void) = 0;
};
