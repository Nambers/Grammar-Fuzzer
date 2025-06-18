#!/bin/bash
set -e

###
# setup environment and type hints for vscode
###

if ! echo "$PATH" | grep -q '/nix/store'; then
    echo "This script is intended to be run inside a Nix shell."
    echo "Use gen_hints.sh"
    exit 1
fi

if [ ! -d ".vscode" ]; then
    mkdir .vscode
fi

WORK_DIR=$(readlink -f .)
SCRIPT_DIR=$(readlink -f ./scripts)
REAL_PYTHON_INCLUDE=$(readlink -f $CPYTHON_INCLUDE_PATH/python*/)

INCLUDE_DIRS=(
    "$REAL_PYTHON_INCLUDE"
    "$REAL_PYTHON_INCLUDE/internal"
)
IFS=':' read -ra ADDITIONAL_INCLUDE_ARRAY <<<"$ADDITIONAL_INCLUDES"
INCLUDE_DIRS+=("${ADDITIONAL_INCLUDE_ARRAY[@]}")

# VSCode IntelliSense config
cat >"$WORK_DIR/.vscode/c_cpp_properties.json" <<EOF
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
            "compileCommands": "build/compile_commands.json",
            "cStandard": "c23",
            "cppStandard": "c++23",
            "intelliSenseMode": "linux-clang-x64"
        }
    ],
    "version": 4
}
EOF

# VSCode settings
cat >"$WORK_DIR/.vscode/settings.json" <<EOF
{
    "cmake.sourceDirectory": "\${workspaceFolder}/src",
    "cmake.buildDirectory": "\${workspaceFolder}/build",
    "C_Cpp.default.compilerPath": "$CLANG_BIN/clang++",
    "cmake.configureOnOpen": false
}
EOF

# Clangd config
cat >"$WORK_DIR/.clangd" <<EOF
CompileFlags:
  Add: [
$(printf '\t"-I%s",\n' "${INCLUDE_DIRS[@]}")
    "-ferror-limit=0"
  ]
EOF
