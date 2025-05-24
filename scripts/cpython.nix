let
  pkgs = import <nixpkgs> {};
  cpython = import ./cpython-instrumented.nix { inherit pkgs; };
in
pkgs.mkShell {
  buildInputs = with pkgs; [
    llvm
    clang
    cmake
    cpython
    ninja
  ];
  shellHook = ''
    export ASAN_OPTIONS='detect_leaks=0';
    export CC="${pkgs.clang}/bin/clang";
    export CXX="${pkgs.clang}/bin/clang++";
    export CLANG_BIN="${pkgs.clang}/bin";
    export CPYTHON_INCLUDE_PATH="${cpython}/include/python3.13";
    export NIX_ENFORCE_NO_NATIVE=0;
    export PATH="${cpython}/bin:$PATH";
  '';
}
