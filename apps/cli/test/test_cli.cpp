#include <catch2/catch_all.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <chrono>
#include <sstream>
#include <sys/wait.h>
#include <nlohmann/json.hpp>

#ifndef CLI_EXE_PATH
#define CLI_EXE_PATH ""
#endif

namespace fs = std::filesystem;

static fs::path make_temp_dir()
{
    auto base = fs::temp_directory_path();
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    fs::path dir = base / ("hephora_cli_test_" + std::to_string(now));
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

static int normalized_exit_code(int status);

static std::string run_cmd_with_input(const std::string &cmd, const std::string &input, const fs::path &out_file, const fs::path &in_file)
{
    write_file(in_file, input);
    std::string full = cmd + " < \"" + in_file.string() + "\" > \"" + out_file.string() + "\"";
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

static nlohmann::json parse_json_output(const std::string &output)
{
    return nlohmann::json::parse(output);
}

static std::vector<nlohmann::json> parse_json_lines(const std::string &output)
{
    std::vector<nlohmann::json> out;
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line))
    {
        if (line.empty())
            continue;
        out.push_back(nlohmann::json::parse(line));
    }
    return out;
}

TEST_CASE("hephora-sdk-cli non-interactive workflow", "[cli]")
{
    const std::string cli = CLI_EXE_PATH;
    REQUIRE_FALSE(cli.empty());

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
                  "  tags:\n"
                  "    type: array\n"
                  "    items:\n"
                  "      type: string\n"
                  "  refs:\n"
                  "    type: array\n"
                  "    items:\n"
                  "      type: reference\n"
                  "      target: attachment\n"
                  "  objects:\n"
                  "    type: array\n"
                  "    items:\n"
                  "      type: object\n"
                  "      fields:\n"
                  "        name:\n"
                  "          type: string\n"
                  "        value:\n"
                  "          type: string\n"
                  "  specs:\n"
                  "    type: object\n"
                  "    fields:\n"
                  "      manufacturer:\n"
                  "        type: string\n"
                  "      warranty_years:\n"
                  "        type: integer\n"
                  "children:\n"
                  "  attachments:\n"
                  "    node: attachment\n"
                   "  requirements:\n"
                   "    node: requirement\n"
               "commands:\n"
               "  hello:\n"
               "    script: hello.lua\n"
               "    trigger: manual\n");

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
                   "  priority:\n"
                   "    type: enum\n"
                   "    values: [low, high]\n"
                   "  active:\n"
                   "    type: boolean\n"
                   "  refs:\n"
                   "    type: array\n"
                   "    items:\n"
                   "      type: reference\n"
                   "      target: attachment\n");

    write_file(data / "project.yaml",
               "_profile: project\n"
               "_id: P-1\n"
               "_label: Demo\n"
               "title: Demo\n"
               "tags: [beta]\n"
               "refs: [A-1]\n"
               "objects:\n"
               "  - name: Brake\n"
               "    value: ECU\n"
               "specs:\n"
               "  manufacturer: Acme\n"
               "  warranty_years: 3\n");

    write_file(data / "project-2.yaml",
               "_profile: project\n"
               "_id: P-2\n"
               "_label: Alpha\n"
               "title: Alpha\n"
               "tags: [alpha]\n");

    write_file(data / "attachment.yaml",
               "_profile: attachment\n"
               "_id: A-1\n"
               "_label: Spec\n"
               "_parent_id: P-1\n"
               "filename: spec.pdf\n");

    write_file(data / "requirement.yaml",
               "_profile: requirement\n"
               "_id: R-1\n"
               "_label: Req\n"
               "_parent_id: P-1\n"
               "title: Brake latency\n"
               "priority: high\n"
               "active: true\n"
               "refs: [A-1]\n");

    write_file(scripts / "hello.lua", "log_message(\"hello\")\n");

    fs::path out = tmp / "out.txt";

    std::string base = "\"" + cli + "\" --schemas \"" + schemas.string() + "\" --data \"" + data.string() + "\" --scripts \"" + scripts.string() + "\" ";

    SECTION("list and get")
    {
        auto list_out = run_cmd(base + "list project", out);
        auto list_json = parse_json_output(list_out);
        REQUIRE(list_json["ok"].get<bool>());
        REQUIRE(list_json["result"]["count"].get<int>() == 2);

        auto get_out = run_cmd(base + "get project P-1", out);
        auto get_json = parse_json_output(get_out);
        REQUIRE(get_json["ok"].get<bool>());
        REQUIRE(get_json["result"]["node"]["label"].get<std::string>() == "Demo");
    }

    SECTION("get-many get-by-id get-by-ids")
    {
        auto get_many_out = run_cmd(base + "get-many project P-1 P-2", out);
        auto get_many_json = parse_json_output(get_many_out);
        REQUIRE(get_many_json["ok"].get<bool>());
        REQUIRE(get_many_json["result"]["count"].get<int>() == 2);

        auto get_by_id_out = run_cmd(base + "get-by-id R-1", out);
        auto get_by_id_json = parse_json_output(get_by_id_out);
        REQUIRE(get_by_id_json["ok"].get<bool>());
        REQUIRE(get_by_id_json["result"]["count"].get<int>() == 1);
        REQUIRE(get_by_id_json["result"]["nodes"][0].contains("fields"));

        auto get_by_ids_out = run_cmd(base + "get-by-ids P-1 R-1", out);
        auto get_by_ids_json = parse_json_output(get_by_ids_out);
        REQUIRE(get_by_ids_json["ok"].get<bool>());
        REQUIRE(get_by_ids_json["result"]["count"].get<int>() == 2);
        REQUIRE(get_by_ids_json["result"]["results"].size() == 2);
    }

    SECTION("profiles and schema")
    {
        auto profiles_out = run_cmd(base + "get-profiles", out);
        auto profiles_json = parse_json_output(profiles_out);
        REQUIRE(profiles_json["ok"].get<bool>());
        REQUIRE(profiles_json["result"]["profiles"].size() > 0);

        auto schema_out = run_cmd(base + "get-schema project", out);
        auto schema_json = parse_json_output(schema_out);
        REQUIRE(schema_json["ok"].get<bool>());
        REQUIRE(schema_json["result"]["schema"].get<std::string>().find("name: project") != std::string::npos);
    }

    SECTION("query and execute-command")
    {
        auto query_out = run_cmd(base + "query '{\"profile\":\"project\",\"query\":[[{\"field\":\"title\",\"operator\":\"EQ\",\"value\":\"Demo\"}]]}'", out);
        auto query_json = parse_json_output(query_out);
        REQUIRE(query_json["ok"].get<bool>());
        REQUIRE(query_json["result"]["count"].get<int>() > 0);

        auto query_prefix = run_cmd(base + "query '{\"profile\":\"project\",\"query\":[[{\"field\":\"title\",\"operator\":\"PREFIX\",\"value\":\"De\"}]]}'", out);
        auto prefix_json = parse_json_output(query_prefix);
        REQUIRE(prefix_json["ok"].get<bool>());

        auto query_contains = run_cmd(base + "query '{\"profile\":\"project\",\"query\":[[{\"field\":\"tags\",\"operator\":\"CONTAINS\",\"value\":\"beta\"}]]}'", out);
        auto contains_json = parse_json_output(query_contains);
        REQUIRE(contains_json["ok"].get<bool>());

        auto query_in = run_cmd(base + "query '{\"profile\":\"project\",\"query\":[[{\"field\":\"title\",\"operator\":\"IN\",\"value\":[\"Demo\",\"Alpha\"]}]]}'", out);
        auto in_json = parse_json_output(query_in);
        REQUIRE(in_json["ok"].get<bool>());

        auto exec_out = run_cmd(base + "execute-command project P-1 hello", out);
        auto exec_json = parse_json_output(exec_out);
        REQUIRE(exec_json["ok"].get<bool>());
        REQUIRE(exec_json["result"]["status"].get<std::string>() == "OK");
    }

        SECTION("create update arrays children and flush")
        {
            run_cmd(base + "update project P-1 title=Demo specs.manufacturer=Acme specs.warranty_years=3", out);
            run_cmd(base + "update attachment A-1 parent=P-1 filename=spec.pdf", out);

            run_cmd(base + "arr-add project P-1 tags beta", out);
            run_cmd(base + "arr-add project P-1 objects name=Brake value=ECU", out);
            run_cmd(base + "arr-add project P-1 refs A-1", out);
            run_cmd(base + "arr-set project P-1 tags[0] gamma", out);
            run_cmd(base + "arr-set project P-1 objects[0].name BrakeECU2", out);

            auto sel_out = run_cmd(base + "get-select project P-1 objects[0].name", out);
            auto sel_json = parse_json_output(sel_out);
            REQUIRE(sel_json["ok"].get<bool>());
            REQUIRE(sel_json["result"]["value"].get<std::string>().find("Brake") != std::string::npos);

            auto children_out = run_cmd(base + "get-children project P-1", out);
            auto children_json = parse_json_output(children_out);
            REQUIRE(children_json["ok"].get<bool>());
            auto rows = children_json["result"]["rows"];
            auto rows_dump = rows.dump();
            REQUIRE(rows_dump.find("attachment") != std::string::npos);
            REQUIRE(rows_dump.find("requirement") != std::string::npos);

            auto children_by_id_out = run_cmd(base + "get-children-by-id P-1", out);
            auto children_by_id_json = parse_json_output(children_by_id_out);
            REQUIRE(children_by_id_json["ok"].get<bool>());
            auto children_by_id_rows = children_by_id_json["result"]["rows"];
            REQUIRE(children_by_id_rows.size() > 0);
            REQUIRE_FALSE(children_by_id_rows[0].contains("fields"));

            auto refs_out = run_cmd(base + "get-refs-to-by-id A-1", out);
            auto refs_json = parse_json_output(refs_out);
            REQUIRE(refs_json["ok"].get<bool>());
            auto refs_rows = refs_json["result"]["rows"];
            REQUIRE(refs_rows.size() >= 2);
            REQUIRE_FALSE(refs_rows[0].contains("fields"));

            run_cmd(base + "arr-del project P-1 tags value=gamma", out);
            auto tags_out = run_cmd(base + "get-field project P-1 tags", out);
            auto tags_json = parse_json_output(tags_out);
            REQUIRE(tags_json["ok"].get<bool>());
            REQUIRE(tags_json["result"]["value"].dump().find("beta") != std::string::npos);

        }

        SECTION("create without id returns generated id")
        {
            auto create_out = run_cmd(base + "create requirement parent=P-1 title=AutoId priority=low active=true", out);
            auto create_json = parse_json_output(create_out);
            REQUIRE(create_json["ok"].get<bool>());
            REQUIRE(create_json["result"]["node"].contains("id"));

            auto list_out = run_cmd(base + "list requirement", out);
            auto list_json = parse_json_output(list_out);
            REQUIRE(list_json["ok"].get<bool>());
            REQUIRE(list_json["result"].contains("rows"));
        }

        SECTION("interactive batch workflow")
        {
            fs::path in = tmp / "in.txt";
            std::string input =
                "load-workspace " + schemas.string() + " " + data.string() + " " + scripts.string() + "\n"
                "list project\n"
                "delete project P-2\n"
                "list project\n"
                "get-field project P-1 specs\n"
                "get-select project P-1 refs[0]\n"
                "quit\n";

            auto out_text = run_cmd_with_input("\"" + cli + "\"", input, out, in);
            auto lines = parse_json_lines(out_text);
            REQUIRE(lines.size() >= 5);
            REQUIRE(lines[0]["ok"].get<bool>());
            REQUIRE(lines[1]["ok"].get<bool>());
            REQUIRE(lines.back()["ok"].get<bool>());
        }

    fs::remove_all(tmp);
}
