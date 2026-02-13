# https://github.com/NixOS/nixpkgs/blob/master/pkgs/development/interpreters/python/cpython/default.nix
{ fuzzCFlags, fuzzLDFlags }:
let
  pkgs = import <nixpkgs> {
    overlays = [
      (self: super: {
        ccacheWrapper = super.ccacheWrapper.override {
          extraConfig = ''
            export CCACHE_COMPRESS=1
            export CCACHE_DIR="/nix/var/cache/ccache"
            export CCACHE_UMASK=007
            if [ ! -d "$CCACHE_DIR" ]; then
              echo "====="
              echo "Directory '$CCACHE_DIR' does not exist"
              echo "Please create it with:"
              echo "  sudo mkdir -m0770 '$CCACHE_DIR'"
              echo "  sudo chown root:nixbld '$CCACHE_DIR'"
              echo "====="
              exit 1
            fi
            if [ ! -w "$CCACHE_DIR" ]; then
              echo "====="
              echo "Directory '$CCACHE_DIR' is not accessible for user $(whoami)"
              echo "Please verify its access permissions"
              echo "====="
              exit 1
            fi
          '';
        };
      })
    ];
  };
  enableGIL = true; # TODO weird GC bug
  sourceVersion = {
    major = "3";
    minor = "14";
    patch = "3";
    suffix = "";
  };
  pythonVersion = with sourceVersion; "${major}.${minor}";
  libPrefix =
    "python${pythonVersion}${pkgs.lib.optionalString (!enableGIL) "t"}";
  sitePackages = "lib/${libPrefix}/site-packages";
  version = with sourceVersion; "${major}.${minor}.${patch}${suffix}";
  src = pkgs.fetchFromGitHub {
    owner = "python";
    repo = "cpython";
    rev = "v${version}";
    hash = "sha256-3fxs8KFEg7+qPQrneCgJDsSDHLNNsThYNsqyE8VfSVw=";
  };
  keep-references = pkgs.lib.concatMapStringsSep " " (val: "-e ${val}") [
    (placeholder "out")
    pkgs.libxcrypt
    pkgs.tzdata
  ];

in pkgs.ccacheStdenv.mkDerivation {
  pname = "cpython-inst-pkg";
  inherit version src;
  dontStrip = true;
  # separateDebugInfo = true;
  enableParallelBuilding = true;

  nativeBuildInputs = [
    pkgs.clang
    pkgs.llvmPackages.compiler-rt-libc
    pkgs.pkg-config
    pkgs.nukeReferences
    pkgs.autoconf-archive
    pkgs.autoreconfHook
    pkgs.keepBuildTree
    pkgs.ccache
  ];

  buildInputs = with pkgs; [
    bashNonInteractive
    expat
    tzdata
    mold-wrapped
    openssl
    zlib
    bzip2
    libffi
    libxcrypt
    mpdecimal
    ncurses
    readline
    xz
    zstd
  ];

  setupHook = pkgs.python-setup-hook sitePackages;

  configureFlags = [
    "--enable-shared"
    "--with-openssl=${pkgs.openssl.dev}"
    "--with-system-expat"
    # "--with-ensurepip=no"
    "--without-ensurepip"
    "--with-tzpath=${pkgs.tzdata}/share/zoneinfo"
    "ac_cv_func_lchmod=no"
    (pkgs.lib.enableFeature enableGIL "gil")
  ];

  prePatch = ''
    echo "Injecting fuzz instrumentation flags..."
    echo 'CFLAGS += ${fuzzCFlags}' >> Makefile.pre.in
    echo 'LDFLAGS += ${fuzzLDFlags}' >> Makefile.pre.in
  '';

  preConfigure = ''
    export CC=clang
    export CXX=clang++
    export ASAN_OPTIONS=detect_leaks=0
    export CFLAGS="-I${pkgs.libxcrypt}/include"
    export LDFLAGS="-L${pkgs.libxcrypt}/lib -L${pkgs.llvmPackages.compiler-rt-libc}/lib/linux -lclang_rt.profile-x86_64"
    export LIBS="-L${pkgs.libxcrypt}/lib"

    sed -E -i -e 's/uname -r/echo/g' -e 's/uname -n/echo nixpkgs/g' config.guess
    sed -E -i -e 's/uname -r/echo/g' -e 's/uname -n/echo nixpkgs/g' configure

    substituteInPlace configure \
        --replace-fail 'libmpdec_machine=universal' 'libmpdec_machine=${
          if pkgs.stdenv.hostPlatform.isAarch64 then "uint128" else "x64"
        }'
  '';

  patches = [
    "${pkgs.path}/pkgs/development/interpreters/python/cpython/${sourceVersion.major}.${sourceVersion.minor}/no-ldconfig.patch"
  ];

  buildPhase = "make -j$NIX_BUILD_CORES";

  postInstall = ''
    # Get rid of retained dependencies on -dev packages, and remove
    # some $TMPDIR references to improve binary reproducibility.
    # Note that the .pyc file of _sysconfigdata.py should be regenerated!
    for i in $out/lib/${libPrefix}/_sysconfigdata*.py $out/lib/${libPrefix}/config-${sourceVersion.major}${sourceVersion.minor}*/Makefile; do
       sed -i $i -e "s|$TMPDIR|/no-such-path|g"
    done

    # Further get rid of references. https://github.com/NixOS/nixpkgs/issues/51668
    find $out/lib/python*/config-* -type f -print -exec nuke-refs ${keep-references} '{}' +
    find $out/lib -name '_sysconfigdata*.py*' -print -exec nuke-refs ${keep-references} '{}' +

    # Make the sysconfigdata module accessible on PYTHONPATH
    # This allows build Python to import host Python's sysconfigdata
    mkdir -p "$out/${sitePackages}"
    ln -sf "$out/lib/${libPrefix}/"_sysconfigdata*.py "$out/${sitePackages}/"

    for i in $out/lib/${libPrefix}/_sysconfig_vars*.json; do
         sed -i $i -e "s|$TMPDIR|/no-such-path|g"
    done
    find $out/lib -name '_sysconfig_vars*.json*' -print -exec nuke-refs ${keep-references} '{}' +
    ln -sf "$out/lib/${libPrefix}/"_sysconfig_vars*.json "$out/${sitePackages}/"
  '';

  postFixup = ''
    sysconfigdataName="$(make --eval $'print-sysconfigdata-name:
    \t@echo _sysconfigdata_$(ABIFLAGS)_$(MACHDEP)_$(MULTIARCH) ' print-sysconfigdata-name)"
    cat <<EOF >> "$out/nix-support/setup-hook"
    sysconfigdataHook() {
      if [ "\$1" = '$out' ]; then
        export _PYTHON_HOST_PLATFORM="${pkgs.stdenv.hostPlatform.parsed.kernel.name}-${pkgs.stdenv.hostPlatform.parsed.cpu.name}"
        export _PYTHON_SYSCONFIGDATA_NAME='$sysconfigdataName'
      fi
    }

    addEnvHooks "\$hostOffset" sysconfigdataHook
    EOF
  '';

  # disallowedReferences = [ pkgs.openssl.dev ];

  meta = with pkgs.lib; {
    description =
      "Instrumented standalone CPython ${version} with full runtime support";
    homepage = "https://www.python.org";
    license = licenses.psfl;
    platforms = platforms.unix;
  };
}
