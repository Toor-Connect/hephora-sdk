#include "SQLiteDataBackend.h"
#include <stdexcept>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <unordered_set>
#include <stdexcept>
#include "SQLiteSchemaExporter.h"
#include <iostream>
#include <random>
#include <iomanip>

// Figure out the parent profile for a given child profile from the registry.
// Returns std::nullopt for roots.
static std::optional<std::string>
deriveParentProfile(const SchemaRegistry *registry, const std::string &childProfile)
{
    if (!registry)
        return std::nullopt;
    for (const auto &kv : registry->schemas())
    {
        const NodeSchema *parent = kv.second.get();
        for (const auto &ch : parent->children())
        {
            if (ch.second->profileName() == childProfile)
                return parent->profileName();
        }
    }
    return std::nullopt; // root (no parent)
}

static void insertNestedObject(ObjectData &root, const std::vector<std::string> &path, const FieldValue &v)
{
    if (path.empty())
        return;

    ObjectData *current = &root;
    for (size_t i = 0; i < path.size(); ++i)
    {
        const std::string &part = path[i];
        if (i == path.size() - 1)
        {
            (*current)[part] = v;
        }
        else
        {
            FieldValue &subv = (*current)[part];
            if (!subv.isObject())
                subv = ObjectData{};
            current = &subv.asObject();
        }
    }
}

static bool lookupNested(const ObjectData &root,
                         const std::vector<std::string> &path,
                         FieldValue &out)
{
    const ObjectData *cur = &root;
    for (size_t i = 0; i < path.size(); ++i)
    {
        auto it = cur->find(path[i]);
        if (it == cur->end())
            return false;

        if (i + 1 == path.size())
        {
            out = it->second; // leaf value (can be null, int, string, etc.)
            return true;
        }

        if (!it->second.isObject())
            return false; // path continues but value isn't an object

        cur = &it->second.asObject();
    }
    return false;
}

// Joins schema path parts with '$' for columns / side-table suffixes
static inline std::string joinName(const std::string &a, const std::string &b)
{
    return a.empty() ? b : (a + "$" + b);
}

// Split a flattened object column name "top$sub" → {"top", "sub"}
// If there's no '$', returns {name, ""}.
static inline std::pair<std::string, std::string> splitTopSub(const std::string &name)
{
    auto p = name.find('$');
    if (p == std::string::npos)
        return {name, ""};
    return {name.substr(0, p), name.substr(p + 1)};
}

std::string generateUuid()
{
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    static thread_local std::uniform_int_distribution<uint64_t> dist(0, std::numeric_limits<uint64_t>::max());

    uint64_t part1 = dist(rng);
    uint64_t part2 = dist(rng);

    // Set version (4) and variant (10x)
    uint16_t time_hi_and_version = static_cast<uint16_t>((part1 >> 48) & 0x0FFF);
    time_hi_and_version |= (4 << 12);

    uint16_t clock_seq = static_cast<uint16_t>((part2 >> 48) & 0x3FFF);
    clock_seq |= 0x8000;

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(8) << static_cast<uint32_t>(part1 >> 32) << "-"
        << std::setw(4) << static_cast<uint16_t>((part1 >> 16) & 0xFFFF) << "-"
        << std::setw(4) << time_hi_and_version << "-"
        << std::setw(4) << clock_seq << "-"
        << std::setw(12) << (part2 & 0xFFFFFFFFFFFFULL);

    return oss.str();
}

// Reject setting any required field/subfield to null in a patch.
static void assertNoExplicitNullOnRequired(const NodeSchema *schema,
                                           const NodeSnapshot &patch)
{

    // For patch, ID must exist; for inserts, it can be auto-generated.
    if (!patch.key.id.has_value())
        return; // fine — auto ID will be created later

    for (const auto &kv : schema->fields())
    {
        const std::string &fname = kv.first;
        const FieldSchema *fs = kv.second.get();

        auto it = patch.fields.find(fname);
        if (it == patch.fields.end())
            continue;

        const FieldValue &v = it->second;

        if (fs->type() == FieldType::Object)
        {
            if (!v.isObject())
                continue;
            const auto &obj = v.asObject();
            const auto *of = dynamic_cast<const ObjectFieldSchema *>(fs);
            for (const auto &sub : of->fields())
            {
                const std::string &subname = sub->name();
                auto itSub = obj.find(subname);
                if (itSub == obj.end())
                    continue;
                if (sub->required() && itSub->second.isNull())
                    throw std::runtime_error("Cannot null-out required field '" + fname + "." + subname + "'");
            }
        }
        else
        {
            if (fs->required() && v.isNull())
                throw std::runtime_error("Cannot null-out required field '" + fname + "'");
        }
    }
}

// Require all required fields/subfields when inserting a brand-new row.
// Also assert that the snapshot carries no _id (must be null/absent for auto-ID).
static void assertRequiredPresentForInsert(const NodeSchema *schema,
                                           const NodeSnapshot &node)

{
    for (const auto &kv : schema->fields())
    {
        const std::string &fname = kv.first;
        const FieldSchema *fs = kv.second.get();
        auto it = node.fields.find(fname);

        if (fs->type() == FieldType::Object)
        {
            const auto *of = dynamic_cast<const ObjectFieldSchema *>(fs);
            // object itself isn't required; its subfields may be
            for (const auto &sub : of->fields())
            {
                if (!sub->required())
                    continue;
                // must be present and non-null in node
                if (it == node.fields.end() || !it->second.isObject())
                    throw std::runtime_error("Missing required field '" + fname + "." + sub->name() + "'");
                const auto &obj = it->second.asObject();
                auto itSub = obj.find(sub->name());
                if (itSub == obj.end() || itSub->second.isNull())
                    throw std::runtime_error("Missing required field '" + fname + "." + sub->name() + "'");
            }
        }
        else
        {
            if (fs->required())
            {
                if (it == node.fields.end() || it->second.isNull())
                    throw std::runtime_error("Missing required field '" + fname + "'");
            }
        }
    }
}

// ---------------- RAII ----------------
struct SQLiteDataBackend::Stmt
{
    sqlite3 *db{};
    sqlite3_stmt *st{};

    Stmt(sqlite3 *d, const char *sql) : db(d)
    {
        if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK)
        {
            std::string msg = sqlite3_errmsg(db);
            throw std::runtime_error("sqlite prepare error: " + msg + " | SQL: " + std::string(sql));
        }
    }
    ~Stmt()
    {
        if (st)
            sqlite3_finalize(st);
    }

    // no copy
    Stmt(const Stmt &) = delete;
    Stmt &operator=(const Stmt &) = delete;
    // move
    Stmt(Stmt &&other) noexcept : db(other.db), st(other.st) { other.st = nullptr; }
    Stmt &operator=(Stmt &&other) noexcept
    {
        std::swap(db, other.db);
        std::swap(st, other.st);
        return *this;
    }
};

// ---------------- ctor/dtor ----------------
SQLiteDataBackend::SQLiteDataBackend(std::string db_path, bool recreate)
    : db_path_(std::move(db_path)), recreate_(recreate) {}

SQLiteDataBackend::~SQLiteDataBackend() { close(); }

void SQLiteDataBackend::open()
{
    if (db_)
        return;
    if (sqlite3_open(db_path_.c_str(), &db_) != SQLITE_OK)
    {
        std::string msg = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("sqlite open error: " + msg);
    }
    exec("PRAGMA foreign_keys=ON;");
}

void SQLiteDataBackend::close()
{
    if (db_)
    {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void SQLiteDataBackend::setReferenceChecksEnabled(bool enabled)
{
    open();
    if (enabled)
        exec("PRAGMA foreign_keys=ON;");
    else
        exec("PRAGMA foreign_keys=OFF;");
}

void SQLiteDataBackend::execOrIgnoreExists(const std::string &sql) const
{
    char *err = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK)
    {
        std::string msg = err ? err : "unknown";
        if (msg.find("already exists") != std::string::npos)
        {
            sqlite3_free(err);
            return;
        }
        sqlite3_free(err);
        throw std::runtime_error("sqlite exec error: " + msg + " | SQL: " + sql);
    }
}

void SQLiteDataBackend::exec(const std::string &sql) const
{
    char *err = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK)
    {
        std::string msg = err ? err : "unknown";
        sqlite3_free(err);
        throw std::runtime_error("sqlite exec error: " + msg + " | SQL: " + sql);
    }
}

// ---------------- naming helpers ----------------
std::string SQLiteDataBackend::flattenedObjectCol(const std::string &prefix, const std::string &sub)
{
    return joinName(prefix, sub);
}

// ---------------- init/tx ----------------
void SQLiteDataBackend::init(const SchemaRegistry &registry)
{
    open();
    registry_ = &registry;

    if (recreate_)
    {
        // Create all tables using your exporter; ignore "already exists"
        SQLiteSchemaExporter exp;
        const std::string ddl = exp.exportSchema(registry);
        // Split simple on ';' to execute piece by piece (avoid single large exec for better error msgs)
        std::string chunk;
        std::istringstream iss(ddl);
        while (std::getline(iss, chunk, ';'))
        {
            std::string sql = chunk;
            // trim
            auto notspace = [](int ch)
            { return !std::isspace(ch); };
            sql.erase(sql.begin(), std::find_if(sql.begin(), sql.end(), notspace));
            sql.erase(std::find_if(sql.rbegin(), sql.rend(), notspace).base(), sql.end());
            if (sql.empty())
                continue;
            execOrIgnoreExists(sql + ";");
        }
    }
}

void SQLiteDataBackend::begin() { exec("BEGIN;"); }
void SQLiteDataBackend::commit() { exec("COMMIT;"); }
void SQLiteDataBackend::rollback() { exec("ROLLBACK;"); }

// ---------------- column collection ----------------
void SQLiteDataBackend::collectMainColumns(
    const NodeSchema *schema,
    std::vector<std::pair<std::string, const FieldSchema *>> &cols,
    const std::string &prefix) const
{
    for (const auto &kv : schema->fields())
    {
        const std::string &name = kv.first;
        const FieldSchema *fs = kv.second.get();

        // skip arrays (side tables)
        if (fs->type() == FieldType::Array)
            continue;

        // full prefix path (used for recursion)
        const std::string fullName = joinName(prefix, name);

        if (fs->type() == FieldType::Object)
        {
            const auto *obj = dynamic_cast<const ObjectFieldSchema *>(fs);
            // recurse deeper — allow objects within objects
            collectMainColumns(obj, cols, fullName);
        }
        else
        {
            cols.emplace_back(fullName, fs);
        }
    }
}

void SQLiteDataBackend::collectMainColumns(
    const ObjectFieldSchema *obj,
    std::vector<std::pair<std::string, const FieldSchema *>> &cols,
    const std::string &prefix) const
{
    for (const auto &sub : obj->fields())
    {
        const std::string fullName = joinName(prefix, sub->name());
        const FieldSchema *fs = sub.get();

        if (fs->type() == FieldType::Array)
            continue;

        if (fs->type() == FieldType::Object)
        {
            const auto *subObj = dynamic_cast<const ObjectFieldSchema *>(fs);
            collectMainColumns(subObj, cols, fullName);
        }
        else
        {
            cols.emplace_back(fullName, fs);
        }
    }
}

// ---------------- binding helpers ----------------
void SQLiteDataBackend::bindFieldValue(sqlite3_stmt *st, int idx, const FieldValue &v)
{
    if (v.isString())
    {
        const auto &s = v.asString();

        // Treat empty strings as NULL to avoid FK errors and keep semantics consistent
        if (s.empty())
        {
            sqlite3_bind_null(st, idx);
        }
        else
        {
            sqlite3_bind_text(st, idx, s.c_str(), -1, SQLITE_TRANSIENT);
        }
    }
    else if (v.isInteger())
    {
        sqlite3_bind_int(st, idx, v.asInteger());
    }
    else if (v.isBoolean())
    {
        sqlite3_bind_int(st, idx, v.asBoolean() ? 1 : 0);
    }
    else if (v.isNull())
    {
        sqlite3_bind_null(st, idx);
    }
    else
    {
        // arrays/objects are not bound directly to main columns
        sqlite3_bind_null(st, idx);
    }
}

FieldValue SQLiteDataBackend::readColumn(sqlite3_stmt *st, int col, FieldType type)
{
    if (sqlite3_column_type(st, col) == SQLITE_NULL)
        return FieldValue{};
    switch (type)
    {
    case FieldType::String:
    case FieldType::Enum:
    case FieldType::Reference:
        return FieldValue(std::string(reinterpret_cast<const char *>(sqlite3_column_text(st, col))));
    case FieldType::Integer:
        return FieldValue(sqlite3_column_int(st, col));
    case FieldType::Boolean:
        return FieldValue(sqlite3_column_int(st, col) != 0);
    default:
        return FieldValue{};
    }
}

FieldValue SQLiteDataBackend::readColumnAuto(sqlite3_stmt *st, int col)
{
    int t = sqlite3_column_type(st, col);
    if (t == SQLITE_NULL)
        return FieldValue{};
    if (t == SQLITE_INTEGER)
        return FieldValue(sqlite3_column_int(st, col));
    if (t == SQLITE_TEXT)
        return FieldValue(std::string(reinterpret_cast<const char *>(sqlite3_column_text(st, col))));
    if (t == SQLITE_FLOAT)
        return FieldValue(static_cast<int>(sqlite3_column_double(st, col))); // not used
    // BLOB/others → skip
    return FieldValue{};
}

void SQLiteDataBackend::upsert(NodeSnapshot &node)
{
    const NodeSchema *schema = registry_->getSchema(node.key.profile);
    if (!schema)
        throw std::runtime_error("unknown profile in upsert: " + node.key.profile);

    const bool existsRow = exists(node.key);
    if (existsRow)
    {
        // PATCH semantics: don’t require all fields, but don’t allow explicit NULL to required ones.
        assertNoExplicitNullOnRequired(schema, node);
    }
    else
    {
        // CREATE semantics: all required fields must be present.
        assertRequiredPresentForInsert(schema, node);
    }

    // Collect main columns (flattened objects included)
    std::vector<std::pair<std::string, const FieldSchema *>> cols;
    collectMainColumns(schema, cols);

    auto collectPresent = [&](std::vector<std::string> &colNames,
                              std::vector<FieldValue> &values)
    {
        for (const auto &[col, fs] : cols)
        {
            FieldValue v;
            bool present = false;

            // (A) Try exact field name first (handles "project_name")
            if (auto itDirect = node.fields.find(col);
                itDirect != node.fields.end() && !itDirect->second.isObject())
            {
                v = itDirect->second;
                present = true;
            }
            else
            {
                // (B) Support multi-level flattened object subfields (e.g., "settings$network$ip")
                auto [top, sub] = splitTopSub(col);
                if (!sub.empty())
                {
                    if (const FieldSchema *topFs = schema->getField(top);
                        topFs && topFs->type() == FieldType::Object)
                    {
                        auto itTop = node.fields.find(top);
                        if (itTop != node.fields.end() && itTop->second.isObject())
                        {
                            // split subpath by '$' and walk the nested object
                            std::vector<std::string> parts;
                            {
                                std::istringstream ss(sub);
                                std::string token;
                                while (std::getline(ss, token, '$'))
                                    parts.push_back(token);
                            }

                            FieldValue leaf;
                            if (lookupNested(itTop->second.asObject(), parts, leaf))
                            {
                                v = leaf;       // may be null (explicit clear) or a primitive
                                present = true; // we will bind this column
                            }
                        }
                    }
                }
            }

            if (present)
            {
                colNames.push_back(col);
                values.push_back(v);
            }
        }
    };
    if (existsRow)
    {
        // ---------- UPDATE (patch) ----------
        std::vector<std::string> dynCols;
        std::vector<FieldValue> dynVals;
        collectPresent(dynCols, dynVals);

        // Only touch parent if provided
        const bool setParent = node.parent_id.has_value();

        // Preserve current _label only if caller didn't provide one (nullopt).
        // If provided (even empty string), use exactly what was given.
        std::optional<std::string> patchLabel = node.label;
        if (!patchLabel.has_value())
        {
            if (auto prev = fetch(node.key))
            {
                patchLabel = prev->label; // keep existing label (may still be null)
            }
        }

        std::ostringstream sql;
        sql << "UPDATE " << node.key.profile << " SET "
            << "_label = ?";

        if (setParent)
            sql << ", _parent_id = ?";

        for (const auto &c : dynCols)
            sql << ", " << c << " = ?";

        sql << " WHERE _id = ?;";

        Stmt st(db_, sql.str().c_str());
        int idx = 1;

        // _label: bind provided value; if none and no previous, bind NULL
        if (patchLabel.has_value())
            bindFieldValue(st.st, idx++, FieldValue(*patchLabel));
        else
            sqlite3_bind_null(st.st, idx++);

        if (setParent)
        {
            if (node.parent_id && !node.parent_id->empty())
                bindFieldValue(st.st, idx++, FieldValue(*node.parent_id));
            else
                sqlite3_bind_null(st.st, idx++);
        }

        for (const auto &v : dynVals)
            bindFieldValue(st.st, idx++, v);

        bindFieldValue(st.st, idx++, FieldValue(node.key.id.value()));

        if (sqlite3_step(st.st) != SQLITE_DONE)
            throw std::runtime_error("sqlite update failed: " + std::string(sqlite3_errmsg(db_)));
    }
    else
    {
        // ---------- INSERT (create) ----------
        if (!node.key.id.has_value())
            node.key.id = generateUuid();
        std::vector<std::string> insCols = {"_id", "_label", "_parent_id", "_parent_profile"};

        std::vector<std::string> dynCols;
        std::vector<FieldValue> dynVals;
        collectPresent(dynCols, dynVals);
        insCols.insert(insCols.end(), dynCols.begin(), dynCols.end());

        std::ostringstream sql;
        sql << "INSERT INTO " << node.key.profile << " (";
        for (size_t i = 0; i < insCols.size(); ++i)
        {
            if (i)
                sql << ",";
            sql << insCols[i];
        }
        sql << ") VALUES (";
        for (size_t i = 0; i < insCols.size(); ++i)
        {
            if (i)
                sql << ",";
            sql << "?";
        }
        sql << ");";

        Stmt st(db_, sql.str().c_str());
        int idx = 1;

        // Reserved columns first
        if (node.key.id.has_value())
            bindFieldValue(st.st, idx++, FieldValue(*node.key.id)); // _id
        else
            sqlite3_bind_null(st.st, idx++); // _id (null if auto-id to be generated upstream)

        // _label — use provided value, else fallback to UUID
        if (node.label.has_value())
        {
            bindFieldValue(st.st, idx++, FieldValue(*node.label));
        }
        else
        {
            node.label = *node.key.id; // default label = UUID
            bindFieldValue(st.st, idx++, FieldValue(*node.label));
        }

        // _parent_id — bind NULL when not provided or empty
        if (!node.parent_id.has_value() || node.parent_id->empty())
            sqlite3_bind_null(st.st, idx++);
        else
            bindFieldValue(st.st, idx++, FieldValue(*node.parent_id));

        // --- _parent_profile: backend-owned value ---
        // Ignore whatever the caller may have put and compute from registry.
        {
            const auto derived = deriveParentProfile(registry_, node.key.profile);
            if (derived.has_value())
                bindFieldValue(st.st, idx++, FieldValue(*derived)); // child -> NOT NULL
            else
                sqlite3_bind_null(st.st, idx++); // root -> NULL
        }

        // Dynamic fields after reserved
        for (const auto &v : dynVals)
            bindFieldValue(st.st, idx++, v);

        if (sqlite3_step(st.st) != SQLITE_DONE)
            throw std::runtime_error("sqlite insert failed: " + std::string(sqlite3_errmsg(db_)));
    }

    // ---------- Arrays: rewrite only if present in snapshot ----------
    for (const auto &kv : schema->fields())
    {
        const std::string &fname = kv.first;
        const FieldSchema *fs = kv.second.get();
        if (fs->type() != FieldType::Array)
            continue;

        if (node.fields.find(fname) == node.fields.end())
            continue; // absent → leave as-is

        const auto *arr = dynamic_cast<const ArrayFieldSchema *>(fs);

        // purge existing
        {
            std::ostringstream del;
            del << "DELETE FROM " << node.key.profile << "$" << fname
                << " WHERE " << node.key.profile << "_id = ?;";
            Stmt d(db_, del.str().c_str());
            sqlite3_bind_text(d.st, 1, node.key.id->c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(d.st) != SQLITE_DONE)
                throw std::runtime_error("sqlite delete array failed: " + std::string(sqlite3_errmsg(db_)));
        }
        // write fresh
        writeArrayField(node, fname, arr);
    }

    // ---------- Nested object arrays: rewrite only if present ----------
    for (const auto &kv : schema->fields())
    {
        const std::string &fname = kv.first;
        const FieldSchema *fs = kv.second.get();
        if (fs->type() != FieldType::Object)
            continue;

        const auto *obj = dynamic_cast<const ObjectFieldSchema *>(fs);
        for (const auto &sub : obj->fields())
        {
            if (sub->type() != FieldType::Array)
                continue;

            const auto *subArr = dynamic_cast<const ArrayFieldSchema *>(sub.get());
            const std::string nestedName = joinName(fname, sub->name());

            // Check if user provided this nested array in snapshot
            auto itTop = node.fields.find(fname);
            if (itTop == node.fields.end() || !itTop->second.isObject())
                continue;

            const auto &objVal = itTop->second.asObject();
            auto itArr = objVal.find(sub->name());
            if (itArr == objVal.end() || !itArr->second.isArray())
                continue;

            // Purge existing rows
            {
                std::ostringstream del;
                del << "DELETE FROM " << node.key.profile << "$" << nestedName
                    << " WHERE " << node.key.profile << "_id = ?;";
                Stmt d(db_, del.str().c_str());
                sqlite3_bind_text(d.st, 1, node.key.id->c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(d.st) != SQLITE_DONE)
                    throw std::runtime_error("sqlite delete nested array failed: " +
                                             std::string(sqlite3_errmsg(db_)));
            }

            // Write new array contents
            NodeSnapshot tmp = node;
            tmp.fields.clear();
            tmp.fields[nestedName] = itArr->second;
            writeArrayField(tmp, nestedName, subArr);
        }
    }
}

void SQLiteDataBackend::remove(const NodeKey &key)
{
    if (!key.id.has_value())
        throw std::runtime_error("remove() requires a concrete '_id'");

    // No BEGIN/COMMIT/ROLLBACK here — caller controls the transaction.

    // 1) Recursively delete polymorphic children first
    for (const auto &kv : registry_->schemas())
    {
        const std::string &childProfile = kv.first;
        if (childProfile == key.profile)
            continue;

        std::ostringstream findSql;
        findSql << "SELECT _id FROM " << childProfile
                << " WHERE _parent_profile = ? AND _parent_id = ?;";

        Stmt findSt(db_, findSql.str().c_str());
        sqlite3_bind_text(findSt.st, 1, key.profile.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(findSt.st, 2, key.id->c_str(), -1, SQLITE_TRANSIENT);

        std::vector<NodeKey> childKeys;
        while (sqlite3_step(findSt.st) == SQLITE_ROW)
        {
            const unsigned char *p = sqlite3_column_text(findSt.st, 0);
            if (p)
                childKeys.push_back(NodeKey{childProfile, std::string(reinterpret_cast<const char *>(p))});
        }

        for (const auto &ck : childKeys)
            remove(ck); // recursive and still transaction-free
    }

    // 2) Delete this node
    std::ostringstream del;
    del << "DELETE FROM " << key.profile << " WHERE _id = ?;";
    Stmt st(db_, del.str().c_str());
    sqlite3_bind_text(st.st, 1, key.id->c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st.st) != SQLITE_DONE)
        throw std::runtime_error("sqlite remove failed: " + std::string(sqlite3_errmsg(db_)));
}

// ---------------- exists/fetch/parent/children ----------------
bool SQLiteDataBackend::exists(const NodeKey &key) const
{
    if (!key.id.has_value())
        return false;
    std::ostringstream sql;
    sql << "SELECT 1 FROM " << key.profile << " WHERE _id=? LIMIT 1;";
    Stmt st(db_, sql.str().c_str());
    sqlite3_bind_text(st.st, 1, key.id->c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st.st);
    return rc == SQLITE_ROW;
}

std::optional<std::string> SQLiteDataBackend::parentOf(const NodeKey &key) const
{
    if (!key.id.has_value())
        throw std::runtime_error("parentOf() requires a concrete '_id'");
    std::ostringstream sql;
    sql << "SELECT _parent_id FROM " << key.profile << " WHERE _id=?;";
    Stmt st(db_, sql.str().c_str());
    sqlite3_bind_text(st.st, 1, key.id->c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st.st) == SQLITE_ROW)
    {
        if (sqlite3_column_type(st.st, 0) == SQLITE_NULL)
            return std::nullopt;
        return std::string(reinterpret_cast<const char *>(sqlite3_column_text(st.st, 0)));
    }
    return std::nullopt;
}

// ---------------- exists/fetch/parent/children ----------------
std::optional<NodeSnapshot> SQLiteDataBackend::fetch(const NodeKey &key) const
{
    if (!key.id.has_value())
        throw std::runtime_error("fetch() requires a concrete '_id'");
    const NodeSchema *schema = registry_->getSchema(key.profile);
    if (!schema)
        return std::nullopt;

    // Select all main columns we know about
    std::vector<std::pair<std::string, const FieldSchema *>> cols;
    collectMainColumns(schema, cols);

    // Build set of object-tops to interpret flattened object columns
    std::unordered_set<std::string> objectTops;
    for (const auto &kv : schema->fields())
        if (kv.second->type() == FieldType::Object)
            objectTops.insert(kv.first);

    std::ostringstream sql;
    sql << "SELECT _label,_parent_id";
    for (const auto &[c, _] : cols)
        sql << "," << c;
    sql << " FROM " << key.profile << " WHERE _id=?;";
    Stmt st(db_, sql.str().c_str());
    sqlite3_bind_text(st.st, 1, key.id->c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(st.st) != SQLITE_ROW)
        return std::nullopt;

    NodeSnapshot snap;
    snap.key = key;
    snap.label = (sqlite3_column_type(st.st, 0) == SQLITE_NULL) ? std::string()
                                                                : std::string(reinterpret_cast<const char *>(sqlite3_column_text(st.st, 0)));
    snap.parent_id = (sqlite3_column_type(st.st, 1) == SQLITE_NULL) ? std::string()
                                                                    : std::string(reinterpret_cast<const char *>(sqlite3_column_text(st.st, 1)));

    // Reconstruct fields
    int base = 2;
    for (size_t i = 0; i < cols.size(); ++i)
    {
        const auto &[c, fs] = cols[i];
        FieldValue v = readColumn(st.st, base + static_cast<int>(i), fs->type());
        if (v.isNull())
            continue;

        // Support multi-level flattened names (like settings$network$ip)
        std::vector<std::string> parts;
        std::istringstream ss(c);
        std::string token;
        while (std::getline(ss, token, '$'))
            parts.push_back(token);

        if (parts.size() > 1)
        {
            // Ensure top-level object exists
            FieldValue &topVal = snap.fields[parts[0]];
            if (!topVal.isObject())
                topVal = ObjectData{};
            insertNestedObject(topVal.asObject(),
                               std::vector<std::string>(parts.begin() + 1, parts.end()), v);
        }
        else
        {
            snap.fields[c] = v;
        }
    }

    // ---- Arrays: read for every array field (including those nested in objects)
    for (const auto &kv : schema->fields())
    {
        const std::string &fname = kv.first;
        const FieldSchema *fs = kv.second.get();

        if (fs->type() == FieldType::Array)
        {
            // Top-level array
            const auto *arr = dynamic_cast<const ArrayFieldSchema *>(fs);
            snap.fields[fname] = readArrayField(key, key.profile, fname, arr);
        }
        else if (fs->type() == FieldType::Object)
        {
            // Object containing possible array subfields
            const auto *obj = dynamic_cast<const ObjectFieldSchema *>(fs);
            for (const auto &sub : obj->fields())
            {
                if (sub->type() != FieldType::Array)
                    continue;

                const auto *subArr = dynamic_cast<const ArrayFieldSchema *>(sub.get());
                FieldValue arrVal = readArrayField(key, key.profile, fname + "$" + sub->name(), subArr);

                // Ensure parent object exists, even if only array part is present
                ObjectData &objVal = snap.fields[fname].isObject()
                                         ? snap.fields[fname].asObject()
                                         : (snap.fields[fname] = ObjectData{}, snap.fields[fname].asObject());
                objVal[sub->name()] = arrVal;
            }
        }
    }

    return snap;
}

std::vector<NodeKey> SQLiteDataBackend::childrenOf(const NodeKey &parent,
                                                   const std::string &childProfile) const
{
    if (!parent.id.has_value())
        throw std::runtime_error("childrenOf() requires a concrete '_id'");
    // In your design, child rows store _parent_id = parent._id
    std::ostringstream sql;
    sql << "SELECT _id FROM " << childProfile << " WHERE _parent_id = ?;";
    Stmt st(db_, sql.str().c_str());
    sqlite3_bind_text(st.st, 1, parent.id->c_str(), -1, SQLITE_TRANSIENT);

    std::vector<NodeKey> out;
    while (sqlite3_step(st.st) == SQLITE_ROW)
    {
        out.push_back(NodeKey{childProfile,
                              std::string(reinterpret_cast<const char *>(sqlite3_column_text(st.st, 0)))});
    }
    return out;
}

// ---------------- arrays read/write ----------------
void SQLiteDataBackend::writeArrayField(const NodeSnapshot &node,
                                        const std::string &fname,
                                        const ArrayFieldSchema *arr) const
{
    if (!node.key.id.has_value())
        throw std::runtime_error("writeArrayField() requires a concrete '_id'");
    const std::string table = node.key.profile + "$" + fname;
    const FieldSchema *items = arr->items();

    auto it = node.fields.find(fname);
    if (it == node.fields.end() || !it->second.isArray())
        return;
    const auto &values = it->second.asArray();

    if (items->type() == FieldType::String ||
        items->type() == FieldType::Integer ||
        items->type() == FieldType::Boolean ||
        items->type() == FieldType::Enum)
    {
        std::ostringstream ins;
        ins << "INSERT INTO " << table << "(" << node.key.profile << "_id,value) VALUES (?,?);";
        for (const auto &v : values)
        {
            Stmt st(db_, ins.str().c_str());
            sqlite3_bind_text(st.st, 1, node.key.id->c_str(), -1, SQLITE_TRANSIENT);
            bindFieldValue(st.st, 2, v);
            if (sqlite3_step(st.st) != SQLITE_DONE)
            {
                throw std::runtime_error("sqlite insert array value failed: " + std::string(sqlite3_errmsg(db_)));
            }
        }
        return;
    }

    if (items->type() == FieldType::Reference)
    {
        auto ref = dynamic_cast<const ReferenceFieldSchema *>(items);
        const std::string valueCol = refArrayValueCol(node.key.profile, fname, ref->target());

        std::ostringstream ins;
        ins << "INSERT INTO " << table << "("
            << node.key.profile << "_id," << valueCol << ") VALUES (?,?);";

        for (const auto &v : values)
        {
            // Only strings are valid for reference IDs
            if (!v.isString())
                continue;

            Stmt st(db_, ins.str().c_str());
            sqlite3_bind_text(st.st, 1, node.key.id->c_str(), -1, SQLITE_TRANSIENT);

            const std::string &rid = v.asString();

            // 🧠 Normalize empty string to NULL
            if (rid.empty())
                sqlite3_bind_null(st.st, 2);
            else
                sqlite3_bind_text(st.st, 2, rid.c_str(), -1, SQLITE_TRANSIENT);

            if (sqlite3_step(st.st) != SQLITE_DONE)
            {
                throw std::runtime_error(
                    "sqlite insert ref array failed: " + std::string(sqlite3_errmsg(db_)));
            }
        }
        return;
    }

    if (items->type() == FieldType::Object)
    {
        auto obj = dynamic_cast<const ObjectFieldSchema *>(items);
        // build INSERT with all flattened columns
        // columns: owner_id + flattened object subfields
        std::vector<std::pair<std::string, const FieldSchema *>> subcols;
        for (const auto &sub : obj->fields())
        {
            subcols.emplace_back(sub->name(), sub.get());
        }

        std::ostringstream ins;
        ins << "INSERT INTO " << table << "(" << node.key.profile << "_id";
        for (auto &[cn, _] : subcols)
            ins << "," << cn;
        ins << ") VALUES (?";
        for (size_t i = 0; i < subcols.size(); ++i)
            ins << ",?";
        ins << ");";

        for (const auto &v : values)
        {
            if (!v.isObject())
                continue;
            const auto &o = v.asObject();

            Stmt st(db_, ins.str().c_str());
            int idx = 1;
            sqlite3_bind_text(st.st, idx++, node.key.id->c_str(), -1, SQLITE_TRANSIENT);

            for (auto &[cn, fs] : subcols)
            {
                auto it2 = o.find(cn);
                if (it2 == o.end())
                {
                    sqlite3_bind_null(st.st, idx++);
                    continue;
                }
                bindFieldValue(st.st, idx++, it2->second);
            }

            if (sqlite3_step(st.st) != SQLITE_DONE)
            {
                throw std::runtime_error("sqlite insert object array failed: " + std::string(sqlite3_errmsg(db_)));
            }
        }
        return;
    }
}

FieldValue SQLiteDataBackend::readArrayField(const NodeKey &key,
                                             const std::string &owner,
                                             const std::string &fieldName,
                                             const ArrayFieldSchema *arr) const
{
    if (!key.id.has_value())
        throw std::runtime_error("readArrayField() requires a concrete '_id'");
    const std::string table = owner + "$" + fieldName;
    const FieldSchema *items = arr->items();
    ArrayData out;

    if (items->type() == FieldType::Reference)
    {
        auto ref = dynamic_cast<const ReferenceFieldSchema *>(items);
        const std::string valueCol = refArrayValueCol(owner, fieldName, ref->target());

        std::ostringstream sql;
        sql << "SELECT " << valueCol << " FROM " << table
            << " WHERE " << owner << "_id=? AND " << valueCol << " IS NOT NULL"
            << " ORDER BY id;";

        Stmt st(db_, sql.str().c_str());
        sqlite3_bind_text(st.st, 1, key.id->c_str(), -1, SQLITE_TRANSIENT);

        while (sqlite3_step(st.st) == SQLITE_ROW)
        {
            const unsigned char *p = sqlite3_column_text(st.st, 0);
            if (!p)
                continue; // defensive
            out.emplace_back(std::string(reinterpret_cast<const char *>(p)));
        }
        return FieldValue(out);
    }

    if (items->type() == FieldType::Object)
    {
        auto obj = dynamic_cast<const ObjectFieldSchema *>(items);
        // SELECT every sub column
        std::ostringstream sql;
        sql << "SELECT ";
        bool first = true;
        for (const auto &sub : obj->fields())
        {
            if (!first)
                sql << ",";
            first = false;
            sql << sub->name();
        }
        sql << " FROM " << table << " WHERE " << owner << "_id=? ORDER BY id;";
        Stmt st(db_, sql.str().c_str());
        sqlite3_bind_text(st.st, 1, key.id->c_str(), -1, SQLITE_TRANSIENT);

        while (sqlite3_step(st.st) == SQLITE_ROW)
        {
            ObjectData row;
            int col = 0;
            for (const auto &sub : obj->fields())
            {
                FieldValue v = readColumn(st.st, col++, sub->type());
                if (!v.isNull())
                    row[sub->name()] = v;
            }
            out.emplace_back(row);
        }
        return FieldValue(out);
    }

    // primitives / enum
    {
        std::ostringstream sql;
        sql << "SELECT value FROM " << table << " WHERE " << owner << "_id=? ORDER BY id;";
        Stmt st(db_, sql.str().c_str());
        sqlite3_bind_text(st.st, 1, key.id->c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(st.st) == SQLITE_ROW)
        {
            out.emplace_back(readColumnAuto(st.st, 0));
        }
        return FieldValue(out);
    }
}

// ---------------- refs ----------------
std::vector<NodeKey> SQLiteDataBackend::refsFrom(const NodeKey &key,
                                                 const std::string &field) const
{
    if (!key.id.has_value())
        throw std::runtime_error("refsFrom() requires a concrete '_id'");
    const NodeSchema *schema = registry_->getSchema(key.profile);
    if (!schema)
        return {};

    std::vector<NodeKey> out;

    // Support dotted paths: e.g., "sys_requirement_ref.references"
    std::string top = field;
    std::string sub;
    auto dot = field.find('.');
    if (dot != std::string::npos)
    {
        top = field.substr(0, dot);
        sub = field.substr(dot + 1);
    }

    const FieldSchema *fs = schema->getField(top);
    if (!fs)
        return out;

    // --- 1) Single reference field on main table
    if (fs->type() == FieldType::Reference)
    {
        auto ref = dynamic_cast<const ReferenceFieldSchema *>(fs);
        std::ostringstream sql;
        sql << "SELECT " << field << " FROM " << key.profile << " WHERE _id=?;";
        Stmt st(db_, sql.str().c_str());
        sqlite3_bind_text(st.st, 1, key.id->c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st.st) == SQLITE_ROW && sqlite3_column_type(st.st, 0) != SQLITE_NULL)
        {
            const unsigned char *p = sqlite3_column_text(st.st, 0);
            if (p)
                out.push_back(NodeKey{ref->target(), std::string(reinterpret_cast<const char *>(p))});
        }
        return out;
    }

    // --- 2) Object field, possibly containing a reference or array<reference>
    // --- 2) Object field, possibly containing a reference or array<reference>
    if (fs->type() == FieldType::Object)
    {
        const auto *obj = dynamic_cast<const ObjectFieldSchema *>(fs);

        // ---- Case A: user passed dotted path (legacy behaviour) ----
        if (!sub.empty())
        {
            const FieldSchema *subf = obj->getField(sub);
            if (!subf)
                return out;

            if (subf->type() == FieldType::Reference)
            {
                auto ref = dynamic_cast<const ReferenceFieldSchema *>(subf);
                const std::string col = joinName(top, sub);
                std::ostringstream sql;
                sql << "SELECT " << col << " FROM " << key.profile << " WHERE _id=?;";
                Stmt st(db_, sql.str().c_str());
                sqlite3_bind_text(st.st, 1, key.id->c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(st.st) == SQLITE_ROW && sqlite3_column_type(st.st, 0) != SQLITE_NULL)
                {
                    const unsigned char *p = sqlite3_column_text(st.st, 0);
                    if (p)
                        out.push_back(NodeKey{ref->target(), std::string(reinterpret_cast<const char *>(p))});
                }
                return out;
            }

            if (subf->type() == FieldType::Array)
            {
                const auto *arr = dynamic_cast<const ArrayFieldSchema *>(subf);
                if (arr->items()->type() != FieldType::Reference)
                    return out;
                const auto *ref = dynamic_cast<const ReferenceFieldSchema *>(arr->items());
                const std::string table = key.profile + "$" + joinName(top, sub);
                const std::string valueCol = refArrayValueCol(key.profile, joinName(top, sub), ref->target());
                std::ostringstream sql;
                sql << "SELECT " << valueCol
                    << " FROM " << table
                    << " WHERE " << key.profile << "_id=? AND " << valueCol << " IS NOT NULL;";
                Stmt st(db_, sql.str().c_str());
                sqlite3_bind_text(st.st, 1, key.id->c_str(), -1, SQLITE_TRANSIENT);
                while (sqlite3_step(st.st) == SQLITE_ROW)
                {
                    if (sqlite3_column_type(st.st, 0) == SQLITE_NULL)
                        continue;
                    const unsigned char *p = sqlite3_column_text(st.st, 0);
                    if (p)
                        out.push_back(NodeKey{ref->target(), std::string(reinterpret_cast<const char *>(p))});
                }
                return out;
            }
        }

        // ---- Case B: auto-recursion — collect all sub-refs inside object ----
        for (const auto &subf : obj->fields())
        {
            if (subf->type() == FieldType::Reference)
            {
                auto ref = dynamic_cast<const ReferenceFieldSchema *>(subf.get());
                const std::string col = joinName(top, subf->name());
                std::ostringstream sql;
                sql << "SELECT " << col << " FROM " << key.profile << " WHERE _id=?;";
                Stmt st(db_, sql.str().c_str());
                sqlite3_bind_text(st.st, 1, key.id->c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(st.st) == SQLITE_ROW && sqlite3_column_type(st.st, 0) != SQLITE_NULL)
                {
                    const unsigned char *p = sqlite3_column_text(st.st, 0);
                    if (p)
                        out.push_back(NodeKey{ref->target(), std::string(reinterpret_cast<const char *>(p))});
                }
            }
            else if (subf->type() == FieldType::Array)
            {
                const auto *arr = dynamic_cast<const ArrayFieldSchema *>(subf.get());
                if (!arr || arr->items()->type() != FieldType::Reference)
                    continue;

                const auto *ref = dynamic_cast<const ReferenceFieldSchema *>(arr->items());
                const std::string table = key.profile + "$" + joinName(top, subf->name());
                const std::string valueCol = refArrayValueCol(key.profile, joinName(top, subf->name()), ref->target());
                std::ostringstream sql;
                sql << "SELECT " << valueCol
                    << " FROM " << table
                    << " WHERE " << key.profile << "_id=? AND " << valueCol << " IS NOT NULL;";
                Stmt st(db_, sql.str().c_str());
                sqlite3_bind_text(st.st, 1, key.id->c_str(), -1, SQLITE_TRANSIENT);
                while (sqlite3_step(st.st) == SQLITE_ROW)
                {
                    if (sqlite3_column_type(st.st, 0) == SQLITE_NULL)
                        continue;
                    const unsigned char *p = sqlite3_column_text(st.st, 0);
                    if (p)
                        out.push_back(NodeKey{ref->target(), std::string(reinterpret_cast<const char *>(p))});
                }
            }
        }
        return out;
    }

    // --- 3) Array<reference> (top-level)
    if (fs->type() == FieldType::Array)
    {
        auto arr = dynamic_cast<const ArrayFieldSchema *>(fs);
        if (arr->items()->type() == FieldType::Reference)
        {
            auto ref = dynamic_cast<const ReferenceFieldSchema *>(arr->items());
            const std::string table = key.profile + "$" + field;
            const std::string valueCol = refArrayValueCol(key.profile, field, ref->target());

            std::ostringstream sql;
            sql << "SELECT " << valueCol
                << " FROM " << table
                << " WHERE " << key.profile << "_id=? AND " << valueCol << " IS NOT NULL;";
            Stmt st(db_, sql.str().c_str());
            sqlite3_bind_text(st.st, 1, key.id->c_str(), -1, SQLITE_TRANSIENT);

            while (sqlite3_step(st.st) == SQLITE_ROW)
            {
                if (sqlite3_column_type(st.st, 0) == SQLITE_NULL)
                    continue;
                const unsigned char *p = sqlite3_column_text(st.st, 0);
                if (!p)
                    continue;
                out.push_back(NodeKey{ref->target(), std::string(reinterpret_cast<const char *>(p))});
            }
        }
    }

    // --- 4) Array<object> possibly containing reference subfields ---
    if (fs->type() == FieldType::Array)
    {
        const auto *arr = dynamic_cast<const ArrayFieldSchema *>(fs);
        if (!arr || arr->items()->type() != FieldType::Object)
            return out;

        const auto *obj = dynamic_cast<const ObjectFieldSchema *>(arr->items());
        if (!obj)
            return out;

        // For each subfield inside the object item
        for (const auto &subf : obj->fields())
        {
            if (subf->type() == FieldType::Reference)
            {
                const auto *ref = dynamic_cast<const ReferenceFieldSchema *>(subf.get());
                const std::string table = key.profile + "$" + top;
                const std::string valueCol = subf->name(); // subfield name = column name
                std::ostringstream sql;
                sql << "SELECT " << valueCol
                    << " FROM " << table
                    << " WHERE " << key.profile << "_id=? AND " << valueCol << " IS NOT NULL;";
                Stmt st(db_, sql.str().c_str());
                sqlite3_bind_text(st.st, 1, key.id->c_str(), -1, SQLITE_TRANSIENT);
                while (sqlite3_step(st.st) == SQLITE_ROW)
                {
                    if (sqlite3_column_type(st.st, 0) == SQLITE_NULL)
                        continue;
                    const unsigned char *p = sqlite3_column_text(st.st, 0);
                    if (p)
                        out.push_back(NodeKey{ref->target(), std::string(reinterpret_cast<const char *>(p))});
                }
            }
        }
        return out;
    }
    return out;
}

void SQLiteDataBackend::collectRefsToRecursive(
    const std::string &owner,
    const std::string &prefix,
    const FieldSchema *fs,
    const NodeKey &key,
    std::vector<NodeKey> &out) const
{
    // --- Case 1: direct Reference (main table only) ---
    if (fs->type() == FieldType::Reference)
    {
        // Ignore dotted prefixes (those belong to side-tables)
        if (prefix.find('.') != std::string::npos)
            return;

        auto ref = dynamic_cast<const ReferenceFieldSchema *>(fs);
        if (!ref || ref->target() != key.profile)
            return;

        std::ostringstream sql;
        sql << "SELECT _id FROM " << owner << " WHERE " << prefix << "=?;";
        Stmt st(db_, sql.str().c_str());
        sqlite3_bind_text(st.st, 1, key.id->c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(st.st) == SQLITE_ROW)
        {
            const unsigned char *p = sqlite3_column_text(st.st, 0);
            if (p)
                out.push_back(NodeKey{owner, std::string(reinterpret_cast<const char *>(p))});
        }
        return;
    }

    // --- Case 2: Array (reference or object) ---
    if (fs->type() == FieldType::Array)
    {
        const auto *arr = dynamic_cast<const ArrayFieldSchema *>(fs);
        if (!arr)
            return;

        // ---- (a) array<reference> ----
        if (arr->items()->type() == FieldType::Reference)
        {
            const auto *ref = dynamic_cast<const ReferenceFieldSchema *>(arr->items());
            if (!ref || ref->target() != key.profile)
                return;

            const std::string table = owner + "$" + prefix;
            // ✅ FIX: use the profile of the *target key* to get the correct column name
            const std::string valueCol = refArrayValueCol(owner, prefix, key.profile);

            std::ostringstream sql;
            sql << "SELECT " << owner << "_id FROM " << table
                << " WHERE " << valueCol << " = ?;";
            Stmt st(db_, sql.str().c_str());
            sqlite3_bind_text(st.st, 1, key.id->c_str(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(st.st) == SQLITE_ROW)
            {
                const unsigned char *p = sqlite3_column_text(st.st, 0);
                if (p)
                    out.push_back(NodeKey{owner, std::string(reinterpret_cast<const char *>(p))});
            }
            return;
        }

        // ---- (b) array<object> possibly containing references ----
        if (arr->items()->type() == FieldType::Object)
        {
            const auto *obj = dynamic_cast<const ObjectFieldSchema *>(arr->items());
            if (!obj)
                return;

            for (const auto &sub : obj->fields())
            {
                if (sub->type() == FieldType::Reference)
                {
                    const auto *ref = dynamic_cast<const ReferenceFieldSchema *>(sub.get());
                    if (!ref || ref->target() != key.profile)
                        continue;

                    const std::string table = owner + "$" + prefix; // e.g. report$sections
                    const std::string valueCol = sub->name();       // e.g. main_ref
                    std::ostringstream sql;
                    sql << "SELECT " << owner << "_id FROM " << table
                        << " WHERE " << valueCol << " = ?;";
                    Stmt st(db_, sql.str().c_str());
                    sqlite3_bind_text(st.st, 1, key.id->c_str(), -1, SQLITE_TRANSIENT);
                    while (sqlite3_step(st.st) == SQLITE_ROW)
                    {
                        const unsigned char *p = sqlite3_column_text(st.st, 0);
                        if (p)
                            out.push_back(NodeKey{owner, std::string(reinterpret_cast<const char *>(p))});
                    }

                    // 🚫 Do NOT recurse into Reference fields
                    continue;
                }

                // recurse only for non-Reference subfields
                collectRefsToRecursive(owner, joinName(prefix, sub->name()), sub.get(), key, out);
            }
            return;
        }

        // otherwise: array of primitives → skip
        return;
    }

    // --- Case 3: object → recurse into subfields ---
    if (fs->type() == FieldType::Object)
    {
        const auto *obj = dynamic_cast<const ObjectFieldSchema *>(fs);
        for (const auto &sub : obj->fields())
        {
            collectRefsToRecursive(owner, joinName(prefix, sub->name()), sub.get(), key, out);
        }
    }
}
std::vector<NodeKey> SQLiteDataBackend::refsTo(const NodeKey &key) const
{
    if (!key.id.has_value())
        throw std::runtime_error("refsTo() requires a concrete '_id'");
    std::vector<NodeKey> out;

    for (const auto &kvp : registry_->schemas())
    {
        const std::string &owner = kvp.first;
        const NodeSchema *s = kvp.second.get();

        for (const auto &fk : s->fields())
        {
            const std::string &fname = fk.first;
            const FieldSchema *fs = fk.second.get();
            collectRefsToRecursive(owner, fname, fs, key, out);
        }
    }

    return out;
}

// ---------------- QueryDNF ----------------
SQLiteDataBackend::WhereSQL
SQLiteDataBackend::buildWhereFilter(const NodeSchema *schema, const FieldFilter &f) const
{
    // path may be "a" or "obj.sub" or an array field name "tags"
    // For arrays: CONTAINS uses EXISTS subquery.
    WhereSQL w;

    // Find field in schema (top-level)
    std::string top = f.field;
    std::string sub;
    auto dot = f.field.find('.');
    if (dot != std::string::npos)
    {
        top = f.field.substr(0, dot);
        sub = f.field.substr(dot + 1);
    }

    const FieldSchema *fs = schema->getField(top);
    if (!fs)
    {
        w.sql = "1=0"; // unknown field never matches
        return w;
    }

    auto opToStr = [&](FieldOp op) -> const char *
    {
        switch (op)
        {
        case FieldOp::EQ:
            return "=";
        case FieldOp::NEQ:
            return "!=";
        case FieldOp::GT:
            return ">";
        case FieldOp::GTE:
            return ">=";
        case FieldOp::LT:
            return "<";
        case FieldOp::LTE:
            return "<=";
        default:
            return nullptr;
        }
    };

    // --- Support object/array nesting ---
    if (fs->type() == FieldType::Object && !sub.empty())
    {
        const auto *obj = dynamic_cast<const ObjectFieldSchema *>(fs);
        if (!obj)
            return w;

        // --- NEW: object.array<object> with comparisons on a subfield (meta.subitems.name etc.) ---
        auto dot2 = sub.find('.');
        if (dot2 != std::string::npos)
        {
            std::string arrName = sub.substr(0, dot2);
            std::string rest = sub.substr(dot2 + 1);

            const FieldSchema *arrFs = obj->getField(arrName);
            if (arrFs && arrFs->type() == FieldType::Array)
            {
                const auto *arr = dynamic_cast<const ArrayFieldSchema *>(arrFs);
                if (arr && arr->items()->type() == FieldType::Object)
                {
                    const std::string tbl = schema->profileName() + "$" + top + "$" + arrName;
                    const std::string ownerCol = schema->profileName() + "_id";
                    const std::string col = rest;

                    auto addLike = [&](bool prefix)
                    {
                        w.sql = "EXISTS (SELECT 1 FROM " + tbl + " sub WHERE sub." + ownerCol +
                                " = t._id AND sub." + col + " LIKE ?)";
                        if (f.value.isString())
                            w.params.emplace_back(prefix ? (f.value.asString() + "%")
                                                         : ("%" + f.value.asString() + "%"));
                        else
                            w.params.emplace_back(std::string("%"));
                    };

                    switch (f.op)
                    {
                    case FieldOp::EQ:
                    case FieldOp::NEQ:
                    case FieldOp::GT:
                    case FieldOp::GTE:
                    case FieldOp::LT:
                    case FieldOp::LTE:
                    {
                        const char *opstr = opToStr(f.op);
                        w.sql = "EXISTS (SELECT 1 FROM " + tbl + " sub WHERE sub." + ownerCol +
                                " = t._id AND sub." + col + " " + opstr + " ?)";
                        w.params.push_back(f.value);
                        return w;
                    }
                    case FieldOp::PREFIX:
                        addLike(true);
                        return w;
                    case FieldOp::CONTAINS:
                        addLike(false);
                        return w;
                    case FieldOp::IN:
                    case FieldOp::NOT_IN:
                    {
                        if (!f.value.isArray())
                        {
                            w.sql = "1=0";
                            return w;
                        }
                        const auto &arrVals = f.value.asArray();
                        if (arrVals.empty())
                        {
                            w.sql = (f.op == FieldOp::IN ? "1=0" : "1=1");
                            return w;
                        }
                        std::ostringstream ph;
                        for (size_t i = 0; i < arrVals.size(); ++i)
                        {
                            if (i)
                                ph << ",";
                            ph << "?";
                        }
                        w.sql = "EXISTS (SELECT 1 FROM " + tbl + " sub WHERE sub." + ownerCol +
                                " = t._id AND sub." + col +
                                (f.op == FieldOp::IN ? " IN (" : " NOT IN (") + ph.str() + "))";
                        for (const auto &v : arrVals)
                            w.params.push_back(v);
                        return w;
                    }
                    case FieldOp::IS_NULL:
                        w.sql = "EXISTS (SELECT 1 FROM " + tbl + " sub WHERE sub." + ownerCol +
                                " = t._id AND sub." + col + " IS NULL)";
                        return w;
                    case FieldOp::NOT_NULL:
                        w.sql = "EXISTS (SELECT 1 FROM " + tbl + " sub WHERE sub." + ownerCol +
                                " = t._id AND sub." + col + " IS NOT NULL)";
                        return w;
                    default:
                        break;
                    }
                }
            }
        }

        // --- Existing: object.array CONTAINS ---
        const FieldSchema *subf = nullptr;
        for (const auto &fld : obj->fields())
            if (fld->name() == sub)
                subf = fld.get();

        if (subf && subf->type() == FieldType::Array && f.op == FieldOp::CONTAINS)
        {
            const auto *arr = dynamic_cast<const ArrayFieldSchema *>(subf);
            if (!arr)
                return w;

            if (arr->items()->type() == FieldType::Reference)
            {
                const auto *ref = dynamic_cast<const ReferenceFieldSchema *>(arr->items());
                const std::string joined = joinName(top, sub);
                const std::string valueCol =
                    refArrayValueCol(schema->profileName(), joined, ref->target());
                w.sql = "EXISTS (SELECT 1 FROM " + schema->profileName() + "$" + top + "$" + sub +
                        " sub WHERE sub." + schema->profileName() +
                        "_id = t._id AND sub." + valueCol + " = ?)";
                w.params.push_back(f.value);
                return w;
            }
            else
            {
                w.sql = "EXISTS (SELECT 1 FROM " + schema->profileName() + "$" + top + "$" + sub +
                        " sub WHERE sub." + schema->profileName() +
                        "_id = t._id AND sub.value = ?)";
                w.params.push_back(f.value);
                return w;
            }
        }
    }

    // --- Top-level array CONTAINS ---
    if (fs->type() == FieldType::Array)
    {
        auto arr = dynamic_cast<const ArrayFieldSchema *>(fs);
        if (f.op != FieldOp::CONTAINS)
        {
            w.sql = "1=0";
            return w;
        }
        if (arr->items()->type() == FieldType::Reference)
        {
            auto ref = dynamic_cast<const ReferenceFieldSchema *>(arr->items());
            const std::string valueCol =
                refArrayValueCol(schema->profileName(), top, ref->target());
            w.sql = "EXISTS (SELECT 1 FROM " + schema->profileName() + "$" + top +
                    " sub WHERE sub." + schema->profileName() +
                    "_id = t._id AND sub." + valueCol + " = ?)";
            w.params.push_back(f.value);
            return w;
        }
        else
        {
            w.sql = "EXISTS (SELECT 1 FROM " + schema->profileName() + "$" + top +
                    " sub WHERE sub." + schema->profileName() +
                    "_id = t._id AND sub.value = ?)";
            w.params.push_back(f.value);
            return w;
        }
    }

    // --- Primitive / enum / ref / simple object subfield ---
    std::string col = top;
    if (!sub.empty())
        col = flattenedObjectCol(top, sub);

    switch (f.op)
    {
    case FieldOp::EQ:
    case FieldOp::NEQ:
    case FieldOp::GT:
    case FieldOp::GTE:
    case FieldOp::LT:
    case FieldOp::LTE:
        w.sql = "t." + col + " " + opToStr(f.op) + " ?";
        w.params.push_back(f.value);
        break;
    case FieldOp::IS_NULL:
        w.sql = "t." + col + " IS NULL";
        break;
    case FieldOp::NOT_NULL:
        w.sql = "t." + col + " IS NOT NULL";
        break;
    case FieldOp::PREFIX:
        w.sql = "t." + col + " LIKE ?";
        w.params.emplace_back(f.value.isString() ? (f.value.asString() + "%") : "%");
        break;
    case FieldOp::CONTAINS:
        w.sql = "t." + col + " LIKE ?";
        w.params.emplace_back(f.value.isString() ? ("%" + f.value.asString() + "%") : "%");
        break;
    case FieldOp::IN:
    case FieldOp::NOT_IN:
    {
        if (!f.value.isArray())
        {
            w.sql = "1=0";
            break;
        }
        const auto &arr = f.value.asArray();
        if (arr.empty())
        {
            w.sql = (f.op == FieldOp::IN ? "1=0" : "1=1");
            break;
        }
        std::ostringstream ph;
        for (size_t i = 0; i < arr.size(); ++i)
        {
            if (i)
                ph << ",";
            ph << "?";
        }
        w.sql = "t." + col + (f.op == FieldOp::IN ? " IN (" : " NOT IN (") + ph.str() + ")";
        for (const auto &v : arr)
            w.params.push_back(v);
        break;
    }
    }
    return w;
}

SQLiteDataBackend::WhereSQL
SQLiteDataBackend::buildWhereDNF(const NodeSchema *schema, const QueryDNF &q) const
{
    WhereSQL w;
    if (q.any_of.empty())
    {
        w.sql = "1=1";
        return w;
    }

    std::ostringstream oss;
    bool firstOr = true;
    for (const auto &andGroup : q.any_of)
    {
        WhereSQL g;
        bool firstAnd = true;
        std::ostringstream group;
        for (const auto &f : andGroup)
        {
            auto one = buildWhereFilter(schema, f);
            if (one.sql.empty())
                continue;
            if (!firstAnd)
                group << " AND ";
            firstAnd = false;
            group << "(" << one.sql << ")";
            // append params
            w.params.insert(w.params.end(), one.params.begin(), one.params.end());
        }
        if (firstAnd)
            continue; // empty group
        if (!firstOr)
            oss << " OR ";
        firstOr = false;
        oss << "(" << group.str() << ")";
    }
    w.sql = oss.str().empty() ? "1=1" : oss.str();
    return w;
}

// ---------------- QueryDNF ----------------
std::vector<NodeSnapshot> SQLiteDataBackend::find(const QueryDNF &q) const
{
    const NodeSchema *schema = registry_->getSchema(q.profile);
    if (!schema)
        return {};

    // WHERE from DNF
    auto where = buildWhereDNF(schema, q);

    // Select main columns
    std::vector<std::pair<std::string, const FieldSchema *>> cols;
    collectMainColumns(schema, cols);

    // Build set of object-tops for reconstruction
    std::unordered_set<std::string> objectTops;
    for (const auto &kv : schema->fields())
        if (kv.second->type() == FieldType::Object)
            objectTops.insert(kv.first);

    std::ostringstream sql;
    sql << "SELECT t._id,t._label,t._parent_id";
    for (const auto &[c, _] : cols)
        sql << ",t." << c;
    sql << " FROM " << q.profile << " t";
    if (!where.sql.empty())
        sql << " WHERE " << where.sql;

    // order by
    if (!q.order_by.empty())
    {
        sql << " ORDER BY ";
        for (size_t i = 0; i < q.order_by.size(); ++i)
        {
            if (i)
                sql << ",";
            sql << "t." << q.order_by[i];
        }
        sql << (q.ascending ? " ASC" : " DESC");
    }
    // limit/offset
    if (q.limit > 0)
    {
        sql << " LIMIT " << q.limit;
        if (q.offset > 0)
            sql << " OFFSET " << q.offset;
    }
    sql << ";";

    Stmt st(db_, sql.str().c_str());

    // bind params
    int idx = 1;
    for (const auto &p : where.params)
        bindFieldValue(st.st, idx++, p);

    std::vector<NodeSnapshot> out;
    while (sqlite3_step(st.st) == SQLITE_ROW)
    {
        NodeSnapshot snap;
        snap.key.profile = q.profile;
        snap.key.id = std::string(reinterpret_cast<const char *>(sqlite3_column_text(st.st, 0)));
        snap.label = (sqlite3_column_type(st.st, 1) == SQLITE_NULL) ? std::string()
                                                                    : std::string(reinterpret_cast<const char *>(sqlite3_column_text(st.st, 1)));
        snap.parent_id = (sqlite3_column_type(st.st, 2) == SQLITE_NULL) ? std::string()
                                                                        : std::string(reinterpret_cast<const char *>(sqlite3_column_text(st.st, 2)));

        int base = 3;
        for (size_t i = 0; i < cols.size(); ++i)
        {
            const auto &[c, fs] = cols[i];
            FieldValue v = readColumn(st.st, base + static_cast<int>(i), fs->type());
            if (v.isNull())
                continue;

            // Support multi-level flattened names (like settings$network$ip)
            std::vector<std::string> parts;
            std::istringstream ss(c);
            std::string token;
            while (std::getline(ss, token, '$'))
                parts.push_back(token);

            if (parts.size() > 1)
            {
                // Ensure top-level object exists
                FieldValue &topVal = snap.fields[parts[0]];
                if (!topVal.isObject())
                    topVal = ObjectData{};
                insertNestedObject(topVal.asObject(),
                                   std::vector<std::string>(parts.begin() + 1, parts.end()),
                                   v);
            }
            else
            {
                snap.fields[c] = v;
            }
        }

        // ---- Arrays: read for every array field (including those nested in objects)
        for (const auto &kv : schema->fields())
        {
            const std::string &fname = kv.first;
            const FieldSchema *fs = kv.second.get();

            if (fs->type() == FieldType::Array)
            {
                const auto *arr = dynamic_cast<const ArrayFieldSchema *>(fs);
                snap.fields[fname] = readArrayField(snap.key, q.profile, fname, arr);
            }
            else if (fs->type() == FieldType::Object)
            {
                const auto *obj = dynamic_cast<const ObjectFieldSchema *>(fs);
                for (const auto &sub : obj->fields())
                {
                    if (sub->type() != FieldType::Array)
                        continue;

                    const auto *subArr = dynamic_cast<const ArrayFieldSchema *>(sub.get());
                    FieldValue arrVal = readArrayField(snap.key, q.profile, fname + "$" + sub->name(), subArr);

                    ObjectData &objVal = snap.fields[fname].isObject()
                                             ? snap.fields[fname].asObject()
                                             : (snap.fields[fname] = ObjectData{}, snap.fields[fname].asObject());
                    objVal[sub->name()] = arrVal;
                }
            }
        }

        out.emplace_back(std::move(snap));
    }
    return out;
}

void SQLiteDataBackend::reset()
{
    // Nothing to do if there is no active schema registry
    if (!registry_)
        return;

    if (!db_)
        open();

    begin();
    try
    {
        exec("PRAGMA foreign_keys=OFF;");

        std::vector<std::string> tables;

        // Traverse each profile schema
        for (const auto &[profileName, schemaPtr] : registry_->schemas())
        {
            if (!schemaPtr)
                continue;

            // Main table for this profile
            tables.push_back(profileName);

            // --- Handle field-based subtables ---
            for (const auto &[fname, fieldPtr] : schemaPtr->fields())
            {
                if (!fieldPtr)
                    continue;

                FieldType t = fieldPtr->type();

                // Array fields → side table <profile>$<field>
                if (t == FieldType::Array)
                {
                    tables.push_back(profileName + "$" + fname);
                }

                // Object fields may contain nested arrays
                else if (t == FieldType::Object)
                {
                    const auto *obj = dynamic_cast<const ObjectFieldSchema *>(fieldPtr.get());
                    if (!obj)
                        continue;

                    for (const auto &sub : obj->fields())
                    {
                        if (sub->type() == FieldType::Array)
                        {
                            tables.push_back(profileName + "$" + fname + "$" + sub->name());
                        }
                    }
                }
            }
        }

        // Drop all collected tables safely
        for (const auto &t : tables)
        {
            try
            {
                exec("DROP TABLE IF EXISTS " + t + ";");
            }
            catch (...)
            {
                // ignore individual drop errors
            }
        }

        commit();
    }
    catch (...)
    {
        rollback();
        throw;
    }

    exec("PRAGMA foreign_keys=ON;");
}

void SQLiteDataBackend::validateData() const
{
    if (!registry_)
        return;
    for (const auto &kv : registry_->schemas())
    {
        const NodeSchema *schema = kv.second.get();
        if (!schema)
            continue;
        QueryDNF q;
        q.profile = kv.first;
        auto nodes = find(q);
        for (const auto &node : nodes)
        {
            // Check direct reference fields
            for (const auto &field : schema->fields())
            {
                const FieldSchema *fs = field.second.get();
                if (fs->type() == FieldType::Reference)
                {
                    auto it = node.fields.find(field.first);
                    if (it != node.fields.end() && it->second.isString())
                    {
                        std::string refId = it->second.asString();
                        if (!refId.empty())
                        {
                            const auto *refFs = dynamic_cast<const ReferenceFieldSchema *>(fs);
                            NodeKey refKey{refFs->target(), std::optional<std::string>(refId)};
                            if (!exists(refKey))
                            {
                                throw std::runtime_error("validateData: Broken reference in " + node.key.profile + "/" + *node.key.id + " field " + field.first + " -> " + refKey.profile + "/" + refId);
                            }
                        }
                    }
                }
                // Check array<reference> fields
                if (fs->type() == FieldType::Array)
                {
                    const auto *arrFs = dynamic_cast<const ArrayFieldSchema *>(fs);
                    if (arrFs && arrFs->items() && arrFs->items()->type() == FieldType::Reference)
                    {
                        auto it = node.fields.find(field.first);
                        if (it != node.fields.end() && it->second.isArray())
                        {
                            for (const auto &v : it->second.asArray())
                            {
                                if (v.isString())
                                {
                                    std::string refId = v.asString();
                                    if (!refId.empty())
                                    {
                                        const auto *refFs = dynamic_cast<const ReferenceFieldSchema *>(arrFs->items());
                                        NodeKey refKey{refFs->target(), std::optional<std::string>(refId)};
                                        if (!exists(refKey))
                                        {
                                            throw std::runtime_error("validateData: Broken array reference in " + node.key.profile + "/" + *node.key.id + " field " + field.first + " -> " + refKey.profile + "/" + refId);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Run SQLite integrity check
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, "PRAGMA integrity_check;", -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
    {
        throw std::runtime_error("validateData: Failed to prepare integrity_check: " + std::string(sqlite3_errmsg(db_)));
    }
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
    {
        const char *result = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        if (std::string(result) != "ok")
        {
            sqlite3_finalize(stmt);
            throw std::runtime_error("validateData: SQLite integrity_check failed: " + std::string(result));
        }
    }
    sqlite3_finalize(stmt);
}
