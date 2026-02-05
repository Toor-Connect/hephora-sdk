set(LIBRARY_NAME hephora-databackend)
set(TESTS_NAME ${LIBRARY_NAME}-tests)

add_library(${LIBRARY_NAME} STATIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/SQLiteDataBackend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/InMemoryDataCommit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FilesystemDataCommit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/NodeAddress.cpp
)

find_package(SQLite3 REQUIRED)

target_include_directories(${LIBRARY_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${LIBRARY_NAME} 
    PUBLIC yaml-cpp hephora-schemamanager hephora-schemaexporter hephora-yamldataparser SQLite::SQLite3
    PRIVATE nlohmann_json::nlohmann_json
)

enable_testing()
include(Catch)

add_executable(${TESTS_NAME}
    test/test_SQLiteDataBackend.cpp
    test/test_NodeAddress.cpp
)

target_link_libraries(${TESTS_NAME} PRIVATE Catch2::Catch2WithMain ${LIBRARY_NAME} nlohmann_json::nlohmann_json)

catch_discover_tests(${TESTS_NAME})
