cmake_minimum_required(VERSION 3.18)
project(lua_fuzzer VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Paths relative to this CMakeLists.txt
set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(SRC_DIR "${ROOT_DIR}/src")
set(TGT_DIR "${ROOT_DIR}/targets/Lua")

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
    lua_src
    URL https://www.lua.org/ftp/lua-5.5.0.tar.gz
    URL_HASH SHA256=57ccc32bbbd005cab75bcc52444052535af691789dba2b9016d5c50640d68b3d
)
FetchContent_MakeAvailable(lua_src)

file(GLOB LUA_C_SOURCES "${lua_src_SOURCE_DIR}/src/*.c")
# Remove the standalone interpreter and compiler entry points
list(FILTER LUA_C_SOURCES EXCLUDE REGEX "/(lua|luac)\\.c$")

# --- instrumented Lua static library (for the fuzzer) ---
add_library(lua_inst STATIC ${LUA_C_SOURCES})
target_include_directories(lua_inst PUBLIC "${lua_src_SOURCE_DIR}/src")
target_compile_definitions(lua_inst PRIVATE LUA_USE_POSIX LUA_USE_DLOPEN)
target_compile_options(lua_inst PRIVATE
    -g -fno-omit-frame-pointer -O2
    -fsanitize-coverage=edge,trace-pc-guard
)
target_link_libraries(lua_inst PRIVATE m dl)

# --- coverage Lua static library (for the coverage runner) ---
add_library(lua_cov STATIC ${LUA_C_SOURCES})
target_include_directories(lua_cov PUBLIC "${lua_src_SOURCE_DIR}/src")
target_compile_definitions(lua_cov PRIVATE LUA_USE_POSIX LUA_USE_DLOPEN)
target_compile_options(lua_cov PRIVATE
    -g -fno-omit-frame-pointer -O1
    -fprofile-instr-generate -fcoverage-mapping
)
target_link_libraries(lua_cov PRIVATE m dl)

# ============================================================================
# Target object library
# ============================================================================
set(TARGET_SOURCE
    ${TGT_DIR}/dumper.cpp
    ${TGT_DIR}/target.cpp
    ${TGT_DIR}/builtins.cpp
)

add_library(LuaTargetOption INTERFACE)
target_compile_options(LuaTargetOption INTERFACE
    -fsanitize=address,undefined -g -fno-omit-frame-pointer -O2
)
target_link_libraries(LuaTargetOption INTERFACE lua_inst)
target_link_options(LuaTargetOption INTERFACE
    -fsanitize=address,undefined -flto
    -fsanitize-coverage=edge,trace-pc-guard -fuse-ld=mold
)

add_library(LuaTarget OBJECT ${TARGET_SOURCE})
target_include_directories(LuaTarget PRIVATE
    "${lua_src_SOURCE_DIR}/src"
    ${SRC_DIR}
)
target_link_libraries(LuaTarget PRIVATE nlohmann_json::nlohmann_json lua_inst)

# ============================================================================
# Main fuzzer executable
# ============================================================================
add_executable(luaFuzzer
    ${SOURCE_FILES}
    $<TARGET_OBJECTS:LuaTarget>
)
target_include_directories(luaFuzzer PRIVATE
    ${SRC_DIR}
    ${ftxui_SOURCE_DIR}/include
)
target_link_libraries(luaFuzzer PRIVATE
    nlohmann_json::nlohmann_json
    ftxui::screen
    ftxui::dom
    ftxui::component
    LuaTargetOption
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
add_executable(LuaCov ${COV_SOURCE})
target_include_directories(LuaCov PRIVATE
    "${lua_src_SOURCE_DIR}/src"
    ${SRC_DIR}
)
target_link_libraries(LuaCov PRIVATE lua_cov nlohmann_json::nlohmann_json)
target_link_options(LuaCov PRIVATE
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
add_executable(LuaTest ${TEST_SOURCE})
target_include_directories(LuaTest PRIVATE
    "${lua_src_SOURCE_DIR}/src"
    ${SRC_DIR}
)
target_link_libraries(LuaTest PRIVATE lua_inst nlohmann_json::nlohmann_json)
target_compile_options(LuaTest PRIVATE -g -fsanitize=address,undefined)
target_link_options(LuaTest PRIVATE -fsanitize=address,undefined -flto -fuse-ld=mold)

# ============================================================================
#  converter
# ============================================================================
set(CONVERT_SOURCE
    ${SRC_DIR}/ast.cpp
    ${TGT_DIR}/dumper.cpp
    ${TGT_DIR}/convert.cpp
    ${TGT_DIR}/builtins.cpp
)
add_executable(LuaConvert ${CONVERT_SOURCE})
target_include_directories(LuaConvert PRIVATE
    "${lua_src_SOURCE_DIR}/src"
    ${SRC_DIR}
)
target_link_libraries(LuaConvert PRIVATE lua_inst nlohmann_json::nlohmann_json)
target_link_options(LuaConvert PRIVATE -fsanitize=address,undefined -flto -fuse-ld=mold)
