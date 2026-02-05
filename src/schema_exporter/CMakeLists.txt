set(LIBRARY_NAME hephora-schemaexporter)
set(TESTS_NAME ${LIBRARY_NAME}-tests)

add_library(${LIBRARY_NAME} STATIC ${CMAKE_CURRENT_SOURCE_DIR}/JSONSchemaExporter.cpp)
target_include_directories(${LIBRARY_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${LIBRARY_NAME} PUBLIC hephora-schemamanager)

enable_testing()
include(Catch)

add_executable(${TESTS_NAME}
    test/test_SQLiteSchemaExporter.cpp
    test/test_JsonSchemaExporter.cpp
)

target_link_libraries(${TESTS_NAME} PRIVATE Catch2::Catch2WithMain ${LIBRARY_NAME})

catch_discover_tests(${TESTS_NAME})
