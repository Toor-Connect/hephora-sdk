set(LIBRARY_NAME hephora-luaengine)

add_library(${LIBRARY_NAME} STATIC ${CMAKE_CURRENT_SOURCE_DIR}/LuaScriptsRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LuaEngine.cpp)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PKG_LUA QUIET lua5.4)
endif()

if(PKG_LUA_FOUND)
    set(HEPHORA_LUA_INCLUDE_DIRS ${PKG_LUA_INCLUDE_DIRS})
    set(HEPHORA_LUA_LIBRARIES ${PKG_LUA_LIBRARIES})
else()
    find_package(Lua 5.4 REQUIRED)
    set(HEPHORA_LUA_INCLUDE_DIRS ${LUA_INCLUDE_DIR})
    set(HEPHORA_LUA_LIBRARIES ${LUA_LIBRARIES})
endif()

target_include_directories(${LIBRARY_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PUBLIC ${HEPHORA_LUA_INCLUDE_DIRS})
target_link_libraries(${LIBRARY_NAME} PUBLIC hephora-core
    PUBLIC ${HEPHORA_LUA_LIBRARIES} nlohmann_json::nlohmann_json)
