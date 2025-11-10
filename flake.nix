{
  description = "The Haven programming language";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
    let
      pkgs = import nixpkgs { inherit system; };
      llvmPkgs = pkgs.llvmPackages_18;
      llvmCmakeDir = "${llvmPkgs.libllvm.dev}/lib/cmake/llvm";
      haven = pkgs.callPackage ./default.nix { inherit llvmPkgs llvmCmakeDir self; stdenv = llvmPkgs.stdenv; };
    in {
      apps.default = {
        type = "app";
        program = "${haven}/bin/haven";
        meta = with pkgs.lib; {
          description = "The Haven programming language";
          license = licenses.mit;
        };
      };

      packages.default = haven;

      devShells.default = pkgs.mkShell {
        nativeBuildInputs = with pkgs; [ cmake ninja pkg-config llvmPkgs.clang llvmPkgs.libllvm llvmPkgs.lld gtest gbenchmark doxygen ];
        CMAKE_PREFIX_PATH = llvmCmakeDir;
      };
    });
}
