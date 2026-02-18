#include <catch2/catch_all.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <sys/wait.h>

#ifndef VIZ_EXE_PATH
#define VIZ_EXE_PATH ""
#endif

namespace fs = std::filesystem;

static fs::path make_temp_dir()
{
    auto base = fs::temp_directory_path();
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    fs::path dir = base / ("hephora_viz_test_" + std::to_string(now));
    fs::create_directories(dir);
    return dir;
}

static void write_file(const fs::path &path, const std::string &content)
{
    std::ofstream out(path, std::ios::binary);
    if (!out)
        throw std::runtime_error("cannot write file: " + path.string());
    out << content;
}

static int normalized_exit_code(int status)
{
    if (status == -1)
        return -1;
#if defined(WIFEXITED) && defined(WEXITSTATUS)
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
#endif
    return status;
}

static std::string run_cmd(const std::string &cmd, const fs::path &out_file)
{
    std::string full = cmd + " > \"" + out_file.string() + "\" 2>&1";
    int status = std::system(full.c_str());
    int code = normalized_exit_code(status);
    if (code != 0)
    {
        std::ifstream in(out_file, std::ios::binary);
        std::ostringstream oss;
        oss << in.rdbuf();
        throw std::runtime_error("command failed (" + std::to_string(code) + "): " + cmd + "\n" + oss.str());
    }

    std::ifstream in(out_file, std::ios::binary);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

static std::pair<int, std::string> run_cmd_allow_fail(const std::string &cmd, const fs::path &out_file)
{
    std::string full = cmd + " > \"" + out_file.string() + "\" 2>&1";
    int status = std::system(full.c_str());
    int code = normalized_exit_code(status);

    std::ifstream in(out_file, std::ios::binary);
    std::ostringstream oss;
    oss << in.rdbuf();
    return {code, oss.str()};
}

TEST_CASE("hephora-sdk-viz basic behavior", "[viz]")
{
    const std::string viz = VIZ_EXE_PATH;
    REQUIRE_FALSE(viz.empty());

    fs::path tmp = make_temp_dir();
    fs::path schemas = tmp / "schemas";
    fs::path data = tmp / "data";
    fs::path scripts = tmp / "scripts";
    fs::create_directories(schemas);
    fs::create_directories(data);
    fs::create_directories(scripts);

    write_file(schemas / "project.yaml",
               "name: project\n"
               "kind: root\n"
               "description: Project\n"
               "fields:\n"
               "  title:\n"
               "    type: string\n"
               "children:\n"
               "  attachments:\n"
               "    node: attachment\n"
               "  requirements:\n"
               "    node: requirement\n");

    write_file(schemas / "attachment.yaml",
               "name: attachment\n"
               "kind: node\n"
               "description: Attachment\n"
               "fields:\n"
               "  filename:\n"
               "    type: string\n");

    write_file(schemas / "requirement.yaml",
               "name: requirement\n"
               "kind: node\n"
               "description: Requirement\n"
               "fields:\n"
               "  title:\n"
               "    type: string\n"
               "  attachment_ref:\n"
               "    type: reference\n"
               "    target: attachment\n");

    write_file(data / "project.yaml",
               "_profile: project\n"
               "_id: P-1\n"
               "_label: Demo Project\n"
               "title: Demo\n");

    write_file(data / "attachment.yaml",
               "_profile: attachment\n"
               "_id: A-1\n"
               "_label: Spec PDF\n"
               "_parent_id: P-1\n"
               "filename: spec.pdf\n");

    write_file(data / "requirement.yaml",
               "_profile: requirement\n"
               "_id: R-1\n"
               "_label: Req One\n"
               "_parent_id: P-1\n"
               "title: Must link attachment\n"
               "attachment_ref: A-1\n");

    fs::path out = tmp / "out.txt";

    SECTION("help")
    {
        auto text = run_cmd("\"" + viz + "\" --help", out);
        REQUIRE(text.find("hephora-sdk-viz") != std::string::npos);
        REQUIRE(text.find("Usage:") != std::string::npos);
    }

    SECTION("missing required paths")
    {
        auto [code, text] = run_cmd_allow_fail("\"" + viz + "\" --schemas ./x", out);
        REQUIRE(code != 0);
        REQUIRE(text.find("provide --schemas, --data, --scripts or set HEPHORA_SCHEMAS, HEPHORA_DATA, HEPHORA_SCRIPTS") != std::string::npos);
    }

    SECTION("uses env path defaults")
    {
        setenv("HEPHORA_SCHEMAS", schemas.string().c_str(), 1);
        setenv("HEPHORA_DATA", data.string().c_str(), 1);
        setenv("HEPHORA_SCRIPTS", scripts.string().c_str(), 1);

        auto text = run_cmd("\"" + viz + "\"", out);
        REQUIRE(text.find("Hephora Tree") != std::string::npos);
        REQUIRE(text.find("project/P-1") != std::string::npos);

        unsetenv("HEPHORA_SCHEMAS");
        unsetenv("HEPHORA_DATA");
        unsetenv("HEPHORA_SCRIPTS");
    }

    SECTION("renders root tree")
    {
        std::string cmd = "\"" + viz + "\" --schemas \"" + schemas.string() + "\" --data \"" + data.string() + "\" --scripts \"" + scripts.string() + "\"";
        auto text = run_cmd(cmd, out);
        REQUIRE(text.find("Hephora Tree") != std::string::npos);
        REQUIRE(text.find("project/P-1") != std::string::npos);
        REQUIRE(text.find("attachment/A-1") != std::string::npos);
        REQUIRE(text.find("requirement/R-1") != std::string::npos);
    }

    SECTION("include-refs in json adds ref edges")
    {
        std::string cmd = "\"" + viz + "\" --format json --include-refs --schemas \"" + schemas.string() + "\" --data \"" + data.string() + "\" --scripts \"" + scripts.string() + "\"";
        auto text = run_cmd(cmd, out);
        auto j = nlohmann::json::parse(text);
        REQUIRE(j["ok"].get<bool>());
        REQUIRE(j.contains("edges"));
        REQUIRE(j["edges"].is_array());

        bool found_req_to_att = false;
        for (const auto &e : j["edges"])
        {
            if (e["kind"].get<std::string>() == "ref" &&
                e["from"]["profile"].get<std::string>() == "requirement" &&
                e["from"]["id"].get<std::string>() == "R-1" &&
                e["to"]["profile"].get<std::string>() == "attachment" &&
                e["to"]["id"].get<std::string>() == "A-1")
            {
                found_req_to_att = true;
                break;
            }
        }
        REQUIRE(found_req_to_att);
    }

    SECTION("refs-only json outputs reference edges")
    {
        std::string cmd = "\"" + viz + "\" --format json --refs-only --schemas \"" + schemas.string() + "\" --data \"" + data.string() + "\" --scripts \"" + scripts.string() + "\"";
        auto text = run_cmd(cmd, out);
        auto j = nlohmann::json::parse(text);
        REQUIRE(j["ok"].get<bool>());
        REQUIRE(j["mode"].get<std::string>() == "refs-only");
        REQUIRE(j.contains("edges"));
        REQUIRE(j["edges"].is_array());
        REQUIRE_FALSE(j["edges"].empty());
    }

    SECTION("no-color option is accepted")
    {
        std::string cmd = "\"" + viz + "\" --no-color --schemas \"" + schemas.string() + "\" --data \"" + data.string() + "\" --scripts \"" + scripts.string() + "\"";
        auto text = run_cmd(cmd, out);
        REQUIRE(text.find("Hephora Tree") != std::string::npos);
        REQUIRE(text.find("\x1b[") == std::string::npos);
    }

    SECTION("json format with all fields includes private keys")
    {
        std::string cmd = "\"" + viz + "\" --format json --show-all-fields --schemas \"" + schemas.string() + "\" --data \"" + data.string() + "\" --scripts \"" + scripts.string() + "\"";
        auto text = run_cmd(cmd, out);
        auto j = nlohmann::json::parse(text);

        REQUIRE(j["ok"].get<bool>());
        REQUIRE(j["format"].get<std::string>() == "json");
        REQUIRE(j["nodes"].is_array());
        REQUIRE_FALSE(j["nodes"].empty());

        const auto &root = j["nodes"][0];
        REQUIRE(root.contains("fields"));
        REQUIRE(root.contains("_profile"));
        REQUIRE(root.contains("_id"));
        REQUIRE(root.contains("_label"));
        REQUIRE(root.contains("_parent_id"));
        REQUIRE(root["_profile"].get<std::string>() == "project");
        REQUIRE(root["_id"].get<std::string>() == "P-1");
    }

    SECTION("query passthrough filters tree roots")
    {
        const std::string query = "{\"profile\":\"attachment\",\"query\":[[{\"field\":\"filename\",\"operator\":\"EQ\",\"value\":\"spec.pdf\"}]]}";
        std::string cmd = "\"" + viz + "\" --query '" + query + "' --schemas \"" + schemas.string() + "\" --data \"" + data.string() + "\" --scripts \"" + scripts.string() + "\"";
        auto text = run_cmd(cmd, out);
        REQUIRE(text.find("Hephora Tree (query)") != std::string::npos);
        REQUIRE(text.find("attachment/A-1") != std::string::npos);
        REQUIRE(text.find("project/P-1") == std::string::npos);
    }

    SECTION("query passthrough works in json mode")
    {
        const std::string query = "{\"profile\":\"attachment\",\"query\":[[{\"field\":\"filename\",\"operator\":\"EQ\",\"value\":\"spec.pdf\"}]]}";
        std::string cmd = "\"" + viz + "\" --format json --query '" + query + "' --schemas \"" + schemas.string() + "\" --data \"" + data.string() + "\" --scripts \"" + scripts.string() + "\"";
        auto text = run_cmd(cmd, out);
        auto j = nlohmann::json::parse(text);

        REQUIRE(j["ok"].get<bool>());
        REQUIRE(j["format"].get<std::string>() == "json");
        REQUIRE(j.contains("query"));
        REQUIRE(j["nodes"].is_array());
        REQUIRE(j["nodes"].size() == 1);
        REQUIRE(j["nodes"][0]["profile"].get<std::string>() == "attachment");
        REQUIRE(j["nodes"][0]["id"].get<std::string>() == "A-1");
    }

    fs::remove_all(tmp);
}
