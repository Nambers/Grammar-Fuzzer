#!/usr/bin/env bash
set -euo pipefail

if [ "${LUA_SHELL:-}" != "1" ]; then
    echo "plz run under lua-pkg.nix"
    exit 1
fi


SCRIPT_DIR=$(realpath "$(dirname $0)")
ROOT_DIR="$SCRIPT_DIR/.."
BUILD_PATH="$SCRIPT_DIR/build"

if [ ! -f "$BUILD_PATH/compile_commands.json" ]; then
    echo "[gen_hints.sh] compile_commands.json not found. Run build.sh first."
    exit 1
fi

# Merge with a root-level symlink so clangd / VS Code can find it
ln -sf "$BUILD_PATH/compile_commands.json" "$ROOT_DIR/compile_commands.json"
echo "[gen_hints.sh] Linked compile_commands.json → $BUILD_PATH/compile_commands.json"
