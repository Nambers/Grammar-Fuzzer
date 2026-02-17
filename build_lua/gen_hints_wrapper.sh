#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(realpath "$(dirname $0)")

nix-shell "$SCRIPT_DIR/lua-pkg.nix" --run "bash $SCRIPT_DIR/gen_hints.sh"
