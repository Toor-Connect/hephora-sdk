add_subdirectory(yaml_schema_parser)

set(LIBRARY_NAME hephora-schemamanager)
set(TESTS_NAME ${LIBRARY_NAME}-tests)

add_library(${LIBRARY_NAME} INTERFACE)
target_include_directories(${LIBRARY_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${LIBRARY_NAME} INTERFACE hephora-yamlschemaparser)

enable_testing()
include(Catch)

add_executable(${TESTS_NAME}
    test/test_schema_manager.cpp
)

target_link_libraries(${TESTS_NAME} PRIVATE Catch2::Catch2WithMain ${LIBRARY_NAME})

catch_discover_tests(${TESTS_NAME})
