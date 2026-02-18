#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

#ifndef CLI_EXE_PATH
#define CLI_EXE_PATH ""
#endif

struct Options
{
    std::string format = "tree";
    std::string schemasDir;
    std::string dataDir;
    std::string scriptsDir;
    std::string profile;
    std::optional<std::string> id;
    int maxDepth = 6;
    bool showAllFields = false;
    bool noColor = false;
    bool includeRefs = false;
    bool refsOnly = false;
    std::optional<std::string> queryJson;
};

struct RefEdge
{
    std::string fromProfile;
    std::string fromId;
    std::string toProfile;
    std::string toId;
};

static std::string node_key(const std::string &profile, const std::string &id)
{
    return profile + ":" + id;
}

struct Colors
{
    bool enabled = false;
    std::string reset;
    std::string header;
    std::string edge;
    std::string profile;
    std::string id;
    std::string label;
    std::string warn;
};

static Colors make_colors(const Options &opt)
{
    Colors c;
    bool enable = !opt.noColor && (::isatty(fileno(stdout)) != 0);

    c.enabled = enable;
    if (!enable)
        return c;

    c.reset = "\x1b[0m";
    c.header = "\x1b[1;36m";
    c.edge = "\x1b[2m";
    c.profile = "\x1b[1;33m";
    c.id = "\x1b[1;32m";
    c.label = "\x1b[0;37m";
    c.warn = "\x1b[1;33m";
    return c;
}

static std::string trim(std::string s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c)
                                    { return !std::isspace(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c)
                         { return !std::isspace(c); })
                .base(),
            s.end());
    return s;
}

static std::string shell_quote(const std::string &s)
{
    std::string out = "'";
    for (char c : s)
    {
        if (c == '\'')
            out += "'\\''";
        else
            out.push_back(c);
    }
    out += "'";
    return out;
}

static std::string run_capture(const std::string &cmd)
{
    std::string full = cmd + " 2>&1";
    FILE *pipe = popen(full.c_str(), "r");
    if (!pipe)
        throw std::runtime_error("failed to execute command");

    std::string out;
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
        out += buffer;

    int rc = pclose(pipe);
    if (rc != 0)
        throw std::runtime_error("command failed: " + trim(out));

    return out;
}

static nlohmann::json run_cli_json(const Options &opt, const std::string &command)
{
    std::string cli;

    // 1) Explicit override for installed/container setups
    if (const char *envCli = std::getenv("HEPHORA_CLI_BIN"); envCli && *envCli)
    {
        cli = envCli;
    }
    else
    {
        // 2) Build-time path (works in build tree)
        cli = CLI_EXE_PATH;

        // If compiled absolute build path is not present (e.g. installed binary), ignore it.
        if (!cli.empty())
        {
            std::error_code ec;
            std::filesystem::path p(cli);
            if (p.is_absolute() && !std::filesystem::exists(p, ec))
                cli.clear();
        }
    }

    // 3) Runtime PATH fallback
    if (cli.empty())
        cli = "hephora-sdk-cli";

    std::ostringstream oss;
    oss << shell_quote(cli)
        << " --schemas " << shell_quote(opt.schemasDir)
        << " --data " << shell_quote(opt.dataDir)
        << " --scripts " << shell_quote(opt.scriptsDir)
        << " " << command;

    const auto out = run_capture(oss.str());
    auto json = nlohmann::json::parse(out);
    if (!json.contains("ok") || !json["ok"].get<bool>())
    {
        std::string err = json.contains("error") ? json["error"].get<std::string>() : "unknown CLI error";
        throw std::runtime_error(err);
    }
    return json;
}

static std::string node_label(const nlohmann::json &row)
{
    const std::string profile = row.value("profile", "?");
    const std::string id = row.value("id", "?");
    const std::string label = row.value("label", "");

    if (!label.empty())
        return profile + "/" + id + " (" + label + ")";
    return profile + "/" + id;
}

static std::string node_label_colored(const nlohmann::json &row, const Colors &colors)
{
    if (!colors.enabled)
        return node_label(row);

    const std::string profile = row.value("profile", "?");
    const std::string id = row.value("id", "?");
    const std::string label = row.value("label", "");

    std::string out = colors.profile + profile + colors.reset + "/" + colors.id + id + colors.reset;
    if (!label.empty())
        out += " (" + colors.label + label + colors.reset + ")";
    return out;
}

static nlohmann::json build_node_payload(const nlohmann::json &node, bool showAllFields)
{
    nlohmann::json out = {
        {"profile", node.value("profile", "")},
        {"id", node.value("id", "")},
        {"label", node.value("label", "")},
        {"parent", node.value("parent", "")}};

    if (showAllFields)
    {
        out["fields"] = node.contains("fields") ? node["fields"] : nlohmann::json::object();
        out["_profile"] = node.value("profile", "");
        out["_id"] = node.value("id", "");
        out["_label"] = node.value("label", "");
        out["_parent_id"] = node.value("parent", "");
    }

    return out;
}

static std::vector<nlohmann::json> get_children(const Options &opt, const std::string &profile, const std::string &id)
{
    auto res = run_cli_json(opt, "get-children " + profile + " " + id);
    if (!res.contains("result") || !res["result"].contains("rows") || !res["result"]["rows"].is_array())
        return {};

    std::vector<nlohmann::json> rows;
    for (const auto &r : res["result"]["rows"])
        rows.push_back(r);
    return rows;
}

static std::vector<nlohmann::json> get_ref_sources_to_id(const Options &opt, const std::string &id)
{
    auto res = run_cli_json(opt, "get-refs-to-by-id " + id);
    if (!res.contains("result") || !res["result"].contains("rows") || !res["result"]["rows"].is_array())
        return {};

    std::vector<nlohmann::json> rows;
    for (const auto &r : res["result"]["rows"])
        rows.push_back(r);
    return rows;
}

static std::vector<nlohmann::json> list_all_nodes(const Options &opt)
{
    std::vector<nlohmann::json> nodes;
    auto profiles = run_cli_json(opt, "get-profiles");
    if (!profiles.contains("result") || !profiles["result"].contains("profiles") || !profiles["result"]["profiles"].is_array())
        return nodes;

    for (const auto &p : profiles["result"]["profiles"])
    {
        const std::string profile = p.value("name", "");
        if (profile.empty())
            continue;

        auto listed = run_cli_json(opt, "list " + profile);
        if (!listed.contains("result") || !listed["result"].contains("rows") || !listed["result"]["rows"].is_array())
            continue;

        for (const auto &row : listed["result"]["rows"])
            nodes.push_back(row);
    }

    return nodes;
}

static std::vector<nlohmann::json> query_nodes(const Options &opt, const std::string &payload)
{
    auto res = run_cli_json(opt, "query " + shell_quote(payload));
    if (!res.contains("result") || !res["result"].contains("rows") || !res["result"]["rows"].is_array())
        return {};

    std::vector<nlohmann::json> rows;
    for (const auto &r : res["result"]["rows"])
        rows.push_back(r);
    return rows;
}

static std::vector<RefEdge> build_ref_edges_for_targets(const Options &opt, const std::vector<nlohmann::json> &targets)
{
    std::vector<RefEdge> edges;
    std::set<std::string> seen;

    for (const auto &target : targets)
    {
        const std::string toProfile = target.value("profile", "");
        const std::string toId = target.value("id", "");
        if (toProfile.empty() || toId.empty())
            continue;

        auto refs = get_ref_sources_to_id(opt, toId);
        for (const auto &src : refs)
        {
            const std::string fromProfile = src.value("profile", "");
            const std::string fromId = src.value("id", "");
            if (fromProfile.empty() || fromId.empty())
                continue;

            const std::string k = fromProfile + ":" + fromId + "->" + toProfile + ":" + toId;
            if (!seen.insert(k).second)
                continue;

            edges.push_back({fromProfile, fromId, toProfile, toId});
        }
    }

    return edges;
}

static nlohmann::json ref_edges_to_json(const std::vector<RefEdge> &edges)
{
    nlohmann::json out = nlohmann::json::array();
    for (const auto &e : edges)
    {
        out.push_back({
            {"kind", "ref"},
            {"from", {{"profile", e.fromProfile}, {"id", e.fromId}}},
            {"to", {{"profile", e.toProfile}, {"id", e.toId}}}});
    }
    return out;
}

static void collect_tree_targets(const nlohmann::json &node, std::vector<nlohmann::json> &targets)
{
    targets.push_back(node);
    if (!node.contains("children") || !node["children"].is_array())
        return;
    for (const auto &child : node["children"])
        collect_tree_targets(child, targets);
}

static void print_tree(const Options &opt,
                       const std::string &profile,
                       const std::string &id,
                       const std::string &prefix,
                       bool isLast,
                       int depth,
                       std::set<std::string> &visited,
                       const Colors &colors)
{
    const std::string key = profile + ":" + id;
    if (visited.count(key))
    {
        if (colors.enabled)
            std::cout << prefix << colors.edge << (isLast ? "└─ " : "├─ ") << colors.reset << colors.profile << profile << colors.reset << "/" << colors.id << id << colors.reset << " (cycle)" << '\n';
        else
            std::cout << prefix << (isLast ? "└─ " : "├─ ") << profile << "/" << id << " (cycle)" << '\n';
        return;
    }
    visited.insert(key);

    if (depth > opt.maxDepth)
        return;

    auto current = run_cli_json(opt, "get " + profile + " " + id);
    auto node = current["result"]["node"];

    if (colors.enabled)
        std::cout << prefix << colors.edge << (isLast ? "└─ " : "├─ ") << colors.reset << node_label_colored(node, colors) << '\n';
    else
        std::cout << prefix << (isLast ? "└─ " : "├─ ") << node_label(node) << '\n';

    if (opt.includeRefs)
    {
        auto refs = get_ref_sources_to_id(opt, id);
        for (const auto &src : refs)
        {
            const std::string fromProfile = src.value("profile", "");
            const std::string fromId = src.value("id", "");
            const std::string fromLabel = src.value("label", "");
            if (fromProfile.empty() || fromId.empty())
                continue;

            std::string line = "↳ ref " + fromProfile + "/" + fromId;
            if (!fromLabel.empty())
                line += " (" + fromLabel + ")";
            line += " -> " + profile + "/" + id;

            if (colors.enabled)
                std::cout << prefix << "   " << colors.edge << line << colors.reset << '\n';
            else
                std::cout << prefix << "   " << line << '\n';
        }
    }

    auto children = get_children(opt, profile, id);
    for (size_t i = 0; i < children.size(); ++i)
    {
        const bool childLast = (i + 1 == children.size());
        std::string childPrefix = prefix + (isLast ? "   " : "│  ");
        std::string childProfile = children[i].value("profile", "");
        std::string childId = children[i].value("id", "");
        if (childProfile.empty() || childId.empty())
            continue;
        print_tree(opt, childProfile, childId, childPrefix, childLast, depth + 1, visited, colors);
    }
}

static nlohmann::json build_tree_json(const Options &opt,
                                      const std::string &profile,
                                      const std::string &id,
                                      int depth,
                                      std::set<std::string> &visited)
{
    const std::string key = profile + ":" + id;
    if (visited.count(key))
    {
        return {
            {"profile", profile},
            {"id", id},
            {"cycle", true},
            {"children", nlohmann::json::array()}};
    }
    visited.insert(key);

    if (depth > opt.maxDepth)
    {
        return {
            {"profile", profile},
            {"id", id},
            {"truncated", true},
            {"children", nlohmann::json::array()}};
    }

    auto current = run_cli_json(opt, "get " + profile + " " + id);
    auto node = current["result"]["node"];

    nlohmann::json out = build_node_payload(node, opt.showAllFields);
    out["children"] = nlohmann::json::array();

    auto children = get_children(opt, profile, id);
    for (const auto &child : children)
    {
        std::string childProfile = child.value("profile", "");
        std::string childId = child.value("id", "");
        if (childProfile.empty() || childId.empty())
            continue;
        out["children"].push_back(build_tree_json(opt, childProfile, childId, depth + 1, visited));
    }

    return out;
}

static void print_help()
{
    std::cout << "hephora-sdk-viz\n"
              << "Usage:\n"
              << "  hephora-sdk-viz [--format tree|json] [--show-all-fields] [--include-refs] [--refs-only] [--no-color] [--query <json>] [--schemas <dir>] [--data <dir>] [--scripts <dir>] [--profile <name>] [--id <node-id>] [--max-depth <n>]\n\n"
              << "Behavior:\n"
              << "  - Runs hephora-sdk-cli in non-interactive mode.\n"
              << "  - Optional CLI override: HEPHORA_CLI_BIN=/path/to/hephora-sdk-cli\n"
              << "  - Paths can be passed with flags or inherited from env vars:\n"
              << "      HEPHORA_SCHEMAS, HEPHORA_DATA, HEPHORA_SCRIPTS\n"
              << "  - If --profile and --id are provided, renders that node subtree.\n"
              << "  - If omitted, it discovers root profile via 'schemas' and renders all root nodes via 'list <root>'.\n"
              << "  - --show-all-fields is supported only with --format json and includes: _profile, _id, _label, _parent_id.\n"
              << "  - --query passes JSON directly to CLI 'query' and uses returned rows as traversal roots.\n"
              << "  - --include-refs adds reference links in output.\n"
              << "  - --refs-only renders only reference links (implies --include-refs).\n"
              << "  - Colors are applied in tree mode when output is a terminal; use --no-color to disable.\n";
}

static Options parse_args(int argc, char **argv)
{
    Options opt;
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if ((arg == "--help") || (arg == "-h"))
        {
            print_help();
            std::exit(0);
        }
        else if (arg == "--schemas" && i + 1 < argc)
            opt.schemasDir = argv[++i];
        else if (arg == "--data" && i + 1 < argc)
            opt.dataDir = argv[++i];
        else if (arg == "--scripts" && i + 1 < argc)
            opt.scriptsDir = argv[++i];
        else if (arg == "--profile" && i + 1 < argc)
            opt.profile = argv[++i];
        else if (arg == "--id" && i + 1 < argc)
            opt.id = std::string(argv[++i]);
        else if (arg == "--max-depth" && i + 1 < argc)
            opt.maxDepth = std::max(0, std::atoi(argv[++i]));
        else if (arg == "--format" && i + 1 < argc)
            opt.format = argv[++i];
        else if (arg == "--query" && i + 1 < argc)
            opt.queryJson = std::string(argv[++i]);
        else if (arg == "--show-all-fields")
            opt.showAllFields = true;
        else if (arg == "--include-refs")
            opt.includeRefs = true;
        else if (arg == "--refs-only")
            opt.refsOnly = true;
        else if (arg == "--no-color")
            opt.noColor = true;
        else
            throw std::runtime_error("unknown argument: " + arg);
    }

    if (opt.format != "tree" && opt.format != "json")
        throw std::runtime_error("--format must be one of: tree, json");

    if (opt.schemasDir.empty())
    {
        if (const char *p = std::getenv("HEPHORA_SCHEMAS"))
            opt.schemasDir = p;
    }
    if (opt.dataDir.empty())
    {
        if (const char *p = std::getenv("HEPHORA_DATA"))
            opt.dataDir = p;
    }
    if (opt.scriptsDir.empty())
    {
        if (const char *p = std::getenv("HEPHORA_SCRIPTS"))
            opt.scriptsDir = p;
    }

    if (opt.schemasDir.empty() || opt.dataDir.empty() || opt.scriptsDir.empty())
        throw std::runtime_error("provide --schemas, --data, --scripts or set HEPHORA_SCHEMAS, HEPHORA_DATA, HEPHORA_SCRIPTS");

    if (opt.id.has_value() && opt.profile.empty())
        throw std::runtime_error("--id requires --profile");

    if (opt.queryJson.has_value() && !opt.profile.empty())
        throw std::runtime_error("--query cannot be used with --profile/--id");

    if (opt.refsOnly)
        opt.includeRefs = true;

    return opt;
}

int main(int argc, char **argv)
{
    try
    {
        const auto opt = parse_args(argc, argv);
        const auto colors = make_colors(opt);

        if (opt.showAllFields && opt.format != "json")
        {
            if (colors.enabled)
                std::cerr << colors.warn << "warning: --show-all-fields is only applied for --format json" << colors.reset << '\n';
            else
                std::cerr << "warning: --show-all-fields is only applied for --format json\n";
        }

        std::set<std::string> visited;

        if (opt.refsOnly)
        {
            std::vector<nlohmann::json> targets = opt.queryJson.has_value()
                                                    ? query_nodes(opt, *opt.queryJson)
                                                    : list_all_nodes(opt);
            if (!opt.profile.empty() && opt.id.has_value())
            {
                const std::string wanted = node_key(opt.profile, *opt.id);
                std::vector<nlohmann::json> filtered;
                for (const auto &n : targets)
                {
                    if (node_key(n.value("profile", ""), n.value("id", "")) == wanted)
                        filtered.push_back(n);
                }
                if (!filtered.empty())
                    targets = filtered;
            }

            auto edges = build_ref_edges_for_targets(opt, targets);

            if (!opt.profile.empty() && opt.id.has_value())
            {
                const std::string wanted = node_key(opt.profile, *opt.id);
                std::vector<RefEdge> filtered;
                for (const auto &e : edges)
                {
                    if (node_key(e.fromProfile, e.fromId) == wanted || node_key(e.toProfile, e.toId) == wanted)
                        filtered.push_back(e);
                }
                edges = std::move(filtered);
            }

            if (opt.format == "json")
            {
                nlohmann::json out = {
                    {"ok", true},
                    {"format", "json"},
                    {"mode", "refs-only"},
                    {"edges", ref_edges_to_json(edges)}};
                std::cout << out.dump(2) << '\n';
            }
            else
            {
                if (colors.enabled)
                    std::cout << colors.header << "Hephora References" << colors.reset << '\n';
                else
                    std::cout << "Hephora References" << '\n';

                for (const auto &e : edges)
                {
                    std::string line = e.fromProfile + "/" + e.fromId + " -> " + e.toProfile + "/" + e.toId;
                    if (colors.enabled)
                        std::cout << colors.edge << "├─ " << colors.reset << line << '\n';
                    else
                        std::cout << "├─ " << line << '\n';
                }
            }
            return 0;
        }

        if (!opt.profile.empty() && opt.id.has_value())
        {
            if (opt.format == "json")
            {
                auto root = build_tree_json(opt, opt.profile, *opt.id, 0, visited);
                nlohmann::json out = {
                    {"ok", true},
                    {"format", "json"},
                    {"root", root}};
                if (opt.includeRefs)
                {
                    std::vector<nlohmann::json> targets;
                    collect_tree_targets(root, targets);
                    out["edges"] = ref_edges_to_json(build_ref_edges_for_targets(opt, targets));
                }
                std::cout << out.dump(2) << '\n';
            }
            else
            {
                if (colors.enabled)
                    std::cout << colors.header << "Hephora Tree" << colors.reset << '\n';
                else
                    std::cout << "Hephora Tree" << '\n';
                print_tree(opt, opt.profile, *opt.id, "", true, 0, visited, colors);
            }
            return 0;
        }

        auto schemas = run_cli_json(opt, "schemas");
        std::string rootProfile = schemas["result"].value("root", "");
        if (rootProfile.empty())
            throw std::runtime_error("root profile not found");

        nlohmann::json rows = nlohmann::json::array();
        if (opt.queryJson.has_value())
        {
            for (const auto &r : query_nodes(opt, *opt.queryJson))
                rows.push_back(r);
        }
        else
        {
            auto roots = run_cli_json(opt, "list " + rootProfile);
            if (!roots.contains("result") || !roots["result"].contains("rows") || !roots["result"]["rows"].is_array())
                throw std::runtime_error("list root failed");
            rows = roots["result"]["rows"];
        }

        if (opt.format == "json")
        {
            nlohmann::json out = {
                {"ok", true},
                {"format", "json"},
                {"root_profile", rootProfile},
                {"nodes", nlohmann::json::array()}};

            if (opt.queryJson.has_value())
                out["query"] = *opt.queryJson;

            std::vector<nlohmann::json> targets;

            for (const auto &row : rows)
            {
                std::string profile = row.value("profile", rootProfile);
                std::string id = row.value("id", "");
                if (id.empty())
                    continue;
                auto n = build_tree_json(opt, profile, id, 0, visited);
                out["nodes"].push_back(n);
                collect_tree_targets(n, targets);
            }
            if (opt.includeRefs)
                out["edges"] = ref_edges_to_json(build_ref_edges_for_targets(opt, targets));
            std::cout << out.dump(2) << '\n';
        }
        else
        {
            if (opt.queryJson.has_value())
            {
                if (colors.enabled)
                    std::cout << colors.header << "Hephora Tree (query)" << colors.reset << '\n';
                else
                    std::cout << "Hephora Tree (query)" << '\n';
            }
            else
            {
                if (colors.enabled)
                    std::cout << colors.header << "Hephora Tree (root profile: " << rootProfile << ")" << colors.reset << '\n';
                else
                    std::cout << "Hephora Tree (root profile: " << rootProfile << ")" << '\n';
            }
            for (size_t i = 0; i < rows.size(); ++i)
            {
                std::string profile = rows[i].value("profile", rootProfile);
                std::string id = rows[i].value("id", "");
                if (id.empty())
                    continue;
                bool isLast = (i + 1 == rows.size());
                print_tree(opt, profile, id, "", isLast, 0, visited, colors);
            }
        }

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
