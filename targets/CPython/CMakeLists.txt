cmake_minimum_required(VERSION 3.14)
project(Target_CPython LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(nlohmann_json 3.12.0 REQUIRED)
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
message(STATUS "Python3_INCLUDE_DIRS: ${Python3_INCLUDE_DIRS}")

# --- target ---
set(TARGET_SOURCE
    ${PROJECT_SOURCE_DIR}/dumper.cpp
    ${PROJECT_SOURCE_DIR}/target.cpp
)

add_library(CPythonTargetOption INTERFACE)
target_compile_options(CPythonTargetOption INTERFACE -fsanitize=address,undefined -g -fno-omit-frame-pointer -O2)
target_link_libraries(CPythonTargetOption INTERFACE ${Python3_LIBRARIES})
target_link_options(CPythonTargetOption INTERFACE -fsanitize=address,undefined -flto -fsanitize-coverage=edge,trace-pc-guard -fuse-ld=mold)

add_library(CPythonTarget OBJECT ${TARGET_SOURCE})
target_include_directories(CPythonTarget PRIVATE
    ${Python3_INCLUDE_DIRS}
    ${PROJECT_SOURCE_DIR}/../../src
)
target_link_libraries(CPythonTarget PRIVATE nlohmann_json::nlohmann_json)


# --- cov ---
set(COV_SOURCE
    ${PROJECT_SOURCE_DIR}/../../src/ast.cpp
    ${PROJECT_SOURCE_DIR}/dumper.cpp
    ${PROJECT_SOURCE_DIR}/cov.cpp
)

add_executable(CPythonCov ${COV_SOURCE})
target_include_directories(CPythonCov PRIVATE ${Python3_INCLUDE_DIRS} ${PROJECT_SOURCE_DIR}/../../src)
target_link_libraries(CPythonCov PRIVATE ${Python3_LIBRARIES} nlohmann_json::nlohmann_json)
target_link_options(CPythonCov PRIVATE -fsanitize=address,undefined -fprofile-instr-generate -fcoverage-mapping -flto -fuse-ld=mold)

# --- test ---
set(TEST_SOURCE
    ${PROJECT_SOURCE_DIR}/../../src/ast.cpp
    ${PROJECT_SOURCE_DIR}/dumper.cpp
    ${PROJECT_SOURCE_DIR}/test.cpp
)

add_executable(CPythonTest ${TEST_SOURCE})
target_include_directories(CPythonTest PRIVATE ${Python3_INCLUDE_DIRS} ${PROJECT_SOURCE_DIR}/../../src)
target_link_libraries(CPythonTest PRIVATE ${Python3_LIBRARIES} nlohmann_json::nlohmann_json)
target_compile_options(CPythonTest PRIVATE -g -fsanitize=address,undefined)
target_link_options(CPythonTest PRIVATE -fsanitize=address,undefined -flto -fuse-ld=mold)

