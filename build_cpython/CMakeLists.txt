cmake_minimum_required(VERSION 3.18)
project(cpython_fuzzer VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Paths relative to this CMakeLists.txt
set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(SRC_DIR  "${ROOT_DIR}/src")
set(TGT_DIR  "${ROOT_DIR}/targets/CPython")

find_package(nlohmann_json 3.12.0 REQUIRED)
find_package(ftxui REQUIRED)
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
message(STATUS "Python3_INCLUDE_DIRS: ${Python3_INCLUDE_DIRS}")

OPTION(DISABLE_DEBUG_OUTPUT OFF)
OPTION(DISABLE_INFO_OUTPUT OFF)
if(DISABLE_DEBUG_OUTPUT)
    add_compile_definitions(DISABLE_DEBUG_OUTPUT)
endif()
if(DISABLE_INFO_OUTPUT)
    add_compile_definitions(DISABLE_INFO_OUTPUT)
endif()

file(GLOB_RECURSE SOURCE_FILES ${SRC_DIR}/*.cpp)

# ============================================================================
# Target object library
# ============================================================================
set(TARGET_SOURCE
    ${TGT_DIR}/dumper.cpp
    ${TGT_DIR}/target.cpp
    ${TGT_DIR}/builtins.cpp
)

add_library(CPythonTargetOption INTERFACE)
target_compile_options(CPythonTargetOption INTERFACE
    -fsanitize=address,undefined -g -fno-omit-frame-pointer -O2
)
target_link_libraries(CPythonTargetOption INTERFACE ${Python3_LIBRARIES})
target_link_options(CPythonTargetOption INTERFACE
    -fsanitize=address,undefined -flto
    -fsanitize-coverage=edge,trace-pc-guard -fuse-ld=mold
)

add_library(CPythonTarget OBJECT ${TARGET_SOURCE})
target_include_directories(CPythonTarget PRIVATE
    ${Python3_INCLUDE_DIRS}
    ${SRC_DIR}
)
target_link_libraries(CPythonTarget PRIVATE nlohmann_json::nlohmann_json)

# ============================================================================
# Main fuzzer executable
# ============================================================================
add_executable(pyFuzzer
    ${SOURCE_FILES}
    $<TARGET_OBJECTS:CPythonTarget>
)
target_include_directories(pyFuzzer PRIVATE
    ${SRC_DIR}
    ${ftxui_SOURCE_DIR}/include
)
target_link_libraries(pyFuzzer PRIVATE
    nlohmann_json::nlohmann_json
    ftxui::screen
    ftxui::dom
    ftxui::component
    CPythonTargetOption
)

# ============================================================================
# Coverage runner
# ============================================================================
set(COV_SOURCE
    ${SRC_DIR}/ast.cpp
    ${TGT_DIR}/dumper.cpp
    ${TGT_DIR}/cov.cpp
    ${TGT_DIR}/builtins.cpp
)

add_executable(CPythonCov ${COV_SOURCE})
target_include_directories(CPythonCov PRIVATE ${Python3_INCLUDE_DIRS} ${SRC_DIR})
target_link_libraries(CPythonCov PRIVATE ${Python3_LIBRARIES} nlohmann_json::nlohmann_json)
target_link_options(CPythonCov PRIVATE
    -fsanitize=address,undefined
    -fprofile-instr-generate -fcoverage-mapping -flto -fuse-ld=mold
)

# ============================================================================
# Standalone test
# ============================================================================
set(TEST_SOURCE
    ${SRC_DIR}/ast.cpp
    ${TGT_DIR}/dumper.cpp
    ${TGT_DIR}/test.cpp
    ${TGT_DIR}/builtins.cpp
)

add_executable(CPythonTest ${TEST_SOURCE})
target_include_directories(CPythonTest PRIVATE ${Python3_INCLUDE_DIRS} ${SRC_DIR})
target_link_libraries(CPythonTest PRIVATE ${Python3_LIBRARIES} nlohmann_json::nlohmann_json)
target_compile_options(CPythonTest PRIVATE -g -fsanitize=address,undefined)
target_link_options(CPythonTest PRIVATE -fsanitize=address,undefined -flto -fuse-ld=mold)

# ============================================================================
# Batch JSON → Python converter
# ============================================================================
set(CONVERT_SOURCE
    ${SRC_DIR}/ast.cpp
    ${TGT_DIR}/dumper.cpp
    ${TGT_DIR}/convert.cpp
    ${TGT_DIR}/builtins.cpp
)
add_executable(CPythonConvert ${CONVERT_SOURCE})
target_include_directories(CPythonConvert PRIVATE ${Python3_INCLUDE_DIRS} ${SRC_DIR})
target_link_libraries(CPythonConvert PRIVATE ${Python3_LIBRARIES} nlohmann_json::nlohmann_json)
target_link_options(CPythonConvert PRIVATE -fsanitize=address,undefined -flto -fuse-ld=mold)
