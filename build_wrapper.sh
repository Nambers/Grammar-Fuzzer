#!/bin/bash

set -e

nix-shell --pure --command "./build.sh" ./scripts/cpython-inst.nix
nix-shell --pure --command "./build_cov.sh" ./scripts/cpython-cov.nix
