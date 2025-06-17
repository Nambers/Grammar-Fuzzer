cmake_minimum_required(VERSION 3.18)
project(fuzzer VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(nlohmann_json 3.12.0 REQUIRED)
find_package(ftxui REQUIRED)

OPTION(DISABLE_DEBUG_OUTPUT OFF)
OPTION(DISABLE_INFO_OUTPUT OFF)
if(DISABLE_DEBUG_OUTPUT)
    add_compile_definitions(DISABLE_DEBUG_OUTPUT)
endif()
if(DISABLE_INFO_OUTPUT)
    add_compile_definitions(DISABLE_INFO_OUTPUT)
endif()

file(GLOB_RECURSE SOURCE_FILES ${CMAKE_SOURCE_DIR}/src/*.cpp)

add_subdirectory(${CMAKE_SOURCE_DIR}/targets/CPython)

add_executable(pyFuzzer
    ${SOURCE_FILES}
    $<TARGET_OBJECTS:CPythonTarget>
)
target_include_directories(pyFuzzer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${ftxui_SOURCE_DIR}/include
)
target_link_libraries(pyFuzzer PRIVATE
    nlohmann_json::nlohmann_json
    ftxui::screen
    ftxui::dom
    ftxui::component
    CPythonTargetOption
)
