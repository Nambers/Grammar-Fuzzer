#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(realpath "$(dirname $0)")

echo "[build_wrapper.sh] Building QuickJS target inside nix-shell..."
nix-shell "$SCRIPT_DIR/quickjs-pkg.nix" --run "bash $SCRIPT_DIR/build.sh" --pure
