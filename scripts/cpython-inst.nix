let
  pkgs = import <nixpkgs> { };
  cpython-pkg = pkgs.callPackage ./cpython-pkg.nix {
    fuzzCFlags = pkgs.lib.concatStringsSep " " [
      "-g"
      "-fno-omit-frame-pointer"
      "-O2"
      "-fsanitize=fuzzer-no-link,address,undefined"
      "-fno-sanitize=function,alignment"
      "-fsanitize-coverage=trace-pc-guard"
    ];

    fuzzLDFlags = pkgs.lib.concatStringsSep " " [
      "-fsanitize=fuzzer-no-link,address,undefined"
      "-fsanitize-coverage=trace-pc-guard"
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
    export ASAN_OPTIONS=allocator_may_return_null=1:detect_leaks=0;
    export CC="${pkgs.clang}/bin/clang";
    export CXX="${pkgs.clang}/bin/clang++";
    export CLANG_BIN="${pkgs.clang}/bin";
    export NIX_ENFORCE_NO_NATIVE=0;
    export CPYTHON_INCLUDE_PATH="${cpython-pkg}/include";
    export PATH="${cpython-pkg}/bin:$PATH";
    export COMPILER_RT_LIBC="${pkgs.llvmPackages.compiler-rt-libc}/lib/linux";
    export ADDITIONAL_INCLUDES="${nlohmann_json_custom}/include:${pkgs.ftxui}/include";
  '';
}
