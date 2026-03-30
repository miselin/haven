{
  lib,
  stdenv,
  llvmOcaml,
  llvmPackages,
  ocamlPackages,
  repoSrc,
  ...
}:

ocamlPackages.buildDunePackage {
  pname = "haven";
  version = "1.0.0";
  src = ./.;
  DUNE_CACHE = "disabled";
  HAVEN_REPO_ROOT = repoSrc;

  nativeBuildInputs = [
    llvmPackages.clang
    ocamlPackages.menhir
  ];
  propagatedBuildInputs = [
    ocamlPackages.fmt
    ocamlPackages.linol-lwt
    llvmOcaml
    ocamlPackages.logs
    ocamlPackages.menhirLib
    ocamlPackages.sedlex
  ];

  buildPhase = ''
    # Keep the Nix build focused on the OCaml build/test entrypoint.
    dune build @runtest
  '';

  installPhase = ''
    mkdir -p $out
  '';
}
