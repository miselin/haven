open Format

module Analysis = Haven.Ast.Analysis
module Convert = Haven.Ast.Convert
module Imports = Haven.Ast.Imports
module Llvm_ir = Haven.Ast.Llvm_ir
module Pretty = Haven.Ast.Pretty

type emit_mode =
  | Emit_ir
  | Emit_core

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
  eprintf "usage: haven [--emit-ir|--emit-core] [FILE]@.";
  exit 1

let main () =
  let rec parse_args index mode input =
    if index >= Array.length Sys.argv then (mode, input)
    else
      match Sys.argv.(index) with
      | "--emit-ir" -> parse_args (index + 1) Emit_ir input
      | "--emit-core" -> parse_args (index + 1) Emit_core input
      | filename when String.length filename > 0 && filename.[0] <> '-' -> (
          match input with
          | None -> parse_args (index + 1) mode (Some filename)
          | Some _ -> usage ())
      | _ -> usage ()
  in
  let emit_mode, input = parse_args 1 Emit_ir None in
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
  match emit_mode with
  | Emit_core ->
      Pretty.pp_core_program std_formatter pipeline.cleaned;
      pp_print_newline std_formatter ()
  | Emit_ir -> (
      try
        pp_print_string std_formatter (Llvm_ir.emit_ir_string pipeline);
        pp_print_newline std_formatter ()
      with
      | Llvm_ir.Error (Some loc, message) ->
          eprintf "%s: llvm: error: %s@." (string_of_loc loc) message;
          exit 1
      | Llvm_ir.Error (None, message) ->
          eprintf "haven: llvm: error: %s@." message;
          exit 1)

let () = main ()
