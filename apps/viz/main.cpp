#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef CLI_EXE_PATH
#define CLI_EXE_PATH ""
#endif

struct Options
{
    std::string schemasDir;
    std::string dataDir;
    std::string scriptsDir;
    std::string profile;
    std::optional<std::string> id;
    int maxDepth = 6;
};

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
    std::string cli = CLI_EXE_PATH;
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

static void print_tree(const Options &opt,
                       const std::string &profile,
                       const std::string &id,
                       const std::string &prefix,
                       bool isLast,
                       int depth,
                       std::set<std::string> &visited)
{
    const std::string key = profile + ":" + id;
    if (visited.count(key))
    {
        std::cout << prefix << (isLast ? "└─ " : "├─ ") << profile << "/" << id << " (cycle)" << '\n';
        return;
    }
    visited.insert(key);

    if (depth > opt.maxDepth)
        return;

    auto current = run_cli_json(opt, "get " + profile + " " + id);
    auto node = current["result"]["node"];

    std::cout << prefix << (isLast ? "└─ " : "├─ ") << node_label(node) << '\n';

    auto children = get_children(opt, profile, id);
    for (size_t i = 0; i < children.size(); ++i)
    {
        const bool childLast = (i + 1 == children.size());
        std::string childPrefix = prefix + (isLast ? "   " : "│  ");
        std::string childProfile = children[i].value("profile", "");
        std::string childId = children[i].value("id", "");
        if (childProfile.empty() || childId.empty())
            continue;
        print_tree(opt, childProfile, childId, childPrefix, childLast, depth + 1, visited);
    }
}

static void print_help()
{
    std::cout << "hephora-sdk-viz\n"
              << "Usage:\n"
              << "  hephora-sdk-viz [--schemas <dir>] [--data <dir>] [--scripts <dir>] [--profile <name>] [--id <node-id>] [--max-depth <n>]\n\n"
              << "Behavior:\n"
              << "  - Runs hephora-sdk-cli in non-interactive mode.\n"
              << "  - Paths can be passed with flags or inherited from env vars:\n"
              << "      HEPHORA_SCHEMAS, HEPHORA_DATA, HEPHORA_SCRIPTS\n"
              << "  - If --profile and --id are provided, renders that node subtree.\n"
              << "  - If omitted, it discovers root profile via 'schemas' and renders all root nodes via 'list <root>'.\n";
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
        else
            throw std::runtime_error("unknown argument: " + arg);
    }

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

    return opt;
}

int main(int argc, char **argv)
{
    try
    {
        const auto opt = parse_args(argc, argv);

        std::set<std::string> visited;

        if (!opt.profile.empty() && opt.id.has_value())
        {
            std::cout << "Hephora Tree" << '\n';
            print_tree(opt, opt.profile, *opt.id, "", true, 0, visited);
            return 0;
        }

        auto schemas = run_cli_json(opt, "schemas");
        std::string rootProfile = schemas["result"].value("root", "");
        if (rootProfile.empty())
            throw std::runtime_error("root profile not found");

        auto roots = run_cli_json(opt, "list " + rootProfile);
        if (!roots.contains("result") || !roots["result"].contains("rows") || !roots["result"]["rows"].is_array())
            throw std::runtime_error("list root failed");

        std::cout << "Hephora Tree (root profile: " << rootProfile << ")" << '\n';
        auto rows = roots["result"]["rows"];
        for (size_t i = 0; i < rows.size(); ++i)
        {
            std::string id = rows[i].value("id", "");
            if (id.empty())
                continue;
            bool isLast = (i + 1 == rows.size());
            print_tree(opt, rootProfile, id, "", isLast, 0, visited);
        }

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
