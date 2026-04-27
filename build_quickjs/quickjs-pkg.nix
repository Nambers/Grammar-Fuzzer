let
  pkgs = import <nixpkgs> { };
  nlohmann_json_custom = pkgs.callPackage ../scripts/nlohmann_json_custom.nix {
    cmake = pkgs.cmake;
    doCheck = false;
  };
  quick_js = pkgs.fetchzip {
    url = "https://bellard.org/quickjs/quickjs-2025-09-13-2.tar.xz";
    sha256 = "sha256-yMKkk24+t7nCHtM9Uw6ZcNOLrRVKGWyQ6PQFdSUqobI=";
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
    export QUICKJS_SHELL=1;
    export QUICKJS_SRC="${quick_js}";
  '';
}
