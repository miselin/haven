{
  stdenv,
  lib,
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
  ...
}:
let
  project_name = "haven";
  stdinc_c   = "${stdenv.cc.libc.dev}/include";
  compiler_builtins = "${llvmPkgs.clang}/resource-root/include";
  haven_c_flags = "-I${stdinc_c};-I${compiler_builtins}";
in
stdenv.mkDerivation {
  pname = "${project_name}";
  version = "1.0.0";
  src = ./.;
  nativeBuildInputs = [ cmake pkg-config llvmPkgs.clang llvmPkgs.libllvm llvmPkgs.libclang llvmPkgs.lld doxygen ];
  buildInputs = [ llvmPkgs.libllvm llvmPkgs.libclang llvmPkgs.clang llvmPkgs.lld gtest gbenchmark glm ];

  cmakeFlags = [
    "-DLLVM_DIR=${llvmCmakeDir}"
    "-DHAVEN_C_FLAGS=${lib.escapeShellArg haven_c_flags}"
  ];

  checkPhase = ''
    echo "Running CTest..."
    ctest --output-on-failure
  '';

  doCheck = true;
}
