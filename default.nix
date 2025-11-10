{
  stdenv,
  lib,
  bash,
  python3,
  cmake,
  pkg-config,
  llvmPkgs,
  llvmCmakeDir,
  callPackage,
  doxygen,
  gtest,
  gbenchmark,
  glibc,
  glm,
  self,
  ...
}:
let
  project_name = "haven";
  stdinc_c   = "${stdenv.cc.libc.dev}/include";
  compiler_builtins = "${llvmPkgs.clang}/resource-root/include";
  haven_c_flags = "-I${stdinc_c};-I${compiler_builtins}";

  checkPythonEnv = python3.withPackages (p: with p; [
    venvShellHook
    lark
  ]);
in
stdenv.mkDerivation {
  pname = "${project_name}";
  version = "1.0.0";
  src = ./.;
  nativeBuildInputs = [ cmake bash checkPythonEnv pkg-config llvmPkgs.clang llvmPkgs.libllvm llvmPkgs.libclang llvmPkgs.lld doxygen ];
  buildInputs = [ llvmPkgs.libllvm llvmPkgs.libclang llvmPkgs.clang llvmPkgs.lld gtest gbenchmark glm ];

  cmakeFlags = [
    "-DLLVM_DIR=${llvmCmakeDir}"
    "-DHAVEN_C_FLAGS=${lib.escapeShellArg haven_c_flags}"
  ];

  postPatch = ''
    patchShebangs --build scripts
  '';

  checkPhase = ''
    echo "Running CTest..."
    ctest --output-on-failure

    echo "Checking examples compile..."
    ../scripts/verify-examples-compile.sh ${lib.escapeShellArg haven_c_flags}

    echo "Checking examples parse with Lark grammar..."
    ../scripts/verify-examples-grammar.sh ${self}/docs/haven.lark
  '';

  doCheck = true;
}
