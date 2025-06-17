let
  pkgs = import <nixpkgs> { };
  cpython-pkg = pkgs.callPackage ./cpython-pkg.nix {
    fuzzCFlags = pkgs.lib.concatStringsSep " " [
      "-g"
      "-fno-omit-frame-pointer"
      "-O1"
      "-fprofile-instr-generate"
      "-fcoverage-mapping"
    ];

    fuzzLDFlags = pkgs.lib.concatStringsSep " " [
      "-fprofile-instr-generate"
      "-fcoverage-mapping"
      "-fuse-ld=mold"
    ];
  };
  nlohmann_json_custom = pkgs.callPackage ./nlohmann_json_custom.nix {
    cmake = pkgs.cmake;
    doCheck = false;
  };
in pkgs.mkShell {
  stdenv = pkgs.ccacheStdenv;
  buildInputs = with pkgs; [
    llvm
    clang
    cmake
    cpython-pkg
    ninja
    lcov
    mold-wrapped
    nlohmann_json_custom
    ftxui
  ];
  shellHook = ''
    export ASAN_OPTIONS='detect_leaks=0';
    export CC="${pkgs.clang}/bin/clang";
    export CXX="${pkgs.clang}/bin/clang++";
    export CLANG_BIN="${pkgs.clang}/bin";
    export NIX_ENFORCE_NO_NATIVE=0;
    export CPYTHON_LIB="${cpython-pkg}/lib";
    export PATH="${cpython-pkg}/bin:$PATH";
  '';
}
