#!/bin/bash

set -e

nix-shell --pure --command "./gen_hints.sh" ./scripts/cpython-inst.nix
