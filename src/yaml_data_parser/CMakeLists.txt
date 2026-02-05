set(LIBRARY_NAME hephora-yamldataparser)
set(TESTS_NAME ${LIBRARY_NAME}-tests)

add_library(${LIBRARY_NAME} STATIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/YamlDataLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/YamlDataDecoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/YamlDataEncoder.cpp
)

target_include_directories(${LIBRARY_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${LIBRARY_NAME} PUBLIC yaml-cpp hephora-schemamanager hephora-databackend)

enable_testing()
include(Catch)

add_executable(${TESTS_NAME}
    test/test_YamlDataLoader.cpp
    test/test_YamlDataDecoder.cpp
    test/test_YamlDataEncoder.cpp
    test/test_YamlDataRoundtrip.cpp
)

target_link_libraries(${TESTS_NAME} PRIVATE Catch2::Catch2WithMain ${LIBRARY_NAME})

catch_discover_tests(${TESTS_NAME})
