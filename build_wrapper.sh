#!/bin/bash

set -e

nix-shell --pure --command "./build.sh" ./scripts/cpython-inst.nix --pure
