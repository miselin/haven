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
      llvmOcaml =
        (pkgs.ocamlPackages.callPackage (pkgs.path + "/pkgs/development/ocaml-modules/llvm") {
          libllvm = llvmPkgs.libllvm;
        }).overrideAttrs
          (old: {
            patches = (old.patches or [ ]) ++ [ ./nix/ocaml-llvm-macos-ext-dll.patch ];
          });
      havenLegacy = pkgs.callPackage ./default.nix { inherit llvmPkgs llvmCmakeDir self; stdenv = llvmPkgs.stdenv; };
      havenOcaml = pkgs.callPackage ./src/default.nix {
        inherit llvmOcaml;
        llvmPackages = llvmPkgs;
        repoSrc = ./.;
      };
    in {
      apps.default = {
        type = "app";
        program = "${havenOcaml}/bin/haven";
        meta = with pkgs.lib; {
          description = "The Haven programming language";
          license = licenses.mit;
        };
      };

      packages.default = havenOcaml;
      packages.ocaml = havenOcaml;
      packages.ml = havenOcaml;
      packages.legacy = havenLegacy;

      checks.default = havenOcaml;
      checks.ocaml = havenOcaml;
      checks.legacy = havenLegacy;

      devShells.default = pkgs.mkShell {
        nativeBuildInputs = with pkgs; [
          cmake
          ninja
          pkg-config
          dune_3
          ocamlPackages.ocaml
          ocamlPackages.cmdliner
          ocamlPackages.fmt
          ocamlPackages.linol-lwt
          ocamlPackages.logs
          ocamlPackages.menhir
          ocamlPackages.sedlex
          ocamlPackages.yojson
          llvmPkgs.clang
          llvmPkgs.libllvm
          llvmPkgs.lld
          llvmOcaml
          gtest
          gbenchmark
          doxygen
        ];
        CMAKE_PREFIX_PATH = llvmCmakeDir;
      };
    });
}
