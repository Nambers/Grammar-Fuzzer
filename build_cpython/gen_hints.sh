#!/bin/bash
set -e

###
# Setup environment and type hints for vscode (CPython target)
###

if ! echo "$PATH" | grep -q '/nix/store'; then
    echo "This script is intended to be run inside a Nix shell."
    echo "Use gen_hints_wrapper.sh"
    exit 1
fi

SCRIPT_DIR=$(realpath "$(dirname $0)")
ROOT_DIR="$SCRIPT_DIR/.."

if [ ! -d "$ROOT_DIR/.vscode" ]; then
    mkdir "$ROOT_DIR/.vscode"
fi

REAL_PYTHON_INCLUDE=$(readlink -f "$CPYTHON_INCLUDE_PATH/python"*/)

INCLUDE_DIRS=(
    "$REAL_PYTHON_INCLUDE"
    "$REAL_PYTHON_INCLUDE/internal"
)
IFS=':' read -ra ADDITIONAL_INCLUDE_ARRAY <<<"$ADDITIONAL_INCLUDES"
INCLUDE_DIRS+=("${ADDITIONAL_INCLUDE_ARRAY[@]}")

# VSCode IntelliSense config
cat >"$ROOT_DIR/.vscode/c_cpp_properties.json" <<EOF
{
    "configurations": [
        {
            "name": "Linux",
            "includePath": [
                "\${workspaceFolder}/**",
$(printf '\t\t\t\t"%s",\n' "${INCLUDE_DIRS[@]}")
            ],
            "defines": [],
            "compilerPath": "$CLANG_BIN/clang++",
            "compileCommands": "build_cpython/out/compile_commands.json",
            "cStandard": "c23",
            "cppStandard": "c++23",
            "intelliSenseMode": "linux-clang-x64"
        }
    ],
    "version": 4
}
EOF

# VSCode settings
cat >"$ROOT_DIR/.vscode/settings.json" <<EOF
{
    "cmake.sourceDirectory": "\${workspaceFolder}/src",
    "cmake.buildDirectory": "\${workspaceFolder}/build_cpython/out",
    "C_Cpp.default.compilerPath": "$CLANG_BIN/clang++",
    "cmake.configureOnOpen": false
}
EOF

# Clangd config
cat >"$ROOT_DIR/.clangd" <<EOF
CompileFlags:
  Add: [
$(printf '\t"-I%s",\n' "${INCLUDE_DIRS[@]}")
    "-ferror-limit=0"
  ]
EOF
