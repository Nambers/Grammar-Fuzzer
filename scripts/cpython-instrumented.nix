{ pkgs ? import <nixpkgs> { } }:

let
  version = "3.13.0";
  pythonSrc = pkgs.fetchFromGitHub {
    owner = "python";
    repo = "cpython";
    rev = "v${version}";
    sha256 = "sha256-GERYjuSqBDj/tYYv5sJTgNeTX2p/LjOulH4PDsWx7Hg=";
  };
  fuzzCFlags = pkgs.lib.concatStringsSep " " [
    "-g"
    "-fno-omit-frame-pointer"
    "-O1"
    "-fsanitize=fuzzer-no-link,address,undefined"
    "-fno-sanitize=function,alignment"
    "-fsanitize-coverage=inline-8bit-counters,trace-pc-guard"
  ];

  fuzzLDFlags = pkgs.lib.concatStringsSep " " [
    "-fsanitize=fuzzer-no-link,address,undefined"
    "-fsanitize-coverage=inline-8bit-counters,trace-pc-guard"
  ];
in pkgs.stdenv.mkDerivation {
  pname = "cpython-instrumented";
  inherit version;
  src = pythonSrc;

  nativeBuildInputs =
    [ pkgs.clang pkgs.llvm pkgs.pkg-config pkgs.llvmPackages.compiler-rt-libc ];

  buildInputs = [
    pkgs.expat
    pkgs.libxcrypt
    pkgs.openssl
    pkgs.bzip2
    pkgs.libffi
    pkgs.mpdecimal
    pkgs.ncurses
    pkgs.readline
    pkgs.sqlite
    pkgs.zlib
    pkgs.xz
    pkgs.tzdata
  ];

  configureFlags = [
    "--prefix $out"
    "--enable-shared"
    "--with-openssl=${pkgs.openssl.dev}"
    "--with-system-expat"
    "--with-ensurepip=no"
    "ac_cv_func_lchmod=no"
  ];

  postPatch = ''
    echo "Appending sanitizer coverage flags to Makefile.pre.in..."

    echo 'CFLAGS := ${fuzzCFlags} $(CFLAGS)' >> Makefile.pre.in

    echo 'LDFLAGS := ${fuzzLDFlags} $(LDFLAGS)' >> Makefile.pre.in
  '';


  passthru.pipSupport = false;

  preConfigure = ''
    export ASAN_OPTIONS='detect_leaks=0';
    export CC=clang
    export CXX=clang++
    export CFLAGS=" -I${pkgs.libxcrypt}/include"
    export LDFLAGS="-L${pkgs.libxcrypt}/lib -L${pkgs.llvmPackages.compiler-rt-libc}/lib/linux"
    export LIBS=-L${pkgs.libxcrypt}/lib
  '';

  patches = [
    "${pkgs.path}/pkgs/development/interpreters/python/cpython/3.13/no-ldconfig.patch"
  ];

  buildPhase = ''
    make -j$NIX_BUILD_CORES
  '';

  installPhase = ''
    make install prefix=$out
  '';

  meta = with pkgs.lib; {
    description =
      "Instrumented CPython with libFuzzer-compatible build (libpython.so + headers)";
    license = licenses.psfl;
    platforms = platforms.unix;
  };
}
