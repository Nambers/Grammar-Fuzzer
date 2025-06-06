cmake_minimum_required(VERSION 3.18)
project(fuzzer VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(nlohmann_json 3.12.0 REQUIRED)
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
message(STATUS "Python3_INCLUDE_DIRS: ${Python3_INCLUDE_DIRS}")

# --- Fetch FTXUI --------------------------------------------------------------
include(FetchContent)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/arthursonzogni/ftxui.git
  GIT_TAG        v6.1.9
  GIT_PROGRESS   TRUE
  GIT_SHALLOW    TRUE
  EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(ftxui)
# ------------------------------------------------------------------------------

OPTION(DISABLE_DEBUG_OUTPUT OFF)
OPTION(DISABLE_INFO_OUTPUT OFF)
if(DISABLE_DEBUG_OUTPUT)
    add_compile_definitions(DISABLE_DEBUG_OUTPUT)
endif()
if(DISABLE_INFO_OUTPUT)
    add_compile_definitions(DISABLE_INFO_OUTPUT)
endif()

file(GLOB_RECURSE SOURCE_FILES ${CMAKE_SOURCE_DIR}/src/*.cpp)

add_executable(pyFuzzer
    ${SOURCE_FILES}
)
target_include_directories(pyFuzzer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${Python3_INCLUDE_DIRS}
    ${ftxui_SOURCE_DIR}/include
)
target_link_libraries(pyFuzzer PRIVATE
    ${Python3_LIBRARIES}
    nlohmann_json::nlohmann_json
    ftxui::screen
    ftxui::dom
    ftxui::component
)

target_compile_options(pyFuzzer PRIVATE -g -fno-omit-frame-pointer -O2)
target_link_options(pyFuzzer PRIVATE -fsanitize=address,undefined,fuzzer-no-link -fsanitize-coverage=edge,trace-pc-guard)

add_subdirectory(test)
