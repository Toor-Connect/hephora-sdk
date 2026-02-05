add_subdirectory(schema)

set(LIBRARY_NAME hephora-yamlschemaparser)
set(TESTS_NAME ${LIBRARY_NAME}-tests)

add_library(${LIBRARY_NAME} STATIC YamlSchemaDecoder.cpp YamlSchemaLoader.cpp YamlSchemaEncoder.cpp)
target_include_directories(${LIBRARY_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${LIBRARY_NAME} PUBLIC yaml-cpp hephora-schema)

enable_testing()
include(Catch)

add_executable(${TESTS_NAME}
    test/test_yaml_schema_decoder.cpp
    test/test_yaml_schema_loader.cpp
    test/test_yaml_schema_encoder.cpp
)

target_link_libraries(${TESTS_NAME} PRIVATE Catch2::Catch2WithMain ${LIBRARY_NAME})

catch_discover_tests(${TESTS_NAME})
