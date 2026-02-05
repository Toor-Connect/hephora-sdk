#include "LuaEngine.h"
#include "LuaScriptsRegistry.h"
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>
#include "FieldValue.h"

// Forward declarations for Lua C functions
int lua_get_current_node(lua_State *L);
int lua_update_current_node_label(lua_State *L);
int lua_get_node(lua_State *L);
int lua_update_node(lua_State *L);
int lua_delete_node(lua_State *L);
int lua_create_node(lua_State *L);
int lua_custom_require(lua_State *L);
int lua_log_message(lua_State *L);

LuaEngine::LuaEngine(CoreEngine *engine, LuaScriptsRegistry *registry)
    : coreEngine_(engine), scriptsRegistry_(registry)
{
    L = luaL_newstate();
    luaL_openlibs(L);
    // Register get_current_node
    lua_pushlightuserdata(L, this); // upvalue: LuaEngine*
    lua_pushcclosure(L, lua_get_current_node, 1);
    lua_setglobal(L, "get_current_node");
    // Register update_current_node_label
    lua_pushlightuserdata(L, this); // upvalue: LuaEngine*
    lua_pushcclosure(L, lua_update_current_node_label, 1);
    lua_setglobal(L, "update_current_node_label");
    // Register get_node
    lua_pushlightuserdata(L, this); // upvalue: LuaEngine*
    lua_pushcclosure(L, lua_get_node, 1);
    lua_setglobal(L, "get_node");
    // Register update_node
    lua_pushlightuserdata(L, this); // upvalue: LuaEngine*
    lua_pushcclosure(L, lua_update_node, 1);
    lua_setglobal(L, "update_node");
    // Register custom require
    lua_pushlightuserdata(L, this); // upvalue: LuaEngine*
    lua_pushcclosure(L, lua_custom_require, 1);
    lua_setglobal(L, "require");
    // Register create_node
    lua_pushlightuserdata(L, this); // upvalue: LuaEngine*
    lua_pushcclosure(L, lua_create_node, 1);
    lua_setglobal(L, "create_node");
    // Register delete_node
    lua_pushlightuserdata(L, this); // upvalue: LuaEngine*
    lua_pushcclosure(L, lua_delete_node, 1);
    lua_setglobal(L, "delete_node");
    // Register log_message
    lua_pushlightuserdata(L, this); // upvalue: LuaEngine*
    lua_pushcclosure(L, lua_log_message, 1);
    lua_setglobal(L, "log_message");

    // Set CoreEngine callbacks
    if (coreEngine_)
    {
        coreEngine_->setNodeCreatedCallback([this](const NodeKey &key)
                                            { this->on_node_created_callback(key); });
        coreEngine_->setBeforeNodeUpdateCallback([this](const NodeKey &key)
                                                 { this->before_node_update_callback(key); });
        coreEngine_->setNodeUpdatedCallback([this](const NodeKey &key)
                                            { this->on_node_updated_callback(key); });
    }
}

LuaEngine::~LuaEngine()
{
    if (L)
        lua_close(L);
}

bool LuaEngine::runScript(const std::string &code, const NodeKey &nodeKey)
{
    currentNodeKey_ = nodeKey;
    if (luaL_loadstring(L, code.c_str()) || lua_pcall(L, 0, LUA_MULTRET, 0))
    {
        lastError = lua_tostring(L, -1);
        lua_pop(L, 1);
        currentNodeKey_.reset();
        return false;
    }
    // Check if the script returned nil (error propagation from Lua)
    int nresults = lua_gettop(L);
    if (nresults >= 1 && lua_isnil(L, -nresults))
    {
        // If there's a second return value, use it as error message
        if (nresults >= 2 && lua_isstring(L, -nresults + 1))
        {
            lastError = lua_tostring(L, -nresults + 1);
        }
        else
        {
            lastError = "Lua script returned nil";
        }
        lua_pop(L, nresults);
        currentNodeKey_.reset();
        return false;
    }
    lua_pop(L, nresults);
    currentNodeKey_.reset();
    return true;
}

std::string LuaEngine::getLastError() const
{
    return lastError;
}

std::string LuaEngine::getLastLog()
{
    std::string log = lastLog;
    lastLog.clear();
    return log;
}

void LuaEngine::appendLog(const std::string &message)
{
    lastLog += message + "\n";
}

CoreEngine *LuaEngine::getCoreEngine()
{
    return coreEngine_;
}
const std::optional<NodeKey> &LuaEngine::getCurrentNodeKey() const
{
    return currentNodeKey_;
}

// Lua: get_current_node() -> json string or nil, err
int lua_get_current_node(lua_State *L)
{
    auto *luaEngine = static_cast<LuaEngine *>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!luaEngine)
    {
        lua_pushnil(L);
        lua_pushstring(L, "LuaEngine not available");
        return 2;
    }
    CoreEngine *engine = luaEngine->getCoreEngine();
    if (!engine)
    {
        lua_pushnil(L);
        lua_pushstring(L, "CoreEngine not available");
        return 2;
    }
    if (!luaEngine->getCurrentNodeKey().has_value())
    {
        lua_pushnil(L);
        lua_pushstring(L, "No node context set");
        return 2;
    }
    const NodeKey &key = luaEngine->getCurrentNodeKey().value();
    auto fetch = engine->fetch(key);
    if (!fetch.has_value())
    {
        lua_pushnil(L);
        lua_pushstring(L, "Node not found");
        return 2;
    }

    nlohmann::json result = fetch.value().toJson();
    std::string json_str = result.get<std::string>();
    lua_pushlstring(L, json_str.c_str(), json_str.size());
    return 1;
}

// Lua: update_current_node_label(new_label) -> true or nil, err
int lua_update_current_node_label(lua_State *L)
{
    auto *luaEngine = static_cast<LuaEngine *>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!luaEngine)
    {
        lua_pushnil(L);
        lua_pushstring(L, "LuaEngine not available");
        return 2;
    }
    CoreEngine *engine = luaEngine->getCoreEngine();
    if (!engine)
    {
        lua_pushnil(L);
        lua_pushstring(L, "CoreEngine not available");
        return 2;
    }
    if (!luaEngine->getCurrentNodeKey().has_value())
    {
        lua_pushnil(L);
        lua_pushstring(L, "No node context set");
        return 2;
    }
    const NodeKey &key = luaEngine->getCurrentNodeKey().value();
    auto fetch = engine->fetch(key);
    if (!fetch.has_value())
    {
        lua_pushnil(L);
        lua_pushstring(L, "Node not found");
        return 2;
    }
    NodeSnapshot snap = fetch.value();
    if (!lua_isstring(L, 1))
    {
        lua_pushnil(L);
        lua_pushstring(L, "update_current_node_label expects a string argument");
        return 2;
    }
    snap.label = std::string(lua_tostring(L, 1));

    // Unregister before update callback and node updated callback to avoid recursion
    auto original_before_update_callback = engine->getBeforeNodeUpdateCallback();
    engine->setBeforeNodeUpdateCallback(nullptr);

    auto original_node_updated_callback = engine->getNodeUpdatedCallback();
    engine->setNodeUpdatedCallback(nullptr);

    engine->upsert(snap);

    // Restore callbacks
    engine->setBeforeNodeUpdateCallback(original_before_update_callback);
    engine->setNodeUpdatedCallback(original_node_updated_callback);
    lua_pushboolean(L, 1);
    return 1;
}

// Lua: require(script_name) -> module or nil, err
int lua_custom_require(lua_State *L)
{
    auto *luaEngine = static_cast<LuaEngine *>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!luaEngine)
    {
        lua_pushnil(L);
        lua_pushstring(L, "LuaEngine not available");
        return 2;
    }
    LuaScriptsRegistry *registry = luaEngine->getScriptsRegistry();
    if (!registry)
    {
        lua_pushnil(L);
        lua_pushstring(L, "LuaScriptsRegistry not available");
        return 2;
    }
    if (!lua_isstring(L, 1))
    {
        lua_pushnil(L);
        lua_pushstring(L, "require expects a script name string");
        return 2;
    }
    std::string script_name = lua_tostring(L, 1);
    if (script_name.size() < 4 || script_name.substr(script_name.size() - 4) != ".lua")
    {
        script_name += ".lua";
    }
    // Module cache in Lua registry
    lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
    lua_getfield(L, -1, script_name.c_str());
    if (!lua_isnil(L, -1))
    {
        // Already loaded, return cached module
        return 1;
    }
    lua_pop(L, 1); // pop nil
    // Load and run the script
    const std::string *script = registry->getScript(script_name);
    if (!script)
    {
        lua_pushnil(L);
        lua_pushstring(L, "Script not found");
        return 2;
    }
    if (luaL_loadstring(L, script->c_str()) || lua_pcall(L, 0, 1, 0))
    {
        lua_pushnil(L);
        lua_pushstring(L, lua_tostring(L, -1));
        return 2;
    }
    // Store result in _LOADED
    lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
    lua_pushvalue(L, -2); // module result
    lua_setfield(L, -2, script_name.c_str());
    lua_pop(L, 1); // pop _LOADED
    // Return the module
    return 1;
}

// Lua: get_node(profile, id) -> json string or nil, err
int lua_get_node(lua_State *L)
{
    auto *luaEngine = static_cast<LuaEngine *>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!luaEngine)
    {
        lua_pushnil(L);
        lua_pushstring(L, "LuaEngine not available");
        return 2;
    }
    CoreEngine *engine = luaEngine->getCoreEngine();
    if (!engine)
    {
        lua_pushnil(L);
        lua_pushstring(L, "CoreEngine not available");
        return 2;
    }
    if (!lua_isstring(L, 1) || !lua_isstring(L, 2))
    {
        lua_pushnil(L);
        lua_pushstring(L, "get_node expects (profile, id) as string arguments");
        return 2;
    }
    std::string profile = lua_tostring(L, 1);
    std::string id = lua_tostring(L, 2);
    auto fetch = engine->fetch(NodeKey{profile, id});
    if (!fetch.has_value())
    {
        lua_pushnil(L);
        lua_pushstring(L, "Node not found");
        return 2;
    }

    nlohmann::json result = fetch.value().toJson();
    std::string json_str = result.get<std::string>();
    lua_pushlstring(L, json_str.c_str(), json_str.size());
    return 1;
}

// Lua: update_node(profile, id, "key1=value1", "key2=value2", ...) -> true or nil, err
int lua_update_node(lua_State *L)
{
    auto *luaEngine = static_cast<LuaEngine *>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!luaEngine)
    {
        lua_pushnil(L);
        lua_pushstring(L, "LuaEngine not available");
        return 2;
    }
    CoreEngine *engine = luaEngine->getCoreEngine();
    if (!engine)
    {
        lua_pushnil(L);
        lua_pushstring(L, "CoreEngine not available");
        return 2;
    }
    if (!lua_isstring(L, 1) || !lua_isstring(L, 2))
    {
        lua_pushnil(L);
        lua_pushstring(L, "update_node expects (profile, id, ...) with profile and id as string arguments");
        return 2;
    }
    std::string profile = lua_tostring(L, 1);
    std::string id = lua_tostring(L, 2);
    auto fetch = engine->fetch(NodeKey{profile, id});
    if (!fetch.has_value())
    {
        lua_pushnil(L);
        lua_pushstring(L, "Node not found");
        return 2;
    }
    NodeSnapshot snap = fetch.value();
    int n = lua_gettop(L);
    for (int i = 3; i <= n; ++i)
    {
        if (!lua_isstring(L, i))
        {
            lua_pushnil(L);
            lua_pushstring(L, "update_node expects key=value strings as additional arguments");
            return 2;
        }
        std::string kv = lua_tostring(L, i);
        size_t eq_pos = kv.find('=');
        if (eq_pos == std::string::npos)
        {
            lua_pushnil(L);
            lua_pushstring(L, "Invalid key=value pair: missing '='");
            return 2;
        }
        std::string key = kv.substr(0, eq_pos);
        std::string value = kv.substr(eq_pos + 1);
        if (key == "label")
        {
            snap.label = value;
        }
        else if (key == "parent")
        {
            snap.parent_id = value;
        }
        else if (snap.fields[key].isString())
        {
            snap.fields[key] = value;
        }
        else if (snap.fields[key].isInteger())
        {
            try
            {
                snap.fields[key] = std::stoi(value);
            }
            catch (const std::exception &)
            {
                lua_pushnil(L);
                lua_pushstring(L, ("Invalid integer value for field " + key).c_str());
                return 2;
            }
        }
        else if (snap.fields[key].isBoolean())
        {
            if (value == "true" || value == "1")
                snap.fields[key] = true;
            else if (value == "false" || value == "0")
                snap.fields[key] = false;
            else
            {
                lua_pushnil(L);
                lua_pushstring(L, ("Invalid boolean value for field " + key).c_str());
                return 2;
            }
        }
        else if (snap.fields[key].isArray())
        {
            // Expecting [item1,item2,...] for array
            ArrayData arr;
            if (value.size() >= 2 && value.front() == '[' && value.back() == ']')
            {
                std::string inner = value.substr(1, value.size() - 2);
                std::istringstream ss(inner);
                std::string item;
                while (std::getline(ss, item, ','))
                {
                    arr.push_back(item);
                }
            }
            else
            {
                lua_pushnil(L);
                lua_pushstring(L, ("Array value for field " + key + " must be delimited with [ ]").c_str());
                return 2;
            }
            snap.fields[key] = arr;
        }
        else if (snap.fields[key].isObject())
        {
            // Expecting {key1=val1,key2=val2} for object
            ObjectData obj;
            if (value.size() >= 2 && value.front() == '{' && value.back() == '}')
            {
                std::string inner = value.substr(1, value.size() - 2);
                std::istringstream ss(inner);
                std::string item;
                while (std::getline(ss, item, ','))
                {
                    size_t eq_pos = item.find('=');
                    if (eq_pos == std::string::npos)
                    {
                        lua_pushnil(L);
                        lua_pushstring(L, ("Invalid key=value pair in object for field " + key).c_str());
                        return 2;
                    }
                    std::string obj_key = item.substr(0, eq_pos);
                    std::string obj_value = item.substr(eq_pos + 1);
                    obj[obj_key] = obj_value;
                }
            }
            else
            {
                lua_pushnil(L);
                lua_pushstring(L, ("Object value for field " + key + " must be delimited with { }").c_str());
                return 2;
            }
            snap.fields[key] = obj;
        }
        else
        {
            // Field not set yet, need to infer type from schema; or unknown type
            CoreEngine *engine = luaEngine->getCoreEngine();
            auto schema = engine->getRegistry()->getSchema(profile);
            if (schema)
            {
                auto fieldType = schema->getField(key)->type();
                switch (fieldType)
                {
                case FieldType::String:
                    snap.fields[key] = value;
                    break;
                case FieldType::Integer:
                    try
                    {
                        snap.fields[key] = std::stoi(value);
                    }
                    catch (const std::exception &)
                    {
                        lua_pushnil(L);
                        lua_pushstring(L, ("Invalid integer value for field " + key).c_str());
                        return 2;
                    }
                    break;
                case FieldType::Boolean:
                    if (value == "true" || value == "1")
                        snap.fields[key] = true;
                    else if (value == "false" || value == "0")
                        snap.fields[key] = false;
                    else
                    {
                        lua_pushnil(L);
                        lua_pushstring(L, ("Invalid boolean value for field " + key).c_str());
                        return 2;
                    }
                    break;
                case FieldType::Array:
                {
                    // Expecting [item1,item2,...] for array
                    ArrayData arr;
                    if (value.size() >= 2 && value.front() == '[' && value.back() == ']')
                    {
                        std::string inner = value.substr(1, value.size() - 2);
                        std::istringstream ss(inner);
                        std::string item;
                        while (std::getline(ss, item, ','))
                        {
                            arr.push_back(item);
                        }
                    }
                    else
                    {
                        lua_pushnil(L);
                        lua_pushstring(L, ("Array value for field " + key + " must be delimited with [ ]").c_str());
                        return 2;
                    }
                    snap.fields[key] = arr;
                }
                break;
                case FieldType::Object:
                {
                    // Expecting {key1=val1,key2=val2} for object
                    ObjectData obj;
                    if (value.size() >= 2 && value.front() == '{' && value.back() == '}')
                    {
                        std::string inner = value.substr(1, value.size() - 2);
                        std::istringstream ss(inner);
                        std::string item;
                        while (std::getline(ss, item, ','))
                        {
                            size_t eq_pos = item.find('=');
                            if (eq_pos == std::string::npos)
                            {
                                lua_pushnil(L);
                                lua_pushstring(L, ("Invalid key=value pair in object for field " + key).c_str());
                                return 2;
                            }
                            std::string obj_key = item.substr(0, eq_pos);
                            std::string obj_value = item.substr(eq_pos + 1);
                            obj[obj_key] = obj_value;
                        }
                    }
                    else
                    {
                        lua_pushnil(L);
                        lua_pushstring(L, ("Object value for field " + key + " must be delimited with { }").c_str());
                        return 2;
                    }
                    snap.fields[key] = obj;
                }
                break;
                case FieldType::Reference:
                    snap.fields[key] = value;
                    break;
                default:
                    lua_pushnil(L);
                    lua_pushstring(L, ("Unknown field type for field " + key).c_str());
                    return 2;
                }
            }
            else
            {
                lua_pushnil(L);
                lua_pushstring(L, ("Schema not found for profile " + profile).c_str());
                return 2;
            }
        }
    }

    // Unregister before update callback and node updated callback to avoid recursion
    auto original_before_update_callback = engine->getBeforeNodeUpdateCallback();
    engine->setBeforeNodeUpdateCallback(nullptr);
    auto original_node_updated_callback = engine->getNodeUpdatedCallback();
    engine->setNodeUpdatedCallback(nullptr);

    engine->upsert(snap);

    // Restore callbacks
    engine->setBeforeNodeUpdateCallback(original_before_update_callback);
    engine->setNodeUpdatedCallback(original_node_updated_callback);

    lua_pushboolean(L, 1);
    return 1;
}

// Lua: delete_node(profile, id) -> true or nil, err
int lua_delete_node(lua_State *L)
{
    auto *luaEngine = static_cast<LuaEngine *>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!luaEngine)
    {
        lua_pushnil(L);
        lua_pushstring(L, "LuaEngine not available");
        return 2;
    }
    CoreEngine *engine = luaEngine->getCoreEngine();
    if (!engine)
    {
        lua_pushnil(L);
        lua_pushstring(L, "CoreEngine not available");
        return 2;
    }
    if (!lua_isstring(L, 1) || !lua_isstring(L, 2))
    {
        lua_pushnil(L);
        lua_pushstring(L, "delete_node expects (profile, id) as string arguments");
        return 2;
    }
    std::string profile = lua_tostring(L, 1);
    std::string id = lua_tostring(L, 2);

    engine->remove(NodeKey{profile, id});

    lua_pushboolean(L, 1);
    return 1;
}

// Lua: create_node(profile, "key1=value1", "key2=value2", ...) -> json string or nil, err
int lua_create_node(lua_State *L)
{
    auto *luaEngine = static_cast<LuaEngine *>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!luaEngine)
    {
        lua_pushnil(L);
        lua_pushstring(L, "LuaEngine not available");
        return 2;
    }
    CoreEngine *engine = luaEngine->getCoreEngine();
    if (!engine)
    {
        lua_pushnil(L);
        lua_pushstring(L, "CoreEngine not available");
        return 2;
    }
    if (!lua_isstring(L, 1))
    {
        lua_pushnil(L);
        lua_pushstring(L, "create_node expects (profile, ...) with profile as string argument");
        return 2;
    }
    std::string profile = lua_tostring(L, 1);
    NodeSnapshot snap{
        .key = NodeKey{
            .profile = profile,
            .id = std::nullopt,
        },
    };
    int n = lua_gettop(L);
    for (int i = 2; i <= n; ++i)
    {
        if (!lua_isstring(L, i))
        {
            lua_pushnil(L);
            lua_pushstring(L, "create_node expects key=value strings as additional arguments");
            return 2;
        }
        std::string kv = lua_tostring(L, i);
        size_t eq_pos = kv.find('=');
        if (eq_pos == std::string::npos)
        {
            lua_pushnil(L);
            lua_pushstring(L, "Invalid key=value pair: missing '='");
            return 2;
        }
        std::string key = kv.substr(0, eq_pos);
        std::string value = kv.substr(eq_pos + 1);
        if (key == "label")
        {
            snap.label = value;
        }
        else if (key == "parent")
        {
            snap.parent_id = value;
        }
        else if (snap.fields[key].isString())
        {
            snap.fields[key] = value;
        }
        else if (snap.fields[key].isInteger())
        {
            try
            {
                snap.fields[key] = std::stoi(value);
            }
            catch (const std::exception &)
            {
                lua_pushnil(L);
                lua_pushstring(L, ("Invalid integer value for field " + key).c_str());
                return 2;
            }
        }
        else if (snap.fields[key].isBoolean())
        {
            if (value == "true" || value == "1")
                snap.fields[key] = true;
            else if (value == "false" || value == "0")
                snap.fields[key] = false;
            else
            {
                lua_pushnil(L);
                lua_pushstring(L, ("Invalid boolean value for field " + key).c_str());
                return 2;
            }
        }
        else if (snap.fields[key].isArray())
        {
            // Expecting [item1,item2,...] for array
            ArrayData arr;
            if (value.size() >= 2 && value.front() == '[' && value.back() == ']')
            {
                std::string inner = value.substr(1, value.size() - 2);
                std::istringstream ss(inner);
                std::string item;
                while (std::getline(ss, item, ','))
                {
                    arr.push_back(item);
                }
            }
            else
            {
                lua_pushnil(L);
                lua_pushstring(L, ("Array value for field " + key + " must be delimited with [ ]").c_str());
                return 2;
            }
            snap.fields[key] = arr;
        }
        else if (snap.fields[key].isObject())
        {
            // Expecting {key1=val1,key2=val2} for object
            ObjectData obj;
            if (value.size() >= 2 && value.front() == '{' && value.back() == '}')
            {
                std::string inner = value.substr(1, value.size() - 2);
                std::istringstream ss(inner);
                std::string item;
                while (std::getline(ss, item, ','))
                {
                    size_t eq_pos = item.find('=');
                    if (eq_pos == std::string::npos)
                    {
                        lua_pushnil(L);
                        lua_pushstring(L, ("Invalid key=value pair in object for field " + key).c_str());
                        return 2;
                    }
                    std::string obj_key = item.substr(0, eq_pos);
                    std::string obj_value = item.substr(eq_pos + 1);
                    obj[obj_key] = obj_value;
                }
            }
            else
            {
                lua_pushnil(L);
                lua_pushstring(L, ("Object value for field " + key + " must be delimited with { }").c_str());
                return 2;
            }
            snap.fields[key] = obj;
        }
        else
        {
            // Field not set yet, need to infer type from schema; or unknown type
            CoreEngine *engine = luaEngine->getCoreEngine();
            auto schema = engine->getRegistry()->getSchema(profile);
            if (schema)
            {
                auto fieldType = schema->getField(key)->type();
                switch (fieldType)
                {
                case FieldType::String:
                    snap.fields[key] = value;
                    break;
                case FieldType::Integer:
                    try
                    {
                        snap.fields[key] = std::stoi(value);
                    }
                    catch (const std::exception &)
                    {
                        lua_pushnil(L);
                        lua_pushstring(L, ("Invalid integer value for field " + key).c_str());
                        return 2;
                    }
                    break;
                case FieldType::Boolean:
                    if (value == "true" || value == "1")
                        snap.fields[key] = true;
                    else if (value == "false" || value == "0")
                        snap.fields[key] = false;
                    else
                    {
                        lua_pushnil(L);
                        lua_pushstring(L, ("Invalid boolean value for field " + key).c_str());
                        return 2;
                    }
                    break;
                case FieldType::Array:
                {
                    // Expecting [item1,item2,...] for array
                    ArrayData arr;
                    if (value.size() >= 2 && value.front() == '[' && value.back() == ']')
                    {
                        std::string inner = value.substr(1, value.size() - 2);
                        std::istringstream ss(inner);
                        std::string item;
                        while (std::getline(ss, item, ','))
                        {
                            arr.push_back(item);
                        }
                    }
                    else
                    {
                        lua_pushnil(L);
                        lua_pushstring(L, ("Array value for field " + key + " must be delimited with [ ]").c_str());
                        return 2;
                    }
                    snap.fields[key] = arr;
                }
                break;
                case FieldType::Object:
                {
                    // Expecting {key1=val1,key2=val2} for object
                    ObjectData obj;
                    if (value.size() >= 2 && value.front() == '{' && value.back() == '}')
                    {
                        std::string inner = value.substr(1, value.size() - 2);
                        std::istringstream ss(inner);
                        std::string item;
                        while (std::getline(ss, item, ','))
                        {
                            size_t eq_pos = item.find('=');
                            if (eq_pos == std::string::npos)
                            {
                                lua_pushnil(L);
                                lua_pushstring(L, ("Invalid key=value pair in object for field " + key).c_str());
                                return 2;
                            }
                            std::string obj_key = item.substr(0, eq_pos);
                            std::string obj_value = item.substr(eq_pos + 1);
                            obj[obj_key] = obj_value;
                        }
                    }
                    else
                    {
                        lua_pushnil(L);
                        lua_pushstring(L, ("Object value for field " + key + " must be delimited with { }").c_str());
                        return 2;
                    }
                    snap.fields[key] = obj;
                }
                break;
                case FieldType::Reference:
                    snap.fields[key] = value;
                    break;
                default:
                    lua_pushnil(L);
                    lua_pushstring(L, ("Unknown field type for field " + key).c_str());
                    return 2;
                }
            }
            else
            {
                lua_pushnil(L);
                lua_pushstring(L, ("Schema not found for profile " + profile).c_str());
                return 2;
            }
        }
    }

    engine->upsert(snap);
    nlohmann::json result = snap.toJson();
    std::string json_str = result.dump();
    lua_pushlstring(L, json_str.c_str(), json_str.size());
    return 1;
}

// Lua: log_message(message) -> nil
int lua_log_message(lua_State *L)
{
    auto *luaEngine = static_cast<LuaEngine *>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!luaEngine)
    {
        return 0;
    }
    if (!lua_isstring(L, 1))
    {
        return 0;
    }
    std::string message = lua_tostring(L, 1);
    luaEngine->appendLog(message);
    return 0;
}

void LuaEngine::on_node_created_callback(const NodeKey &key)
{
    currentNodeKey_ = key;
    auto schema = coreEngine_->getRegistry()->getSchema(key.profile);
    if (!schema)
        throw std::runtime_error("Schema not found in created callback");
    auto node_opt = coreEngine_->fetch(key);
    if (!node_opt.has_value())
        throw std::runtime_error("Node not found in created callback");
    auto onCreateCmds = schema->onCreateCustomCommands();
    for (const auto &[cmdName, cmd] : onCreateCmds)
    {
        const std::string *script = scriptsRegistry_->getScript(cmd.script_name);
        if (!script)
            throw std::runtime_error("Lua script not found: " + cmd.script_name);
        if (!this->runScript(*script, key))
        {
            throw std::runtime_error("Error running onCreate script '" + cmd.script_name + "' Error: " + this->getLastError());
        }
    }
}
void LuaEngine::before_node_update_callback(const NodeKey &key)
{
    currentNodeKey_ = key;
    auto schema = coreEngine_->getRegistry()->getSchema(key.profile);
    if (!schema)
        throw std::runtime_error("Schema not found in before update callback");
    auto node_opt = coreEngine_->fetch(key);
    if (!node_opt.has_value())
        throw std::runtime_error("Node not found in before update callback");
    auto beforeUpdateCmds = schema->beforeUpdateCustomCommands();
    for (const auto &[cmdName, cmd] : beforeUpdateCmds)
    {
        const std::string *script = scriptsRegistry_->getScript(cmd.script_name);
        if (!script)
            throw std::runtime_error("Lua script not found: " + cmd.script_name);
        if (!this->runScript(*script, key))
        {
            throw std::runtime_error("Error running beforeUpdate script '" + cmd.script_name + "' Error: " + this->getLastError());
        }
    }
}
void LuaEngine::on_node_updated_callback(const NodeKey &key)
{
    currentNodeKey_ = key;
    auto schema = coreEngine_->getRegistry()->getSchema(key.profile);
    if (!schema)
        throw std::runtime_error("Schema not found in updated callback");
    auto node_opt = coreEngine_->fetch(key);
    if (!node_opt.has_value())
        throw std::runtime_error("Node not found in updated callback");
    auto onUpdateCmds = schema->onUpdateCustomCommands();
    for (const auto &[cmdName, cmd] : onUpdateCmds)
    {
        const std::string *script = scriptsRegistry_->getScript(cmd.script_name);
        if (!script)
            throw std::runtime_error("Lua script not found: " + cmd.script_name);
        if (!this->runScript(*script, key))
        {
            throw std::runtime_error("Error running onUpdate script '" + cmd.script_name + "' Error: " + this->getLastError());
        }
    }
}