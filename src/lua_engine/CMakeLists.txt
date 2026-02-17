set(LIBRARY_NAME hephora-luaengine)
# set(TESTS_NAME ${LIBRARY_NAME}-tests)

add_library(${LIBRARY_NAME} STATIC ${CMAKE_CURRENT_SOURCE_DIR}/LuaScriptsRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LuaEngine.cpp)

find_package(Lua REQUIRED)

target_include_directories(${LIBRARY_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${LUA_INCLUDE_DIR})
target_link_libraries(${LIBRARY_NAME} PUBLIC hephora-core
    PRIVATE ${LUA_LIBRARIES} nlohmann_json::nlohmann_json)

# enable_testing()
# include(Catch)

# add_executable(${TESTS_NAME}
#     test/test_JsonApiRouter.cpp
#     test/test_JsonApiRouterCommitter.cpp
# )

# target_link_libraries(${TESTS_NAME} PRIVATE Catch2::Catch2WithMain ${LIBRARY_NAME} nlohmann_json::nlohmann_json)

# catch_discover_tests(${TESTS_NAME})