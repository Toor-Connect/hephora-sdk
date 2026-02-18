add_executable(viz main.cpp)
set_target_properties(viz PROPERTIES OUTPUT_NAME hephora-sdk-viz)

add_dependencies(viz cli)

target_link_libraries(viz PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(viz PRIVATE CLI_EXE_PATH="$<TARGET_FILE:cli>")

install(TARGETS viz RUNTIME DESTINATION bin)

enable_testing()
include(Catch)

add_executable(hephora-viz-tests test/test_viz.cpp)
target_link_libraries(hephora-viz-tests PRIVATE Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
target_compile_definitions(hephora-viz-tests PRIVATE VIZ_EXE_PATH="$<TARGET_FILE:viz>")
add_dependencies(hephora-viz-tests viz)

catch_discover_tests(hephora-viz-tests)
