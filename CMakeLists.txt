cmake_minimum_required(VERSION 3.15)
project(hephora-sdk LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(HEPHORA_BUILD_APPS "Build Hephora apps" ON)

include(FetchContent)

set_property(GLOBAL PROPERTY CTEST_TARGETS_ADDED 1)
include(CTest)

FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG 0.8.0
)

FetchContent_MakeAvailable(yaml-cpp)


FetchContent_Declare(
  catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.4.0
)

FetchContent_MakeAvailable(catch2)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(nlohmann_json)

set(nlohmann_json_VERSION 3.11.2)

FetchContent_Declare(
  lua
  GIT_REPOSITORY https://github.com/lua/lua.git
  GIT_TAG v5.4.7
)

FetchContent_MakeAvailable(lua)

add_subdirectory(src)

if(HEPHORA_BUILD_APPS)
  add_subdirectory(apps)
endif()