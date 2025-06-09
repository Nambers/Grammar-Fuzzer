let
  pkgs = import <nixpkgs> {};
  cpython-inst = import ./cpython-cov-pkg.nix { inherit pkgs; };
in
pkgs.mkShell {
  buildInputs = with pkgs; [
    llvm
    clang
    cmake
    cpython-inst
    ninja
    lcov
    mold
  ];
  shellHook = ''
    export ASAN_OPTIONS='detect_leaks=0';
    export CC="${pkgs.clang}/bin/clang";
    export CXX="${pkgs.clang}/bin/clang++";
    export CLANG_BIN="${pkgs.clang}/bin";
    export NIX_ENFORCE_NO_NATIVE=0;
    export CPYTHON_INCLUDE_PATH="${cpython-inst}/include";
    export CPYTHON_SRC="${cpython-inst}/.build/source";
    export CPYTHON_LIB="${cpython-inst}/lib";
    export PATH="${cpython-inst}/bin:$PATH";
  '';
}
