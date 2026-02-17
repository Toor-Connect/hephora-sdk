set(LIBRARY_NAME hephora-luaengine)
# set(TESTS_NAME ${LIBRARY_NAME}-tests)

add_library(${LIBRARY_NAME} STATIC ${CMAKE_CURRENT_SOURCE_DIR}/LuaScriptsRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LuaEngine.cpp)

find_package(Lua 5.4 QUIET)
if(Lua_FOUND)
    set(HEPHORA_LUA_INCLUDE_DIRS ${LUA_INCLUDE_DIR})
    set(HEPHORA_LUA_LIBRARIES ${LUA_LIBRARIES})
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PKG_LUA REQUIRED lua5.4)
    set(HEPHORA_LUA_INCLUDE_DIRS ${PKG_LUA_INCLUDE_DIRS})
    set(HEPHORA_LUA_LIBRARIES ${PKG_LUA_LIBRARIES})
endif()

target_include_directories(${LIBRARY_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PUBLIC ${HEPHORA_LUA_INCLUDE_DIRS})
target_link_libraries(${LIBRARY_NAME} PUBLIC hephora-core
    PUBLIC ${HEPHORA_LUA_LIBRARIES} nlohmann_json::nlohmann_json)

# enable_testing()
# include(Catch)

# add_executable(${TESTS_NAME}
#     test/test_JsonApiRouter.cpp
#     test/test_JsonApiRouterCommitter.cpp
# )

# target_link_libraries(${TESTS_NAME} PRIVATE Catch2::Catch2WithMain ${LIBRARY_NAME} nlohmann_json::nlohmann_json)

# catch_discover_tests(${TESTS_NAME})