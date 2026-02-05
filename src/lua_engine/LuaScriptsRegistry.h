#pragma once
#include <string>
#include <unordered_map>

class LuaScriptsRegistry
{
public:
    LuaScriptsRegistry() = default;
    ~LuaScriptsRegistry() = default;
    LuaScriptsRegistry(const LuaScriptsRegistry &) = delete;
    LuaScriptsRegistry &operator=(const LuaScriptsRegistry &) = delete;
    LuaScriptsRegistry(LuaScriptsRegistry &&) = delete;
    LuaScriptsRegistry &operator=(LuaScriptsRegistry &&) = delete;
    void addScript(const std::string &name, const std::string &content);
    const std::string *getScript(const std::string &name) const;
    void clear();
    void loadScripts(std::unordered_map<std::string, std::string> sources);

private:
    std::unordered_map<std::string, std::string> scripts_; // key: script name, value: script content
};