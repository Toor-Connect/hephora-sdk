// src/data_backend/sqlite/SQLiteDataBackend.h
#pragma once
#include <string>
#include <vector>
#include <optional>
#include <sqlite3.h>

#include "IDataBackend.h"
#include "SchemaRegistry.h"
#include "ArrayFieldSchema.h"
#include "ObjectFieldSchema.h"

// SQLite implementation of IDataBackend.
// Notes:
//  - Uses the same relational layout as SQLiteSchemaExporter
//  - PRAGMA foreign_keys=ON
//  - Upsert writes main table and side tables for array fields (if present in snapshot)
//  - find(QueryDNF) supports primitives, flattened object fields, and array CONTAINS via EXISTS
class SQLiteDataBackend : public IDataBackend
{
public:
    // db_path: file path or ":memory:"
    // recreate: if true, will attempt to (re)create DDL; "table exists" errors are ignored
    explicit SQLiteDataBackend(std::string db_path, bool recreate = true);
    ~SQLiteDataBackend() override;

    // ---- IDataBackend ----
    void init(const SchemaRegistry &registry) override;
    void begin() override;
    void commit() override;
    void rollback() override;

    void upsert(NodeSnapshot &node) override;
    void setReferenceChecksEnabled(bool enabled) override;
    void remove(const NodeKey &key) override;
    void validateData() const override;

    bool exists(const NodeKey &key) const override;
    std::optional<NodeSnapshot> fetch(const NodeKey &key) const override;
    std::optional<std::string> parentOf(const NodeKey &key) const override;

    std::vector<NodeKey> childrenOf(const NodeKey &parent,
                                    const std::string &childProfile) const override;

    std::vector<NodeKey> refsFrom(const NodeKey &key,
                                  const std::string &field) const override;

    std::vector<NodeKey> refsTo(const NodeKey &key) const override;

    std::vector<NodeSnapshot> find(const QueryDNF &q) const override;

    void reset(void) override;

    sqlite3 *rawHandle() const { return db_; }

private:
    sqlite3 *db_{nullptr};
    std::string db_path_;
    bool recreate_{true};
    const SchemaRegistry *registry_{nullptr}; // not owned

    // ---- helpers ----
    void open();
    void close();
    void execOrIgnoreExists(const std::string &sql) const;
    void exec(const std::string &sql) const;

    // Column naming same as your exporter
    static std::string flattenedObjectCol(const std::string &prefix, const std::string &sub);
    static bool isArrayField(const FieldSchema *fs) { return fs->type() == FieldType::Array; }
    static bool isObjectField(const FieldSchema *fs) { return fs->type() == FieldType::Object; }

    // Build list of "main table" columns we can set (primitives + object subfields; exclude arrays)
    void collectMainColumns(const NodeSchema *schema,
                            std::vector<std::pair<std::string, const FieldSchema *>> &cols,
                            const std::string &prefix = "") const;

    void collectMainColumns(
        const ObjectFieldSchema *obj,
        std::vector<std::pair<std::string, const FieldSchema *>> &cols,
        const std::string &prefix) const;

    // Read/write array tables
    void writeArrayField(const NodeSnapshot &node,
                         const std::string &fieldName,
                         const ArrayFieldSchema *arr) const;

    FieldValue readArrayField(const NodeKey &key,
                              const std::string &owner,
                              const std::string &fieldName,
                              const ArrayFieldSchema *arr) const;

    // Helpers to map a dotted path ("specs.warranty_years") to a main-table column name
    std::string columnForPath(const NodeSchema *schema,
                              const std::string &path) const;

    void collectRefsToRecursive(
        const std::string &owner,
        const std::string &prefix,
        const FieldSchema *fs,
        const NodeKey &key,
        std::vector<NodeKey> &out) const;

    // WHERE builders for QueryDNF
    struct WhereSQL
    {
        std::string sql;
        std::vector<FieldValue> params;
    };
    WhereSQL buildWhereDNF(const NodeSchema *schema, const QueryDNF &q) const;
    WhereSQL buildWhereFilter(const NodeSchema *schema, const FieldFilter &f) const;

    // Binding/extraction
    static void bindFieldValue(sqlite3_stmt *st, int idx, const FieldValue &v);
    static FieldValue readColumn(sqlite3_stmt *st, int col, FieldType type);
    static FieldValue readColumnAuto(sqlite3_stmt *st, int col); // TEXT/INTEGER/NULL→skip

    // Small RAII for sqlite statements (see cpp)
    struct Stmt;
};
