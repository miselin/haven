open Format

module Analysis = Haven.Ast.Analysis
module Convert = Haven.Ast.Convert
module Imports = Haven.Ast.Imports
module Pretty = Haven.Ast.Pretty

let string_of_category = function
  | Analysis.Import -> "import"
  | Analysis.TypeCheck -> "typecheck"
  | Analysis.TypeVerify -> "typeverify"
  | Analysis.Semantic -> "semantic"
  | Analysis.Purity -> "purity"
  | Analysis.Cleanup -> "cleanup"
  | Analysis.Ownership -> "ownership"

let string_of_level = function
  | Analysis.Error -> "error"
  | Analysis.Warning -> "warning"

let string_of_loc (loc : Haven_core.Loc.t) =
  let file =
    if loc.start_pos.Lexing.pos_fname = "" then "<stdin>"
    else loc.start_pos.pos_fname
  in
  Printf.sprintf "%s:%d:%d" file loc.start_pos.pos_lnum
    (loc.start_pos.pos_cnum - loc.start_pos.pos_bol + 1)

let print_diagnostic (diagnostic : Analysis.diagnostic) =
  eprintf "%s: %s: %s: %s@." (string_of_loc diagnostic.loc)
    (string_of_category diagnostic.category)
    (string_of_level diagnostic.level)
    diagnostic.message

let has_error diagnostics =
  List.exists
    (fun (diagnostic : Analysis.diagnostic) -> diagnostic.level = Analysis.Error)
    diagnostics

let collect_pipeline_diagnostics (pipeline : Analysis.Pipeline.result) =
  pipeline.typing.diagnostics
  @ pipeline.verify.diagnostics
  @ pipeline.semantic.diagnostics
  @ pipeline.purity.diagnostics
  @ pipeline.ownership.diagnostics

let parse_input = function
  | None -> Haven.Parser.parse_stdin ()
  | Some filename -> Haven.Parser.parse_file filename

let usage () =
  eprintf "usage: haven [FILE]@.";
  exit 1

let main () =
  let input =
    match Array.length Sys.argv with
    | 1 -> None
    | 2 -> Some Sys.argv.(1)
    | _ -> usage ()
  in
  let parsed =
    try parse_input input
    with
    | Failure msg
    | Sys_error msg ->
        eprintf "haven: %s@." msg;
        exit 1
  in
  let expanded = Imports.expand_cst parsed in
  if expanded.diagnostics <> [] then (
    List.iter print_diagnostic expanded.diagnostics;
    exit 1);
  let core = Convert.core_of_expanded_cst expanded.parsed in
  let pipeline = Analysis.Pipeline.run_core core in
  let diagnostics = collect_pipeline_diagnostics pipeline in
  List.iter print_diagnostic diagnostics;
  if has_error diagnostics then exit 1;
  Pretty.pp_core_program std_formatter pipeline.cleaned;
  pp_print_newline std_formatter ()

let () = main ()
