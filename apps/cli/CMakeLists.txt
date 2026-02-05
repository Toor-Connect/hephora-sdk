add_executable(cli main.cpp)
target_link_libraries(cli PRIVATE hephora-core hephora-luaengine nlohmann_json::nlohmann_json)
set_target_properties(cli PROPERTIES OUTPUT_NAME hephora-sdk-cli)
install(TARGETS cli RUNTIME DESTINATION bin)

enable_testing()
include(Catch)

add_executable(hephora-cli-tests test/test_cli.cpp)
target_link_libraries(hephora-cli-tests PRIVATE Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
target_compile_definitions(hephora-cli-tests PRIVATE CLI_EXE_PATH="$<TARGET_FILE:cli>")
add_dependencies(hephora-cli-tests cli)

catch_discover_tests(hephora-cli-tests)