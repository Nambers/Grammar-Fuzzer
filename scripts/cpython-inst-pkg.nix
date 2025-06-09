{ pkgs ? import <nixpkgs> { } }:

let
  version = "3.14.0b2";
  pythonSrc = pkgs.fetchFromGitHub {
    owner = "python";
    repo = "cpython";
    rev = "v${version}";
    sha256 = "sha256-FpOanW5G08aGM9j2+tTXuGk0uTXV+KGl/eOViwTFgrQ=";
  };
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
  ];
in pkgs.stdenv.mkDerivation {
  pname = "cpython-inst-pkg";
  inherit version;
  src = pythonSrc;
  dontStrip = true;

  nativeBuildInputs =
    [ pkgs.clang pkgs.llvm pkgs.pkg-config pkgs.llvmPackages.compiler-rt-libc pkgs.keepBuildTree ];

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
    export LDFLAGS="-L${pkgs.libxcrypt}/lib -L${pkgs.llvmPackages.compiler-rt-libc}/lib/linux -lclang_rt.profile-x86_64"
    export LIBS=-L${pkgs.libxcrypt}/lib
  '';

  patches = [
    "${pkgs.path}/pkgs/development/interpreters/python/cpython/3.14/no-ldconfig.patch"
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
