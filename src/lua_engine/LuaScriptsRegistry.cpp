#include "LuaScriptsRegistry.h"

void LuaScriptsRegistry::addScript(const std::string &name, const std::string &content)
{
    scripts_[name] = content;
}

const std::string *LuaScriptsRegistry::getScript(const std::string &name) const
{
    auto it = scripts_.find(name);
    return (it != scripts_.end()) ? &it->second : nullptr;
}

void LuaScriptsRegistry::clear()
{
    scripts_.clear();
}

void LuaScriptsRegistry::loadScripts(std::unordered_map<std::string, std::string> sources)
{
    for (const auto &[name, content] : sources)
    {
        addScript(name, content);
    }
}