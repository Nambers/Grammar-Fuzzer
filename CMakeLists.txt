cmake_minimum_required(VERSION 3.18)
project(fuzzer VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(nlohmann_json 3.12.0 REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
message(STATUS "Python3_INCLUDE_DIRS: ${Python3_INCLUDE_DIRS}")

file(GLOB_RECURSE SOURCE_FILES ${CMAKE_SOURCE_DIR}/src/*.cpp)

add_executable(pyFuzzer
    ${SOURCE_FILES}
)
target_include_directories(pyFuzzer PRIVATE ${CMAKE_SOURCE_DIR}/src ${Python3_INCLUDE_DIRS})
target_link_libraries(pyFuzzer PRIVATE ${Python3_LIBRARIES} nlohmann_json::nlohmann_json)

target_compile_options(pyFuzzer PRIVATE -fsanitize=address,undefined,fuzzer -g -fno-omit-frame-pointer -flto)
target_link_options(pyFuzzer PRIVATE -fsanitize=address,undefined,fuzzer -flto)

add_subdirectory(test)
