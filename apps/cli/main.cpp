#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <filesystem>
#include <algorithm>
#include <regex>
#include <optional>
#include <set>
#include <cstdlib>

#include "SchemaManager.h"
#include "SQLiteDataBackend.h"
#include "FilesystemDataCommit.h"
#include "YamlDataLoader.h"
#include "CoreEngine.h"
#include "NodeAddress.h"
#include "Query.h"
#include "LuaScriptsRegistry.h"
#include "LuaEngine.h"

#include <nlohmann/json.hpp>

// ---------- tiny helpers ----------
// --- tiny helpers for FieldValue arrays ---
static ArrayData &ensure_array(FieldValue &fv)
{
    if (!fv.isArray())
        fv = ArrayData{};
    return fv.asArray();
}

static FieldValue make_scalar_field(const std::string &s)
{
    // Keep it simple for CLI: treat as string (IDs, tags, etc)
    // If you want int/bool inference, reuse parse_value(s) here.
    return FieldValue(s);
}

static std::string dequote(std::string v)
{
    if (v.size() >= 2 &&
        ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\'')))
    {
        v = v.substr(1, v.size() - 2);
    }
    return v;
}

static inline std::string trim(std::string s)
{
    auto wsfront = std::find_if_not(s.begin(), s.end(), [](int c)
                                    { return std::isspace(c); });
    auto wsback = std::find_if_not(s.rbegin(), s.rend(), [](int c)
                                   { return std::isspace(c); })
                      .base();
    return (wsback <= wsfront ? std::string() : std::string(wsfront, wsback));
}

// replaces split_spaces
static std::vector<std::string> split_args_quotes(const std::string &line)
{
    std::vector<std::string> out;
    std::string cur;
    bool in_quotes = false;

    for (char c : line)
    {
        if (c == '"')
        {
            in_quotes = !in_quotes;
            cur.push_back(c);
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)) && !in_quotes)
        {
            if (!cur.empty())
            {
                out.push_back(cur);
                cur.clear();
            }
        }
        else
        {
            cur.push_back(c);
        }
    }
    if (!cur.empty())
        out.push_back(cur);
    return out;
}

static std::vector<std::string> split_spaces(const std::string &line)
{
    std::vector<std::string> out;
    std::istringstream iss(line);
    std::string tok;
    while (iss >> std::quoted(tok))
        out.push_back(tok);
    if (out.empty())
    { // fallback simple split
        std::istringstream is2(line);
        while (is2 >> tok)
            out.push_back(tok);
    }
    return out;
}

static nlohmann::json schemas_to_json(const SchemaManager &sm)
{
    nlohmann::json res;
    const auto *root = sm.root();
    res["root"] = root ? nlohmann::json(root->profileName()) : nlohmann::json(nullptr);
    res["schemas"] = nlohmann::json::array();
    for (const auto &kv : sm.registry().schemas())
    {
        const std::string &name = kv.first;
        bool is_root = (root && root->profileName() == name);
        res["schemas"].push_back({{"name", name}, {"is_root", is_root}});
    }
    return res;
}

static std::unordered_map<std::string, std::string>
load_schema_sources_dir(const std::filesystem::path &dir)
{
    std::unordered_map<std::string, std::string> sources;
    if (!std::filesystem::exists(dir))
        throw std::runtime_error("Schemas dir not found: " + dir.string());

    for (auto &e : std::filesystem::directory_iterator(dir))
    {
        if (!e.is_regular_file())
            continue;
        auto p = e.path();
        auto ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".yml" || ext == ".yaml")
        {
            std::ifstream f(p, std::ios::binary);
            std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            sources[p.filename().string()] = std::move(s);
        }
    }
    if (sources.empty())
        throw std::runtime_error("No .yaml/.yml files in " + dir.string());
    return sources;
}

static std::unordered_map<std::string, std::string>
load_script_sources_dir(const std::filesystem::path &dir)
{
    std::unordered_map<std::string, std::string> sources;
    if (!std::filesystem::exists(dir))
        throw std::runtime_error("Scripts dir not found: " + dir.string());

    for (auto &e : std::filesystem::directory_iterator(dir))
    {
        if (!e.is_regular_file())
            continue;
        auto p = e.path();
        auto ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".lua")
        {
            std::ifstream f(p, std::ios::binary);
            std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            sources[p.filename().string()] = std::move(s);
        }
    }
    return sources;
}

static FieldValue parse_value(const std::string &raw)
{
    std::string v = raw;

    // 1) JSON/YAML-ish bracketed array: [a, b, "c"]
    if (v.size() >= 2 && v.front() == '[' && v.back() == ']')
    {
        std::string inner = v.substr(1, v.size() - 2);
        ArrayData arr;

        // Allow empty list "[]"
        if (!inner.empty())
        {
            std::stringstream ss(inner);
            std::string item;
            while (std::getline(ss, item, ','))
            {
                auto t = trim(item);
                // dequote each element if quoted
                if (t.size() >= 2 &&
                    ((t.front() == '"' && t.back() == '"') ||
                     (t.front() == '\'' && t.back() == '\'')))
                {
                    t = t.substr(1, t.size() - 2);
                }
                arr.emplace_back(FieldValue(t));
            }
        }
        return FieldValue(arr);
    }

    // 2) null / bool / int
    if (v == "null" || v == "NULL" || v == "Null")
        return FieldValue{};
    if (v == "true" || v == "True" || v == "TRUE")
        return FieldValue(true);
    if (v == "false" || v == "False" || v == "FALSE")
        return FieldValue(false);
    {
        char *end = nullptr;
        long n = std::strtol(v.c_str(), &end, 10);
        if (end && *end == '\0')
            return FieldValue(static_cast<int>(n));
    }

    // 3) Plain string (dequote if wrapped) - check BEFORE comma detection
    if (v.size() >= 2 &&
        ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\'')))
    {
        v = v.substr(1, v.size() - 2);
        return FieldValue(v);
    }

    // 4) Comma-separated list without brackets: tags=a,b,c
    if (v.find(',') != std::string::npos)
    {
        ArrayData arr;
        std::stringstream ss(v);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            auto t = trim(item);
            arr.emplace_back(FieldValue(t));
        }
        return FieldValue(arr);
    }

    // 5) Unquoted string
    return FieldValue(v);
}

static ObjectData parse_object_literal(std::string s);

static FieldValue parse_value_no_array(const std::string &raw)
{
    std::string v = trim(raw);
    if (v.size() >= 2 && v.front() == '{' && v.back() == '}')
        return FieldValue(parse_object_literal(v));
    return parse_value(raw);
}

static FieldValue field_value_from_json(const nlohmann::json &j)
{
    if (j.is_null())
        return FieldValue(std::monostate{});
    if (j.is_boolean())
        return FieldValue(j.get<bool>());
    if (j.is_number_integer())
        return FieldValue(j.get<int>());
    if (j.is_string())
        return FieldValue(j.get<std::string>());

    if (j.is_array())
    {
        ArrayData arr;
        for (auto &elem : j)
            arr.emplace_back(field_value_from_json(elem));
        return FieldValue(arr);
    }

    if (j.is_object())
    {
        ObjectData obj;
        for (auto &[k, v] : j.items())
            obj[k] = field_value_from_json(v);
        return FieldValue(obj);
    }

    return FieldValue(j.dump());
}

static nlohmann::json field_value_to_json(const FieldValue &v)
{
    if (v.isNull())
        return nullptr;
    if (v.isInteger())
        return v.asInteger();
    if (v.isBoolean())
        return v.asBoolean();
    if (v.isString())
        return v.asString();
    if (v.isArray())
    {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &el : v.asArray())
            arr.push_back(field_value_to_json(el));
        return arr;
    }
    if (v.isObject())
    {
        nlohmann::json obj = nlohmann::json::object();
        for (const auto &kv : v.asObject())
            obj[kv.first] = field_value_to_json(kv.second);
        return obj;
    }
    return nullptr;
}

static nlohmann::json node_to_json(const NodeSnapshot &snap)
{
    return nlohmann::json::parse(snap.toJson());
}

static nlohmann::json node_summary_json(const NodeSnapshot &snap)
{
    nlohmann::json j;
    j["profile"] = snap.key.profile;
    if (snap.key.id.has_value())
        j["id"] = *snap.key.id;
    if (snap.label.has_value())
        j["label"] = *snap.label;
    return j;
}

static void print_json_ok(const std::string &cmd, const nlohmann::json &result)
{
    nlohmann::json out;
    out["ok"] = true;
    out["command"] = cmd;
    out["result"] = result;
    std::cout << out.dump() << "\n";
}

static void print_json_error(const std::string &cmd, const std::string &message)
{
    nlohmann::json out;
    out["ok"] = false;
    out["command"] = cmd;
    out["error"] = message;
    std::cout << out.dump() << "\n";
}

static void print_value(const FieldValue &v, int indent = 0);

static void print_object(const ObjectData &o, int indent)
{
    for (const auto &kv : o)
    {
        for (int i = 0; i < indent; i++)
            std::cout << ' ';
        std::cout << kv.first << ": ";
        print_value(kv.second, indent + 2);
    }
}
static void print_array(const ArrayData &a, int)
{
    std::cout << "[";
    bool first = true;
    for (const auto &el : a)
    {
        if (!first)
            std::cout << ", ";
        first = false;
        if (el.isString())
            std::cout << '"' << el.asString() << '"';
        else if (el.isInteger())
            std::cout << el.asInteger();
        else if (el.isBoolean())
            std::cout << (el.asBoolean() ? "true" : "false");
        else if (el.isObject())
            std::cout << "{...}";
        else if (el.isArray())
            std::cout << "[...]";
        else
            std::cout << "null";
    }
    std::cout << "]\n";
}
static void print_value(const FieldValue &v, int indent)
{
    if (v.isNull())
    {
        std::cout << "null\n";
        return;
    }
    if (v.isString())
    {
        std::cout << '"' << v.asString() << '"' << "\n";
        return;
    }
    if (v.isInteger())
    {
        std::cout << v.asInteger() << "\n";
        return;
    }
    if (v.isBoolean())
    {
        std::cout << (v.asBoolean() ? "true" : "false") << "\n";
        return;
    }
    if (v.isArray())
    {
        print_array(v.asArray(), indent);
        return;
    }
    if (v.isObject())
    {
        std::cout << "{\n";
        print_object(v.asObject(), indent + 2);
        for (int i = 0; i < indent; i++)
            std::cout << ' ';
        std::cout << "}\n";
        return;
    }
    std::cout << "<unknown>\n";
}

static std::unordered_map<std::string, YAML::Node>
load_data_docs_dir(const std::filesystem::path &dir)
{
    using namespace std::filesystem;

    if (!exists(dir))
        throw std::runtime_error("Data dir not found: " + dir.string());

    // Read *.yml / *.yaml recursively; key = relative path (forward slashes)
    std::unordered_map<std::string, std::string> sources;

    const auto root = weakly_canonical(dir);
    for (recursive_directory_iterator it(
             root, directory_options::skip_permission_denied),
         end;
         it != end; ++it)
    {
        if (!it->is_regular_file())
            continue;

        auto p = it->path();
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext != ".yaml" && ext != ".yml")
            continue;

        std::ifstream f(p, std::ios::binary);
        if (!f)
            throw std::runtime_error("Cannot open data file: " + p.string());

        std::string s((std::istreambuf_iterator<char>(f)), {});
        // relative key with forward slashes for stability across OSes
        std::string rel = relative(p, root).generic_string();

        // If you prefer keys without folders, be aware of collisions.
        if (!sources.emplace(rel, std::move(s)).second)
            throw std::runtime_error("Duplicate data key: " + rel);
    }

    return YamlDataLoader::loadFromSources(sources);
}

// Build a NodeSnapshot from CLI args
// usage: create <profile> <id> [label=...] [parent=...] [field=val] [obj.sub=val] [tags=a,b,c]
static NodeSnapshot build_snapshot(const std::string &profile,
                                   const std::string &id,
                                   const std::vector<std::string> &kvs)
{
    NodeSnapshot n;
    n.key = NodeKey{profile, id};
    for (const auto &kv : kvs)
    {
        auto eq = kv.find('=');
        if (eq == std::string::npos)
            continue;
        std::string k = kv.substr(0, eq);
        std::string v = kv.substr(eq + 1);
        if (k == "label")
        {
            n.label = dequote(v); // instead of: n.label = v;
        }
        else if (k == "parent")
        {
            n.parent_id = dequote(v); // instead of: n.parent_id = v;
        }
        else if (k.find('.') != std::string::npos)
        {
            auto dot = k.find('.');
            std::string top = k.substr(0, dot);
            std::string sub = k.substr(dot + 1);
            auto &topFV = n.fields[top];
            if (!topFV.isObject())
                topFV = ObjectData{};
            topFV.asObject()[sub] = parse_value_no_array(v);
        }
        else
        {
            n.fields[k] = parse_value_no_array(v);
        }
    }
    return n;
}

// --- relation lookup: is profile a child? return parent profile if yes ---
static std::optional<std::string> parent_profile_of(const SchemaRegistry &reg, const std::string &profile)
{
    for (const auto &kv : reg.schemas())
    {
        const NodeSchema *parent = kv.second.get();
        for (const auto &childKV : parent->children())
        {
            const NodeSchema *child = childKV.second.get();
            if (child && child->profileName() == profile)
                return parent->profileName();
        }
    }
    return std::nullopt;
}

struct ArrSelector
{
    std::string field;              // e.g., "tags" or "objects"
    std::optional<size_t> index;    // e.g., 0 if "tags[0]"
    std::optional<std::string> sub; // e.g., "name" if "objects[0].name"
};

// Parse "field", "field[3]" or "field[3].sub"
static std::optional<ArrSelector> parse_selector(const std::string &s)
{
    static const std::regex rx(R"(^([A-Za-z0-9_]+)(?:\[(\d+)\])?(?:\.([A-Za-z0-9_]+))?$)");
    std::smatch m;
    if (!std::regex_match(s, m, rx))
        return std::nullopt;
    ArrSelector sel;
    sel.field = m[1];
    if (m[2].matched)
        sel.index = static_cast<size_t>(std::stoul(m[2]));
    if (m[3].matched)
        sel.sub = m[3];
    return sel;
}

// Always preserve label/parent_id from the current row (non-optional NodeSnapshot)
static NodeSnapshot make_patch_from_prev(const NodeSnapshot &prev,
                                         const std::string &profile,
                                         const std::string &id)
{
    NodeSnapshot patch;
    patch.key = NodeKey{profile, id};
    patch.label = prev.label;         // keep current _label
    patch.parent_id = prev.parent_id; // keep current _parent_id
    // fields filled by callers (only the one array field being edited)
    return patch;
}

static void arr_add(CoreEngine &engine, const std::string &profile, const std::string &id,
                    const std::string &field, const std::string &value)
{
    auto prev = engine.fetch(NodeKey{profile, id});
    if (!prev)
    {
        throw std::runtime_error("not found");
    }

    FieldValue cur = prev->fields.count(field) ? prev->fields.at(field) : FieldValue(ArrayData{});
    auto &arr = ensure_array(cur);
    arr.emplace_back(make_scalar_field(value));

    auto patch = make_patch_from_prev(*prev, profile, id);
    patch.fields[field] = cur;
    engine.upsert(patch);
}

static void arr_del(CoreEngine &engine, const std::string &profile, const std::string &id,
                    const std::string &field, const std::optional<size_t> &index,
                    const std::optional<std::string> &value)
{
    auto prev = engine.fetch(NodeKey{profile, id});
    if (!prev)
    {
        throw std::runtime_error("not found");
    }

    auto itf = prev->fields.find(field);
    if (itf == prev->fields.end() || !itf->second.isArray())
    {
        throw std::runtime_error("field is not an array or is missing");
    }
    auto cur = itf->second; // copy
    auto &arr = cur.asArray();

    if (index)
    {
        if (*index >= arr.size())
        {
            throw std::runtime_error("index out of range");
        }
        arr.erase(arr.begin() + *index);
    }
    else if (value)
    {
        auto it = std::find_if(arr.begin(), arr.end(), [&](const FieldValue &v)
                               { return v.isString() && v.asString() == *value; });
        if (it == arr.end())
        {
            throw std::runtime_error("value not found");
        }
        arr.erase(it);
    }
    else
    {
        throw std::runtime_error("specify index=<n> or value=<v>");
    }

    auto patch = make_patch_from_prev(*prev, profile, id);
    patch.fields[field] = cur;
    engine.upsert(patch);
}

static void arr_set(CoreEngine &engine, const std::string &profile, const std::string &id,
                    const std::string &selectorEq)
{
    auto pos = selectorEq.find('=');
    if (pos == std::string::npos)
    {
        throw std::runtime_error("use <field>[i][.sub]=<value>");
    }
    std::string selstr = selectorEq.substr(0, pos);
    std::string rawVal = selectorEq.substr(pos + 1);

    auto sel = parse_selector(selstr);
    if (!sel)
    {
        throw std::runtime_error("bad selector");
    }
    if (!sel->index)
    {
        throw std::runtime_error("need an index for arr-set");
    }

    auto prev = engine.fetch(NodeKey{profile, id});
    if (!prev)
    {
        throw std::runtime_error("not found");
    }

    FieldValue cur = prev->fields.count(sel->field) ? prev->fields.at(sel->field) : FieldValue(ArrayData{});
    auto &arr = ensure_array(cur);

    if (arr.size() <= *sel->index)
        arr.resize(*sel->index + 1, FieldValue{}); // expand

    if (sel->sub)
    {
        // array<object>
        if (!arr[*sel->index].isObject())
            arr[*sel->index] = FieldValue(ObjectData{});
        auto &obj = arr[*sel->index].asObject();
        obj[*sel->sub] = parse_value(rawVal);
    }
    else
    {
        // array<scalar/ref>
        arr[*sel->index] = parse_value(rawVal);
    }

    auto patch = make_patch_from_prev(*prev, profile, id);
    patch.fields[sel->field] = cur;
    engine.upsert(patch);
}

// --- helpers: parse object payloads ---
static ObjectData parse_object_literal(std::string s)
{
    ObjectData obj;

    auto strip = [](std::string t)
    {
        t = trim(t);
        if (t.size() >= 2 &&
            ((t.front() == '"' && t.back() == '"') || (t.front() == '\'' && t.back() == '\'')))
            t = t.substr(1, t.size() - 2);
        return t;
    };

    // If it's a { ... } block, split by commas, then key: value OR key = value
    if (!s.empty() && s.front() == '{' && s.back() == '}')
    {
        s = s.substr(1, s.size() - 2);
        std::stringstream ss(s);
        std::string pair;
        while (std::getline(ss, pair, ','))
        {
            auto p = trim(pair);
            if (p.empty())
                continue;
            size_t pos = p.find(':');
            if (pos == std::string::npos)
                pos = p.find('=');
            if (pos == std::string::npos)
                continue;
            std::string k = strip(p.substr(0, pos));
            std::string v = strip(p.substr(pos + 1));
            obj[k] = parse_value(v);
        }
        return obj;
    }

    // Otherwise expect tokens like:  name=...  value=...
    // (This function only gets a single string when called standalone;
    // for multiple k=v tokens we use parse_object_kv below.)
    size_t pos = s.find(':');
    if (pos == std::string::npos)
        pos = s.find('=');
    if (pos != std::string::npos)
    {
        std::string k = strip(s.substr(0, pos));
        std::string v = strip(s.substr(pos + 1));
        obj[k] = parse_value(v);
    }
    return obj;
}

static ObjectData parse_object_kv(const std::vector<std::string> &kvs)
{
    ObjectData obj;
    for (auto &t : kvs)
    {
        auto eq = t.find('=');
        auto co = t.find(':');
        size_t pos = (eq == std::string::npos) ? co : (co == std::string::npos ? eq : std::min(eq, co));
        if (pos == std::string::npos)
            continue;
        std::string k = trim(t.substr(0, pos));
        std::string v = trim(t.substr(pos + 1));
        if (v.size() >= 2 &&
            ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\'')))
            v = v.substr(1, v.size() - 2);
        obj[k] = parse_value(v);
    }
    return obj;
}

static std::string help_text()
{
        return R"(commands:
    help
    load-schemas <dir>              # loads all .yaml/.yml from folder
    load-scripts <dir>              # loads all .lua from folder
    load-workspace <schemas> <data> <scripts>  # reset and reload workspace
        # Or: with env vars set (HEPHORA_SCHEMAS, HEPHORA_DATA, HEPHORA_SCRIPTS)
        #     just run: load-workspace
    create <profile> [k=v ...] # child profiles REQUIRE: parent=<parent_id>
    update <profile> <id> [k=v ...] # same as create; upsert semantics
    arr-add <profile> <id> <field> <value>
    arr-del <profile> <id> <field> [index=<n>] [value=<v>]
    arr-set <profile> <id> <field>[i][.sub]=<value>
    delete <profile> <id>
    get <profile> <id>
    get-many <profile> <id...>
    get-by-id <id>
    get-by-ids <id...>
    get-field <profile> <id> <field>      # field can be obj.sub or arr[i].sub
    get-select <profile> <id> [field]   # field can be obj.sub or arr[i].sub
    list <profile>
    query <json>                   # JSON body like HTTP /query
    get-children <profile> <id>    # list child nodes across profiles
    get-children-by-id <id>        # list child nodes for any node with this id
    get-refs-to-by-id <id>         # list nodes referencing any node with this id
    schemas                        # list schemas with root marker
    get-profiles                   # list profiles with kind
    get-schema <profile>           # show raw YAML schema
    execute-command <profile> <id> <command>
    load-data <dir>                # load all .yaml/.yml from folder into DB
    quit | exit

Non-interactive mode:
    hephora-sdk-cli --schemas <dir> --data <dir> --scripts <dir> <command> [args...]
    # Environment variable defaults (CLI flags override env):
    #   HEPHORA_SCHEMAS=<dir>
    #   HEPHORA_DATA=<dir>
    #   HEPHORA_SCRIPTS=<dir>
    # When set, you can omit the flags:
    #   HEPHORA_SCHEMAS=./schemas HEPHORA_DATA=./data HEPHORA_SCRIPTS=./scripts \
    #     hephora-sdk-cli list project
    # Example:
    # hephora-sdk-cli --schemas ./schemas --data ./data --scripts ./scripts list project
 )";
}

int main(int argc, char **argv)
try
{
    // --- components ---
    SchemaManager sm;
    SQLiteDataBackend backend(":memory:", /*recreate*/ true);

    EngineConfig cfg;
    cfg.base_path = ""; // write directly under the data root (no extra "data/" prefix)

    FilesystemDataCommit committer(std::filesystem::path(".")); // writes to paths we pass in

    CoreEngine engine(backend, &committer, cfg);

    LuaScriptsRegistry luaRegistry;
    LuaEngine luaEngine(&engine, &luaRegistry);

    auto flush_after_change = [&]()
    {
        engine.flushPending();
    };

    auto execute_command = [&](const std::vector<std::string> &args, const std::string &line) -> bool
    {
        if (args.empty())
            return false;
        const auto cmd = args[0];

        try
        {
            if (cmd == "help")
            {
                print_json_ok(cmd, {{"text", help_text()}});
            }
            else if (cmd == "quit" || cmd == "exit")
            {
                return true;
            }
            else if (cmd == "load-schemas")
            {
                if (args.size() < 2)
                {
                    print_json_error(cmd, "usage: load-schemas <dir>");
                    return false;
                }
                auto dir = std::filesystem::path(args[1]);
                auto srcs = load_schema_sources_dir(dir);
                sm.loadSources(srcs);
                engine.init(sm.registry());
                print_json_ok(cmd, {{"schemas", srcs.size()}, {"root", sm.root() ? sm.root()->profileName() : ""}});
            }
            else if (cmd == "load-scripts")
            {
                if (args.size() < 2)
                {
                    print_json_error(cmd, "usage: load-scripts <dir>");
                    return false;
                }
                auto srcs = load_script_sources_dir(args[1]);
                luaRegistry.loadScripts(srcs);
                print_json_ok(cmd, {{"scripts", srcs.size()}});
            }
            else if (cmd == "load-workspace")
            {
                if (args.size() != 4)
                {
                    // Allow env-var driven load-workspace (no args) if all are provided
                    const char *envSchemas = std::getenv("HEPHORA_SCHEMAS");
                    const char *envData = std::getenv("HEPHORA_DATA");
                    const char *envScripts = std::getenv("HEPHORA_SCRIPTS");
                    if (args.size() == 1 && envSchemas && envData && envScripts)
                    {
                        // synthesize args: [cmd, schemas, data, scripts]
                        std::vector<std::string> a = {cmd, envSchemas, envData, envScripts};
                        // Re-run the handler with synthesized args
                        // Set current working directory to data
                        std::error_code ec;
                        std::filesystem::current_path(a[2], ec);
                        if (ec)
                        {
                            print_json_error(cmd, "failed to set data directory: " + ec.message());
                            return false;
                        }

                        engine.resetBackend();
                        sm.clear();

                        auto schemas = load_schema_sources_dir(a[1]);
                        sm.loadSources(schemas);
                        engine.init(sm.registry());

                        auto data = load_data_docs_dir(a[2]);
                        engine.loadData(data);
                        engine.flushPending();

                        luaRegistry.clear();
                        luaRegistry.loadScripts(load_script_sources_dir(a[3]));
                        print_json_ok(cmd, {{"schemas", schemas.size()}, {"data", data.size()}});
                        return false;
                    }

                    print_json_error(cmd, "usage: load-workspace <schemas> <data> <scripts>");
                    return false;
                }

                std::error_code ec;
                std::filesystem::current_path(args[2], ec);
                if (ec)
                {
                    print_json_error(cmd, "failed to set data directory: " + ec.message());
                    return false;
                }

                engine.resetBackend();
                sm.clear();

                auto schemas = load_schema_sources_dir(args[1]);
                sm.loadSources(schemas);
                engine.init(sm.registry());

                auto data = load_data_docs_dir(args[2]);
                engine.loadData(data);
                engine.flushPending();

                luaRegistry.clear();
                luaRegistry.loadScripts(load_script_sources_dir(args[3]));
                print_json_ok(cmd, {{"schemas", schemas.size()}, {"data", data.size()}});
            }
            else if (cmd == "load-data")
            {
                if (args.size() != 2)
                {
                    print_json_error(cmd, "usage: load-data <dir>");
                    return false;
                }
                if (!sm.root())
                {
                    print_json_error(cmd, "load schemas first");
                    return false;
                }
                auto docs = load_data_docs_dir(args[1]);
                engine.loadData(docs);
                print_json_ok(cmd, {{"data", docs.size()}});
            }
            else if (cmd == "create" || cmd == "update")
            {
                if (args.size() < 2)
                {
                    print_json_error(cmd, "usage: " + cmd + " <profile> [k=v ...]");
                    return false;
                }

                std::string profile = args[1];
                if (!sm.hasSchema(profile))
                {
                    print_json_error(cmd, "unknown profile: " + profile);
                    return false;
                }

                const bool updating = (cmd == "update");

                // create: no <id>, update: requires <id>
                std::optional<std::string> id;
                size_t kvStart = 2;
                if (updating)
                {
                    if (args.size() < 3)
                    {
                        print_json_error(cmd, "usage: update <profile> <id> [k=v ...]");
                        return false;
                    }
                    id = args[2];
                    kvStart = 3;
                }

                std::vector<std::string> kvs(args.begin() + kvStart, args.end());
                NodeSnapshot snap;

                if (updating)
                {
                    snap = build_snapshot(profile, *id, kvs);
                }
                else
                {
                    // new node, no id yet (backend will assign UUID)
                    snap.key.profile = profile;
                    for (auto &kv : kvs)
                    {
                        auto eq = kv.find('=');
                        if (eq == std::string::npos)
                            continue;
                        std::string k = kv.substr(0, eq);
                        std::string v = kv.substr(eq + 1);
                        if (k == "label")
                            snap.label = dequote(v);
                        else if (k == "parent")
                            snap.parent_id = dequote(v);
                        else if (k.find('.') != std::string::npos)
                        {
                            auto dot = k.find('.');
                            std::string top = k.substr(0, dot);
                            std::string sub = k.substr(dot + 1);
                            auto &topFV = snap.fields[top];
                            if (!topFV.isObject())
                                topFV = ObjectData{};
                            topFV.asObject()[sub] = parse_value_no_array(v);
                        }
                        else
                        {
                            snap.fields[k] = parse_value_no_array(v);
                        }
                    }
                }

                try
                {
                    engine.upsert(snap);
                    flush_after_change();
                    auto saved = engine.fetch(snap.key);
                    if (!saved)
                    {
                        print_json_error(cmd, "insertion failed");
                        return false;
                    }
                    print_json_ok(cmd, {{"node", node_to_json(*saved)}});
                }
                catch (const std::exception &e)
                {
                    print_json_error(cmd, e.what());
                }
            }
            else if (cmd == "delete")
            {
                if (args.size() != 3)
                {
                    print_json_error(cmd, "usage: delete <profile> <id>");
                    return false;
                }
                engine.remove(NodeKey{args[1], args[2]});
                flush_after_change();
                print_json_ok(cmd, {{"deleted", {{"profile", args[1]}, {"id", args[2]}}}});
            }
            else if (cmd == "schemas")
            {
                print_json_ok(cmd, schemas_to_json(sm));
            }
            else if (cmd == "get-profiles")
            {
                const auto &schemas = sm.registry().schemas();
                nlohmann::json profiles = nlohmann::json::array();
                for (const auto &[name, schemaPtr] : schemas)
                {
                    std::string kind = (schemaPtr->kind() == NodeKind::Root) ? "root" : "node";
                    profiles.push_back({{"name", name}, {"kind", kind}});
                }
                print_json_ok(cmd, {{"profiles", profiles}});
            }
            else if (cmd == "get-schema")
            {
                if (args.size() != 2)
                {
                    print_json_error(cmd, "usage: get-schema <profile>");
                    return false;
                }
                const auto *schema = sm.getSchema(args[1]);
                if (!schema)
                {
                    print_json_error(cmd, "unknown profile: " + args[1]);
                    return false;
                }
                print_json_ok(cmd, {{"profile", args[1]}, {"schema", schema->rawYaml()}});
            }
            else if (cmd == "get")
            {
                if (args.size() != 3)
                {
                    print_json_error(cmd, "usage: get <profile> <id>");
                    return false;
                }
                auto snap = engine.fetch(NodeKey{args[1], args[2]});
                if (!snap)
                {
                    print_json_error(cmd, "not found");
                    return false;
                }
                print_json_ok(cmd, {{"node", node_to_json(*snap)}});
            }
            else if (cmd == "get-many")
            {
                if (args.size() < 3)
                {
                    print_json_error(cmd, "usage: get-many <profile> <id...>");
                    return false;
                }
                const std::string profile = args[1];
                nlohmann::json items = nlohmann::json::array();
                for (size_t i = 2; i < args.size(); ++i)
                {
                    auto snap = engine.fetch(NodeKey{profile, args[i]});
                    if (snap)
                        items.push_back(node_to_json(*snap));
                    else
                        items.push_back({{"profile", profile}, {"id", args[i]}, {"missing", true}});
                }
                print_json_ok(cmd, {{"count", items.size()}, {"nodes", items}});
            }
            else if (cmd == "get-by-id")
            {
                if (args.size() != 2)
                {
                    print_json_error(cmd, "usage: get-by-id <id>");
                    return false;
                }
                const std::string id = args[1];
                nlohmann::json items = nlohmann::json::array();
                for (const auto &[profile, _] : sm.registry().schemas())
                {
                    auto snap = engine.fetch(NodeKey{profile, id});
                    if (snap)
                        items.push_back(node_to_json(*snap));
                }
                print_json_ok(cmd, {{"count", items.size()}, {"nodes", items}});
            }
            else if (cmd == "get-by-ids")
            {
                if (args.size() < 2)
                {
                    print_json_error(cmd, "usage: get-by-ids <id...>");
                    return false;
                }
                nlohmann::json results = nlohmann::json::array();
                for (size_t i = 1; i < args.size(); ++i)
                {
                    const std::string id = args[i];
                    nlohmann::json items = nlohmann::json::array();
                    for (const auto &[profile, _] : sm.registry().schemas())
                    {
                        auto snap = engine.fetch(NodeKey{profile, id});
                        if (snap)
                            items.push_back(node_to_json(*snap));
                    }
                    results.push_back({{"id", id}, {"count", items.size()}, {"nodes", items}});
                }
                print_json_ok(cmd, {{"count", results.size()}, {"results", results}});
            }
            else if (cmd == "list")
            {
                if (args.size() != 2)
                {
                    print_json_error(cmd, "usage: list <profile>");
                    return false;
                }
                QueryDNF q;
                q.profile = args[1];
                auto rows = engine.find(q);
                nlohmann::json items = nlohmann::json::array();
                for (const auto &r : rows)
                    items.push_back(node_summary_json(r));
                print_json_ok(cmd, {{"count", rows.size()}, {"rows", items}});
            }
            else if (cmd == "query")
            {
                std::string payload = trim(line.substr(cmd.size()));
                if (payload.empty())
                {
                    print_json_error(cmd, "usage: query <json>");
                    return false;
                }

                nlohmann::json body = nlohmann::json::parse(payload);
                QueryDNF q;
                q.profile = body["profile"].get<std::string>();
                q.ascending = body.contains("order") ? body["order"]["direction"].get<std::string>() == "ASC" : true;
                if (body.contains("order"))
                {
                    for (const auto &field : body["order"]["fields"])
                        q.order_by.push_back(field.get<std::string>());
                }
                q.limit = body.contains("limit") ? body["limit"].get<int>() : 0;
                q.offset = body.contains("offset") ? body["offset"].get<int>() : 0;

                for (const auto &and_group : body["query"])
                {
                    std::vector<FieldFilter> ff_group;
                    for (const auto &cond : and_group)
                    {
                        FieldFilter ff;
                        ff.field = cond["field"].get<std::string>();
                        std::string op = cond["operator"].get<std::string>();
                        if (op == "EQ")
                            ff.op = FieldOp::EQ;
                        else if (op == "NEQ")
                            ff.op = FieldOp::NEQ;
                        else if (op == "GT")
                            ff.op = FieldOp::GT;
                        else if (op == "GTE")
                            ff.op = FieldOp::GTE;
                        else if (op == "LT")
                            ff.op = FieldOp::LT;
                        else if (op == "LTE")
                            ff.op = FieldOp::LTE;
                        else if (op == "IN")
                            ff.op = FieldOp::IN;
                        else if (op == "NOT_IN")
                            ff.op = FieldOp::NOT_IN;
                        else if (op == "IS_NULL")
                            ff.op = FieldOp::IS_NULL;
                        else if (op == "NOT_NULL")
                            ff.op = FieldOp::NOT_NULL;
                        else if (op == "PREFIX")
                            ff.op = FieldOp::PREFIX;
                        else if (op == "CONTAINS")
                            ff.op = FieldOp::CONTAINS;
                        else
                            throw std::runtime_error("invalid operator: " + op);

                        if (ff.op != FieldOp::IS_NULL && ff.op != FieldOp::NOT_NULL)
                            ff.value = field_value_from_json(cond["value"]);

                        ff_group.emplace_back(ff);
                    }
                    q.any_of.emplace_back(ff_group);
                }

                auto rows = engine.find(q);
                nlohmann::json items = nlohmann::json::array();
                for (const auto &r : rows)
                    items.push_back(node_summary_json(r));
                print_json_ok(cmd, {{"count", rows.size()}, {"rows", items}});
            }
            else if (cmd == "get-children")
            {
                if (args.size() != 3)
                {
                    print_json_error(cmd, "usage: get-children <profile> <id>");
                    return false;
                }
                const std::string profile = args[1];
                const std::string node_id = args[2];
                const auto &schemas = sm.registry().schemas();
                if (schemas.empty())
                {
                    print_json_ok(cmd, {{"count", 0}, {"rows", nlohmann::json::array()}});
                    return false;
                }

                std::set<NodeKey> childrenKeys;
                for (const auto &[name, schemaPtr] : schemas)
                {
                    auto children = engine.childrenOf(NodeKey{profile, node_id}, name);
                    childrenKeys.insert(children.begin(), children.end());
                }

                nlohmann::json items = nlohmann::json::array();
                for (const auto &childKey : childrenKeys)
                {
                    auto snap = engine.fetch(childKey);
                    if (snap)
                        items.push_back(node_summary_json(*snap));
                    else
                        items.push_back({{"profile", childKey.profile}, {"id", childKey.id.value_or("")}});
                }
                print_json_ok(cmd, {{"count", childrenKeys.size()}, {"rows", items}});
            }
            else if (cmd == "get-children-by-id")
            {
                if (args.size() != 2)
                {
                    print_json_error(cmd, "usage: get-children-by-id <id>");
                    return false;
                }
                const std::string node_id = args[1];
                const auto &schemas = sm.registry().schemas();
                if (schemas.empty())
                {
                    print_json_ok(cmd, {{"count", 0}, {"rows", nlohmann::json::array()}});
                    return false;
                }

                std::set<NodeKey> childrenKeys;
                for (const auto &[profile, _schemaPtr] : schemas)
                {
                    NodeKey parentKey{profile, node_id};
                    if (!engine.exists(parentKey))
                        continue;
                    for (const auto &[childProfile, _childSchema] : schemas)
                    {
                        auto children = engine.childrenOf(parentKey, childProfile);
                        childrenKeys.insert(children.begin(), children.end());
                    }
                }

                nlohmann::json items = nlohmann::json::array();
                for (const auto &childKey : childrenKeys)
                {
                    auto snap = engine.fetch(childKey);
                    if (snap)
                        items.push_back(node_summary_json(*snap));
                    else
                        items.push_back({{"profile", childKey.profile}, {"id", childKey.id.value_or("")}});
                }
                print_json_ok(cmd, {{"count", childrenKeys.size()}, {"rows", items}});
            }
            else if (cmd == "get-refs-to-by-id")
            {
                if (args.size() != 2)
                {
                    print_json_error(cmd, "usage: get-refs-to-by-id <id>");
                    return false;
                }
                const std::string node_id = args[1];
                const auto &schemas = sm.registry().schemas();
                if (schemas.empty())
                {
                    print_json_ok(cmd, {{"count", 0}, {"rows", nlohmann::json::array()}});
                    return false;
                }

                std::set<NodeKey> refsKeys;
                for (const auto &[profile, _schemaPtr] : schemas)
                {
                    NodeKey targetKey{profile, node_id};
                    if (!engine.exists(targetKey))
                        continue;
                    auto refs = engine.refsTo(targetKey);
                    refsKeys.insert(refs.begin(), refs.end());
                }

                nlohmann::json items = nlohmann::json::array();
                for (const auto &refKey : refsKeys)
                {
                    auto snap = engine.fetch(refKey);
                    if (snap)
                        items.push_back(node_summary_json(*snap));
                    else
                        items.push_back({{"profile", refKey.profile}, {"id", refKey.id.value_or("")}});
                }
                print_json_ok(cmd, {{"count", refsKeys.size()}, {"rows", items}});
            }
            else if (cmd == "execute-command")
            {
                if (args.size() != 4)
                {
                    print_json_error(cmd, "usage: execute-command <profile> <id> <command>");
                    return false;
                }

                const std::string profile = args[1];
                const std::string node_id = args[2];
                const std::string command = args[3];

                const auto *schema = sm.getSchema(profile);
                if (!schema)
                {
                    print_json_error(cmd, "profile not found");
                    return false;
                }

                auto node_opt = engine.fetch(NodeKey{profile, node_id});
                if (!node_opt.has_value())
                {
                    print_json_error(cmd, "node not found");
                    return false;
                }

                auto manualCommands = schema->manualCustomCommands();
                auto it = manualCommands.find(command);
                if (it == manualCommands.end())
                {
                    print_json_error(cmd, "command not found");
                    return false;
                }

                auto script_name = it->second.script_name;
                auto script = luaRegistry.getScript(script_name);
                if (!script)
                {
                    print_json_error(cmd, "lua script not found: " + script_name);
                    return false;
                }

                if (!luaEngine.runScript(*script, NodeKey{profile, node_id}))
                {
                    print_json_error(cmd, luaEngine.getLastError());
                    return false;
                }

                flush_after_change();

                auto log = luaEngine.getLastLog();
                print_json_ok(cmd, {{"status", "OK"}, {"log", log}});
            }
            else if (cmd == "arr-add")
            {
                // usage:
                //   arr-add <profile> <id> <field> <value...>
                //   for array<string|reference>: value is a single token (quotes ok)
                //   for array<object>: either one JSON-ish literal, or several k=v pairs

                if (args.size() < 5)
                {
                    print_json_error(cmd, "usage: arr-add <profile> <id> <field> <value...>");
                    return false;
                }
                std::string profile = args[1], id = args[2], field = args[3];

                if (!sm.hasSchema(profile))
                {
                    print_json_error(cmd, "unknown profile: " + profile);
                    return false;
                }
                NodeKey key{profile, id};
                auto current = engine.fetch(key);
                if (!current)
                {
                    print_json_error(cmd, "not found");
                    return false;
                }

                const NodeSchema *sch = sm.registry().getSchema(profile);
                const FieldSchema *fs = sch->getField(field);
                if (!fs || fs->type() != FieldType::Array)
                {
                    print_json_error(cmd, "field is not an array: " + field);
                    return false;
                }
                auto as = dynamic_cast<const ArrayFieldSchema *>(fs);
                const FieldSchema *item = as->items();

                ArrayData arr;
                // take existing
                if (auto it = current->fields.find(field); it != current->fields.end() && it->second.isArray())
                    arr = it->second.asArray();

                // parse new element(s)
                if (item->type() == FieldType::String || item->type() == FieldType::Reference)
                {
                    // a single value token (already split respecting quotes)
                    std::string v = args[4];
                    if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\'')))
                        v = v.substr(1, v.size() - 2);
                    arr.emplace_back(FieldValue(v));
                }
                else if (item->type() == FieldType::Object)
                {
                    ObjectData obj;
                    if (args.size() == 5 && !args[4].empty() && args[4].front() == '{' && args[4].back() == '}')
                    {
                        obj = parse_object_literal(args[4]);
                    }
                    else
                    {
                        std::vector<std::string> kvs(args.begin() + 4, args.end());
                        obj = parse_object_kv(kvs);
                    }
                    arr.emplace_back(FieldValue(obj));
                }
                else
                {
                    print_json_error(cmd, "arr-add: unsupported array item type");
                    return false;
                }

                NodeSnapshot patch;
                patch.key = key;
                patch.fields[field] = FieldValue(arr);
                engine.upsert(patch);
                flush_after_change();
                auto saved = engine.fetch(key);
                if (saved)
                    print_json_ok(cmd, {{"node", node_to_json(*saved)}});
                else
                    print_json_error(cmd, "not found");
            }
            else if (cmd == "arr-del")
            {
                // arr-del <profile> <id> <field> index=<n> | value=<v>
                if (args.size() < 5)
                {
                    print_json_error(cmd, "usage: arr-del <profile> <id> <field> index=<n> | value=<v>");
                    return false;
                }
                std::string profile = args[1], id = args[2], field = args[3];
                std::optional<size_t> index;
                std::optional<std::string> value;

                for (size_t i = 4; i < args.size(); ++i)
                {
                    if (args[i].rfind("index=", 0) == 0)
                    {
                        index = static_cast<size_t>(std::stoul(args[i].substr(6)));
                    }
                    else if (args[i].rfind("value=", 0) == 0)
                    {
                        value = dequote(args[i].substr(6));
                    }
                }
                if (!index && !value)
                {
                    print_json_error(cmd, "specify index=<n> or value=<v>");
                    return false;
                }
                if (!sm.hasSchema(profile))
                {
                    print_json_error(cmd, "unknown profile: " + profile);
                    return false;
                }
                arr_del(engine, profile, id, field, index, value);
                flush_after_change();
                auto saved = engine.fetch(NodeKey{profile, id});
                if (saved)
                    print_json_ok(cmd, {{"node", node_to_json(*saved)}});
                else
                    print_json_error(cmd, "not found");
            }
            else if (cmd == "arr-set")
            {
                // arr-set <profile> <id> <selector>=<value>   e.g. tags[2]=timing, objects[1].name=sensor
                if (args.size() != 5)
                {
                    print_json_error(cmd, "usage: arr-set <profile> <id> <field>[i][.sub]=<value>");
                    return false;
                }
                std::string profile = args[1], id = args[2];
                std::string selectorEq = args[3] + "=" + args[4]; // keep '=' split flexibility
                if (!sm.hasSchema(profile))
                {
                    print_json_error(cmd, "unknown profile: " + profile);
                    return false;
                }
                arr_set(engine, profile, id, selectorEq);
                flush_after_change();
                auto saved = engine.fetch(NodeKey{profile, id});
                if (saved)
                    print_json_ok(cmd, {{"node", node_to_json(*saved)}});
                else
                    print_json_error(cmd, "not found");
            }
            // get-field <profile> <id> <field>
            else if (cmd == "get-field")
            {
                if (args.size() != 4)
                {
                    print_json_error(cmd, "usage: get-field <profile> <id> <field>");
                    return false;
                }
                auto snap = engine.fetch(NodeKey{args[1], args[2]});
                if (!snap)
                {
                    print_json_error(cmd, "not found");
                    return false;
                }
                auto it = snap->fields.find(args[3]);
                if (it == snap->fields.end())
                {
                    print_json_error(cmd, "missing");
                    return false;
                }
                print_json_ok(cmd, {{"value", field_value_to_json(it->second)}});
            }

            // get-select <profile> <id> <field>[i][.sub]
            else if (cmd == "get-select")
            {
                if (args.size() != 4)
                {
                    print_json_error(cmd, "usage: get-select <profile> <id> <field>[i][.sub]");
                    return false;
                }
                auto sel = parse_selector(args[3]); // you already have parse_selector()
                if (!sel || !sel->index)
                {
                    print_json_error(cmd, "bad selector or missing index");
                    return false;
                }

                auto snap = engine.fetch(NodeKey{args[1], args[2]});
                if (!snap)
                {
                    print_json_error(cmd, "not found");
                    return false;
                }

                auto it = snap->fields.find(sel->field);
                if (it == snap->fields.end() || !it->second.isArray())
                {
                    print_json_error(cmd, "field not found or not an array");
                    return false;
                }
                const auto &arr = it->second.asArray();
                if (*sel->index >= arr.size())
                {
                    print_json_error(cmd, "index out of range");
                    return false;
                }

                const FieldValue &el = arr[*sel->index];
                if (sel->sub)
                {
                    if (!el.isObject())
                    {
                        print_json_error(cmd, "element is not an object");
                        return false;
                    }
                    const auto &obj = el.asObject();
                    auto it2 = obj.find(*sel->sub);
                    if (it2 == obj.end())
                    {
                        print_json_error(cmd, "missing");
                        return false;
                    }
                    print_json_ok(cmd, {{"value", field_value_to_json(it2->second)}});
                }
                else
                {
                    print_json_ok(cmd, {{"value", field_value_to_json(el)}});
                }
            }
            else
            {
                print_json_error(cmd, "unknown command");
            }
        }
        catch (const std::exception &e)
        {
            print_json_error(cmd, e.what());
        }

        return false;
    };

    // Parse arguments and decide interactive vs non-interactive
    bool interactive = false;
    std::optional<std::string> schemas_dir;
    std::optional<std::string> data_dir;
    std::optional<std::string> scripts_dir;
    std::vector<std::string> cmd_args;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "-it" || arg == "--interactive")
        {
            interactive = true;
            continue;
        }
        if (arg == "--schemas" && i + 1 < argc)
        {
            schemas_dir = argv[++i];
            continue;
        }
        if (arg == "--data" && i + 1 < argc)
        {
            data_dir = argv[++i];
            continue;
        }
        if (arg == "--scripts" && i + 1 < argc)
        {
            scripts_dir = argv[++i];
            continue;
        }
        if (arg == "--help" || arg == "-h")
        {
            print_json_ok("help", {{"text", help_text()}});
            return 0;
        }
        // Remaining tokens constitute the command
        cmd_args.assign(argv + i, argv + argc);
        break;
    }

    if (!interactive)
    {
        if (cmd_args.empty())
        {
            print_json_error("args", "missing command (use --interactive for interactive mode)");
            print_json_ok("help", {{"text", help_text()}});
            return 1;
        }

        // Apply environment defaults if flags were not provided
        if (!schemas_dir)
        {
            if (const char *p = std::getenv("HEPHORA_SCHEMAS"))
                schemas_dir = std::string(p);
        }
        if (!data_dir)
        {
            if (const char *p = std::getenv("HEPHORA_DATA"))
                data_dir = std::string(p);
        }
        if (!scripts_dir)
        {
            if (const char *p = std::getenv("HEPHORA_SCRIPTS"))
                scripts_dir = std::string(p);
        }

        if (!schemas_dir || !data_dir || !scripts_dir)
        {
            print_json_error("args", "non-interactive mode requires --schemas, --data, and --scripts (or set env vars)");
            print_json_ok("help", {{"text", help_text()}});
            return 1;
        }

        {
            std::error_code ec;
            std::filesystem::current_path(*data_dir, ec);
            if (ec)
            {
                print_json_error("args", "failed to set data directory: " + ec.message());
                return 1;
            }
        }

        // Preload workspace
        auto schemas = load_schema_sources_dir(*schemas_dir);
        sm.loadSources(schemas);
        engine.init(sm.registry());
        auto data = load_data_docs_dir(*data_dir);
        engine.loadData(data);
        luaRegistry.clear();
        luaRegistry.loadScripts(load_script_sources_dir(*scripts_dir));

        std::string line;
        for (size_t i = 0; i < cmd_args.size(); ++i)
        {
            if (i)
                line += " ";
            line += cmd_args[i];
        }

        execute_command(cmd_args, line);
        return 0;
    }

    // Interactive mode
    std::cerr << "hephora-sdk-cli — interactive mode. Type 'help' to see commands." << std::endl;

    std::string line;
    while (true)
    {
        std::cerr << "> " << std::flush;
        if (!std::getline(std::cin, line))
            break;
        line = trim(line);
        if (line.empty())
            continue;

        auto args = split_args_quotes(line);
        if (execute_command(args, line))
            break;
    }

    return 0;
}
catch (const std::exception &e)
{
    std::cerr << "fatal: " << e.what() << "\n";
    return 1;
}
