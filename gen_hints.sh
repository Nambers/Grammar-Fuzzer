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

# VSCode IntelliSense config
cat > "$WORK_DIR/.vscode/c_cpp_properties.json" <<EOF
{
    "configurations": [
        {
            "name": "Linux",
            "includePath": [
                "\${workspaceFolder}/**",
                "$CPYTHON_INCLUDE_PATH/",
                "$CPYTHON_INCLUDE_PATH/internal"
            ],
            "defines": [],
            "compilerPath": "$CLANG_BIN/clang++",
            "cStandard": "c23",
            "cppStandard": "c++23",
            "intelliSenseMode": "linux-clang-x64"
        }
    ],
    "version": 4
}
EOF

# VSCode settings
cat > "$WORK_DIR/.vscode/settings.json" <<EOF
{
    "cmake.sourceDirectory": "\${workspaceFolder}/src",
    "cmake.buildDirectory": "\${workspaceFolder}/build",
    "C_Cpp.default.compilerPath": "$CLANG_BIN/clang++",
    "cmake.configureEnvironment": {
        "PYTHON_PATH": "$CPYTHON_INCLUDE_PATH"
    },
    "cmake.configureOnOpen": false
}
EOF

# Clangd config
cat > "$WORK_DIR/.clangd" <<EOF
CompileFlags:
  Add: [
    "-I$CPYTHON_INCLUDE_PATH",
    "-I$CPYTHON_INCLUDE_PATH/internal",
    "-ferror-limit=0"
  ]
EOF
