#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(realpath "$(dirname $0)")

echo "[build_wrapper.sh] Building Lua target inside nix-shell..."
nix-shell "$SCRIPT_DIR/lua-pkg.nix" --run "bash $SCRIPT_DIR/build.sh"
