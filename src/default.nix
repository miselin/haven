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
    ocamlPackages.cmdliner
    ocamlPackages.fmt
    ocamlPackages.linol-lwt
    llvmOcaml
    ocamlPackages.logs
    ocamlPackages.menhirLib
    ocamlPackages.sedlex
  ];

  buildPhase = ''
    runHook preBuild
    dune build @runtest bin/haven.exe bin/hvfmt.exe bin/hvast.exe bin/lsp/hvlsp.exe
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/bin
    cp _build/default/bin/haven.exe $out/bin/haven
    cp _build/default/bin/hvfmt.exe $out/bin/hvfmt
    cp _build/default/bin/hvast.exe $out/bin/hvast-ml
    cp _build/default/bin/lsp/hvlsp.exe $out/bin/haven-lsp
    runHook postInstall
  '';
}
