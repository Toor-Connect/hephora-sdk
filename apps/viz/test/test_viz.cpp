#include <catch2/catch_all.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
               "    node: attachment\n");

    write_file(schemas / "attachment.yaml",
               "name: attachment\n"
               "kind: node\n"
               "description: Attachment\n"
               "fields:\n"
               "  filename:\n"
               "    type: string\n");

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
    }

    fs::remove_all(tmp);
}
