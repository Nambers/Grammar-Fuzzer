# Grammar fuzzer

Syntax aware token/source codes text mutation based fuzzer.

## Features / Contribution

- scope tracking
- declaration and execution statements follow different mutation engines
- adaptive mutation rate
- mutate multiple scope at the same times (attribution problem)
- Higher level general language features support -> e.g. symbol overload (prototype pollution under JS, class-level overload and inherit under Python)
- SandBox-able, iterated scope: e.g. sandbox inner python

## Targets

- CPython
- JavaScript
- LUA
- etc.

## Previous works

- Reflecta
- Nautils


---

or collect branches cover by existent tests. Then set those as "known" to make fuzzer find more deeper branches.

## Pipeline

![pipeline](pipeline.svg)
