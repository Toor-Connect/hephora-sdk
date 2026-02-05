#pragma once

extern "C"
{
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <string>
#include "NodeAddress.h"
#include "CoreEngine.h"
#include "LuaScriptsRegistry.h"

class LuaEngine
{
public:
    LuaEngine(CoreEngine *engine, LuaScriptsRegistry *registry);
    ~LuaEngine();

    bool runScript(const std::string &code, const NodeKey &nodeKey);
    std::string getLastError() const;
    std::string getLastLog();
    void appendLog(const std::string &message);

    CoreEngine *getCoreEngine();
    const std::optional<NodeKey> &getCurrentNodeKey() const;
    LuaScriptsRegistry *getScriptsRegistry() { return scriptsRegistry_; }

private:
    lua_State *L;
    std::string lastError;
    std::string lastLog;
    CoreEngine *coreEngine_;
    LuaScriptsRegistry *scriptsRegistry_;
    std::optional<NodeKey> currentNodeKey_;

    void on_node_created_callback(const NodeKey &key);
    void before_node_update_callback(const NodeKey &key);
    void on_node_updated_callback(const NodeKey &key);
};
