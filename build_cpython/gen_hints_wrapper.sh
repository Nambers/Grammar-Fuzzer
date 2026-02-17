#!/bin/bash
set -e

SCRIPT_DIR=$(realpath "$(dirname $0)")

nix-shell --pure --command "$SCRIPT_DIR/gen_hints.sh" "$SCRIPT_DIR/cpython-inst.nix"
