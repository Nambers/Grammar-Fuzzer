let
  pkgs = import <nixpkgs> { };
  nlohmann_json_custom = pkgs.callPackage ../scripts/nlohmann_json_custom.nix {
    cmake = pkgs.cmake;
    doCheck = false;
  };
in pkgs.mkShell {
  buildInputs = with pkgs; [
    llvm
    clang
    cmake
    ninja
    mold-wrapped
    nlohmann_json_custom
    ftxui
    clang-tools
  ];
  shellHook = ''
    export ASAN_OPTIONS=allocator_may_return_null=1:detect_leaks=0;
    export CC="${pkgs.clang}/bin/clang";
    export CXX="${pkgs.clang}/bin/clang++";
    export NIX_ENFORCE_NO_NATIVE=0;
  '';
}
