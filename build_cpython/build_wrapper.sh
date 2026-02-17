#!/bin/bash
set -e

SCRIPT_DIR=$(realpath "$(dirname $0)")

nix-shell --pure --command "$SCRIPT_DIR/build.sh" "$SCRIPT_DIR/cpython-inst.nix"
nix-shell --pure --command "$SCRIPT_DIR/build_cov.sh" "$SCRIPT_DIR/cpython-cov.nix"
