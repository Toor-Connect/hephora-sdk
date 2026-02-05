set(LIBRARY_NAME hephora-schema)
set(TESTS_NAME ${LIBRARY_NAME}-tests)

add_library(${LIBRARY_NAME} STATIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/fields/ArrayFieldSchema.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/fields/ObjectFieldSchema.cpp
)

target_include_directories(${LIBRARY_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/fields)

enable_testing()
include(Catch)

add_executable(${TESTS_NAME}
    test/test_fields.cpp
    test/test_node_schema.cpp
    test/test_schema_registry.cpp
)

target_link_libraries(${TESTS_NAME} PRIVATE Catch2::Catch2WithMain ${LIBRARY_NAME})

catch_discover_tests(${TESTS_NAME})
