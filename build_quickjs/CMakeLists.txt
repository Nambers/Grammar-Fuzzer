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

option(DISABLE_DEBUG_OUTPUT OFF)
option(DISABLE_INFO_OUTPUT OFF)
if(DISABLE_DEBUG_OUTPUT)
    add_compile_definitions(DISABLE_DEBUG_OUTPUT)
endif()
if(DISABLE_INFO_OUTPUT)
    add_compile_definitions(DISABLE_INFO_OUTPUT)
endif()

file(GLOB_RECURSE SOURCE_FILES ${SRC_DIR}/*.cpp)

include(FetchContent)
fetchcontent_declare(
    quickjs_src
    # URL https://bellard.org/quickjs/quickjs-2025-09-13-2.tar.xz
    SOURCE_DIR $ENV{QUICKJS_SRC}
)
fetchcontent_makeavailable(quickjs_src)

add_library(QuickJSSystemLibs INTERFACE)
target_link_libraries(QuickJSSystemLibs INTERFACE m dl pthread)

add_library(QuickJSLibCommon INTERFACE)
target_include_directories(QuickJSLibCommon INTERFACE "${quickjs_src_SOURCE_DIR}")
target_compile_definitions(QuickJSLibCommon INTERFACE
    _GNU_SOURCE
    CONFIG_VERSION="2025-09-13-2"
)

add_library(QuickJSToolCommon INTERFACE)
target_include_directories(QuickJSToolCommon INTERFACE
    "${quickjs_src_SOURCE_DIR}"
    ${SRC_DIR}
)
target_link_libraries(QuickJSToolCommon INTERFACE
    nlohmann_json::nlohmann_json
)

add_library(QuickJSLinkCommon INTERFACE)
target_link_options(QuickJSLinkCommon INTERFACE
    -fsanitize=address,undefined
    -flto
    -fuse-ld=mold
)

add_library(QuickJSCovLinkOptions INTERFACE)
target_link_options(QuickJSCovLinkOptions INTERFACE
    -fprofile-instr-generate
    -fcoverage-mapping
)

set(QUICKJS_C_SOURCES
    ${quickjs_src_SOURCE_DIR}/quickjs.c
    ${quickjs_src_SOURCE_DIR}/libregexp.c
    ${quickjs_src_SOURCE_DIR}/libunicode.c
    ${quickjs_src_SOURCE_DIR}/cutils.c
    ${quickjs_src_SOURCE_DIR}/dtoa.c
)

add_library(quickjs_inst STATIC ${QUICKJS_C_SOURCES})
target_link_libraries(quickjs_inst
    PRIVATE QuickJSLibCommon
    PUBLIC QuickJSSystemLibs
)
target_compile_options(quickjs_inst PRIVATE
    -g -fno-omit-frame-pointer -O2
    -fsanitize-coverage=edge,trace-pc-guard
)

add_library(quickjs_cov STATIC ${QUICKJS_C_SOURCES})
target_link_libraries(quickjs_cov
    PRIVATE QuickJSLibCommon
    PUBLIC QuickJSSystemLibs
)
target_compile_options(quickjs_cov PRIVATE
    -g -fno-omit-frame-pointer -O1
    -fprofile-instr-generate -fcoverage-mapping
)

set(TARGET_SOURCE
    ${TGT_DIR}/dumper.cpp
    ${TGT_DIR}/target.cpp
    ${TGT_DIR}/builtins.cpp
)

add_library(QuickJSTargetOption INTERFACE)
target_compile_options(QuickJSTargetOption INTERFACE
    -fsanitize=address,undefined -g -fno-omit-frame-pointer -O2
)
target_link_libraries(QuickJSTargetOption INTERFACE quickjs_inst QuickJSLinkCommon)
target_link_options(QuickJSTargetOption INTERFACE
    -fsanitize-coverage=edge,trace-pc-guard
)

add_library(QuickJSTarget OBJECT ${TARGET_SOURCE})
target_link_libraries(QuickJSTarget PRIVATE QuickJSToolCommon quickjs_inst)

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
target_link_libraries(QuickJSCov PRIVATE QuickJSToolCommon quickjs_cov QuickJSLinkCommon QuickJSCovLinkOptions)

set(TEST_SOURCE
    ${SRC_DIR}/ast.cpp
    ${TGT_DIR}/dumper.cpp
    ${TGT_DIR}/test.cpp
    ${TGT_DIR}/builtins.cpp
)
add_executable(QuickJSTest ${TEST_SOURCE})
target_link_libraries(QuickJSTest PRIVATE QuickJSToolCommon quickjs_inst QuickJSLinkCommon)
target_compile_options(QuickJSTest PRIVATE -g -fsanitize=address,undefined)

set(CONVERT_SOURCE
    ${SRC_DIR}/ast.cpp
    ${TGT_DIR}/dumper.cpp
    ${TGT_DIR}/convert.cpp
    ${TGT_DIR}/builtins.cpp
)
add_executable(QuickJSConvert ${CONVERT_SOURCE})
target_link_libraries(QuickJSConvert PRIVATE QuickJSToolCommon quickjs_inst QuickJSLinkCommon)

add_executable(QuickJSBuiltinsProbe ${TGT_DIR}/builtins_probe.cpp)
target_include_directories(QuickJSBuiltinsProbe PRIVATE "${quickjs_src_SOURCE_DIR}")
target_link_libraries(QuickJSBuiltinsProbe PRIVATE quickjs_inst QuickJSLinkCommon)
