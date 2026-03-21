cmake_minimum_required(VERSION 3.18)
project(quickjs_fuzzer VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(SRC_DIR "${ROOT_DIR}/src")
set(TGT_DIR "${ROOT_DIR}/targets/QuickJS")

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

file(GLOB_RECURSE SOURCE_FILES ${SRC_DIR}/*.cpp)

include(FetchContent)
FetchContent_Declare(
    quickjs_src
    URL https://bellard.org/quickjs/quickjs-2025-09-13-2.tar.xz
)
FetchContent_MakeAvailable(quickjs_src)

set(QUICKJS_C_SOURCES
    ${quickjs_src_SOURCE_DIR}/quickjs.c
    ${quickjs_src_SOURCE_DIR}/libregexp.c
    ${quickjs_src_SOURCE_DIR}/libunicode.c
    ${quickjs_src_SOURCE_DIR}/cutils.c
    ${quickjs_src_SOURCE_DIR}/dtoa.c
)

add_library(quickjs_inst STATIC ${QUICKJS_C_SOURCES})
target_include_directories(quickjs_inst PUBLIC "${quickjs_src_SOURCE_DIR}")
target_compile_definitions(quickjs_inst PRIVATE
    _GNU_SOURCE
    CONFIG_VERSION=\"2025-09-13-2\"
)
target_compile_options(quickjs_inst PRIVATE
    -g -fno-omit-frame-pointer -O2
    -fsanitize-coverage=edge,trace-pc-guard
)
target_link_libraries(quickjs_inst PRIVATE m dl pthread)

add_library(quickjs_cov STATIC ${QUICKJS_C_SOURCES})
target_include_directories(quickjs_cov PUBLIC "${quickjs_src_SOURCE_DIR}")
target_compile_definitions(quickjs_cov PRIVATE
    _GNU_SOURCE
    CONFIG_VERSION=\"2025-09-13-2\"
)
target_compile_options(quickjs_cov PRIVATE
    -g -fno-omit-frame-pointer -O1
    -fprofile-instr-generate -fcoverage-mapping
)
target_link_libraries(quickjs_cov PRIVATE m dl pthread)

set(TARGET_SOURCE
    ${TGT_DIR}/dumper.cpp
    ${TGT_DIR}/target.cpp
    ${TGT_DIR}/builtins.cpp
)

add_library(QuickJSTargetOption INTERFACE)
target_compile_options(QuickJSTargetOption INTERFACE
    -fsanitize=address,undefined -g -fno-omit-frame-pointer -O2
)
target_link_libraries(QuickJSTargetOption INTERFACE quickjs_inst m dl pthread)
target_link_options(QuickJSTargetOption INTERFACE
    -fsanitize=address,undefined -flto
    -fsanitize-coverage=edge,trace-pc-guard -fuse-ld=mold
)

add_library(QuickJSTarget OBJECT ${TARGET_SOURCE})
target_include_directories(QuickJSTarget PRIVATE
    "${quickjs_src_SOURCE_DIR}"
    ${SRC_DIR}
)
target_link_libraries(QuickJSTarget PRIVATE nlohmann_json::nlohmann_json quickjs_inst)

add_executable(quickjsFuzzer
    ${SOURCE_FILES}
    $<TARGET_OBJECTS:QuickJSTarget>
)
target_include_directories(quickjsFuzzer PRIVATE
    ${SRC_DIR}
    ${ftxui_SOURCE_DIR}/include
)
target_link_libraries(quickjsFuzzer PRIVATE
    nlohmann_json::nlohmann_json
    ftxui::screen
    ftxui::dom
    ftxui::component
    QuickJSTargetOption
)

set(COV_SOURCE
    ${SRC_DIR}/ast.cpp
    ${TGT_DIR}/dumper.cpp
    ${TGT_DIR}/cov.cpp
    ${TGT_DIR}/builtins.cpp
)
add_executable(QuickJSCov ${COV_SOURCE})
target_include_directories(QuickJSCov PRIVATE
    "${quickjs_src_SOURCE_DIR}"
    ${SRC_DIR}
)
target_link_libraries(QuickJSCov PRIVATE quickjs_cov nlohmann_json::nlohmann_json m dl pthread)
target_link_options(QuickJSCov PRIVATE
    -fsanitize=address,undefined
    -fprofile-instr-generate -fcoverage-mapping -flto -fuse-ld=mold
)

set(TEST_SOURCE
    ${SRC_DIR}/ast.cpp
    ${TGT_DIR}/dumper.cpp
    ${TGT_DIR}/test.cpp
    ${TGT_DIR}/builtins.cpp
)
add_executable(QuickJSTest ${TEST_SOURCE})
target_include_directories(QuickJSTest PRIVATE
    "${quickjs_src_SOURCE_DIR}"
    ${SRC_DIR}
)
target_link_libraries(QuickJSTest PRIVATE quickjs_inst nlohmann_json::nlohmann_json m dl pthread)
target_compile_options(QuickJSTest PRIVATE -g -fsanitize=address,undefined)
target_link_options(QuickJSTest PRIVATE -fsanitize=address,undefined -flto -fuse-ld=mold)

set(CONVERT_SOURCE
    ${SRC_DIR}/ast.cpp
    ${TGT_DIR}/dumper.cpp
    ${TGT_DIR}/convert.cpp
    ${TGT_DIR}/builtins.cpp
)
add_executable(QuickJSConvert ${CONVERT_SOURCE})
target_include_directories(QuickJSConvert PRIVATE
    "${quickjs_src_SOURCE_DIR}"
    ${SRC_DIR}
)
target_link_libraries(QuickJSConvert PRIVATE quickjs_inst nlohmann_json::nlohmann_json m dl pthread)
target_link_options(QuickJSConvert PRIVATE -fsanitize=address,undefined -flto -fuse-ld=mold)
