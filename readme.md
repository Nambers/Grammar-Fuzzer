# Grammar fuzzer

Syntax aware token/source codes text mutation based fuzzer.  
*Started in SEFCOM.*

## Targets

- CPython
- JavaScript TODO
- LUA TODO
- etc.

## How to use

- build instructed binary
  1. `nix-shell scripts/cpython-inst.nix`
  2. `./build.sh`
  3. collect builtin info `python3 targets/CPython/builtins.py targets/CPython/builtins.json`
- build coverage binary
  1. `nix-shell scripts/cpython-cov.nix`
  2. `./build_cov.sh`
- run fuzzer `./run.sh`
- after fuzzer terminated, build coverage result
  1. `nix-shell scripts/cpython-cov.nix`
  2. `./run_cov.sh`
  3. draw map `python cov_map.py`(install dependencies by `pip install -r requirements.txt`)

## Features / Contributions

- scope tracking
- declaration and execution statements follow different mutation engines
- adaptive mutation rate
- mutate multiple scope at the same times (attribution problem)
- Higher level general language features support -> e.g. symbol overload (prototype pollution under JS, class-level overload and inherit under Python)
- Fully customized fuzzing framework including TUI, scheduler, mutator and coverage report

## Pipeline

![pipeline](pipeline.svg)

## Previous works

- Reflecta
- Nautils
