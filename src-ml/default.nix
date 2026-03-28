{
  lib,
  stdenv,
  ocamlPackages,
  ...
}:

ocamlPackages.buildDunePackage {
  pname = "haven";
  version = "1.0.0";
  src = ./.;

  nativeBuildInputs = [ ocamlPackages.menhir ];
  propagatedBuildInputs = [
    ocamlPackages.fmt
    ocamlPackages.linol-lwt
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
