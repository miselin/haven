open Format

module Analysis = Haven.Ast.Analysis
module Convert = Haven.Ast.Convert
module Imports = Haven.Ast.Imports
module Llvm_ir = Haven.Ast.Llvm_ir
module Pretty = Haven.Ast.Pretty

type output_mode =
  | Emit_ir
  | Emit_core
  | Emit_bitcode
  | Emit_object
  | Emit_asm
  | Emit_binary

type config = {
  output_mode : output_mode;
  input : string option;
  output_file : string option;
  include_dirs : string list;
  debug_ast : bool;
  debug_ir : bool;
  debug_llvm : bool;
  verbose : bool;
  trace : bool;
  no_color : bool;
  no_preamble : bool;
  asan : bool;
  sysroot : string option;
  linker : string option;
  linker_options : string list;
  only_parse : bool;
  opt_level : Llvm_ir.opt_level;
}

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

let render_core_program program = asprintf "%a@." Pretty.pp_core_program program

let replace_extension path ext =
  let base =
    try Filename.chop_extension path with Invalid_argument _ -> path
  in
  match ext with "" -> base | _ -> base ^ "." ^ ext

let default_output_file (config : config) =
  match config.output_file with
  | Some path -> Some path
  | None -> (
      match config.output_mode with
      | Emit_ir | Emit_core -> None
      | Emit_bitcode ->
          Some
            (replace_extension
               (match config.input with Some path -> path | None -> "out")
               "bc")
      | Emit_object ->
          Some
            (replace_extension
               (match config.input with Some path -> path | None -> "out")
               "o")
      | Emit_asm ->
          Some
            (replace_extension
               (match config.input with Some path -> path | None -> "out")
               "s")
      | Emit_binary -> (
          match config.input with
          | Some path -> Some (replace_extension path "")
          | None -> Some "a.out"))

let write_text_file path text =
  let ch = open_out path in
  Fun.protect ~finally:(fun () -> close_out_noerr ch) (fun () -> output_string ch text)

let emit_text output_file text =
  match output_file with
  | Some path -> write_text_file path text
  | None ->
      pp_print_string std_formatter text;
      pp_print_flush std_formatter ()

let trace config fmt =
  if config.trace then kfprintf (fun _ -> pp_print_flush err_formatter ()) err_formatter fmt
  else ifprintf err_formatter fmt

let emit_debug_ast heading program =
  eprintf "%s@.%s" heading (render_core_program program)

let split_commas value =
  value
  |> String.split_on_char ','
  |> List.filter (fun token -> not (String.equal token ""))

let validate_include_dir path =
  let resolved =
    try Unix.realpath path
    with Unix.Unix_error (err, _, _) ->
      raise (Arg.Bad (Printf.sprintf "failed to resolve include path %s: %s" path
                        (Unix.error_message err)))
  in
  try
    match (Unix.stat resolved).Unix.st_kind with
    | Unix.S_DIR -> resolved
    | _ -> raise (Arg.Bad (Printf.sprintf "not a directory: %s" resolved))
  with Unix.Unix_error (err, _, _) ->
    raise (Arg.Bad (Printf.sprintf "failed to stat include path %s: %s" resolved
                      (Unix.error_message err)))

let parse_args argv =
  let output_mode = ref Emit_binary in
  let input = ref None in
  let output_file = ref None in
  let include_dirs_rev = ref [] in
  let debug_ast = ref false in
  let debug_ir = ref false in
  let debug_llvm = ref false in
  let verbose = ref false in
  let trace_logs = ref false in
  let no_color = ref false in
  let no_preamble = ref false in
  let asan = ref false in
  let sysroot = ref None in
  let linker = ref None in
  let linker_options_rev = ref [] in
  let only_parse = ref false in
  let opt_level = ref Llvm_ir.O0 in
  let set_output mode () = output_mode := mode in
  let set_opt level () = opt_level := level in
  let set_input filename =
    match !input with
    | None -> input := Some filename
    | Some _ -> raise (Arg.Bad "multiple input files specified")
  in
  let add_include_dir path =
    include_dirs_rev := validate_include_dir path :: !include_dirs_rev
  in
  let add_linker_option value =
    linker_options_rev :=
      List.rev_append (split_commas value) !linker_options_rev
  in
  let specs =
    Arg.align
      [
        ("-c", Arg.Unit (set_output Emit_object), " generate an object file, do not link");
        ("-o", Arg.String (fun value -> output_file := Some value), " <file> output file");
        ("-S", Arg.Unit (set_output Emit_asm), " output assembly");
        ("--O0", Arg.Unit (set_opt Llvm_ir.O0), " no optimizations");
        ("--O1", Arg.Unit (set_opt Llvm_ir.O1), " light optimizations");
        ("--O2", Arg.Unit (set_opt Llvm_ir.O2), " normal optimizations");
        ("--O3", Arg.Unit (set_opt Llvm_ir.O3), " aggressive optimizations");
        ("--Os", Arg.Unit (set_opt Llvm_ir.Os), " optimize for size");
        ("--debug-ast", Arg.Set debug_ast, " display the parsed AST");
        ("--debug-ir", Arg.Set debug_ir, " display the generated LLVM IR before emission");
        ("--debug-llvm", Arg.Set debug_llvm, " enable LLVM pass debug logging");
        ("--emit-ir", Arg.Unit (set_output Emit_ir), " emit textual IR");
        ("--emit-core", Arg.Unit (set_output Emit_core), " emit the cleaned core AST");
        ("--emit-bitcode", Arg.Unit (set_output Emit_bitcode), " emit LLVM bitcode");
        ("--verbose", Arg.Set verbose, " enable driver logging");
        ("--trace", Arg.Set trace_logs, " enable phase-level driver tracing");
        ("-I", Arg.String add_include_dir, " <path> add a path to the import search path");
        ("-isysroot", Arg.String (fun value -> sysroot := Some value), " <path> use <path> as the SDK/sysroot for cimport");
        ("--no-preamble", Arg.Set no_preamble, " do not emit the default preamble");
        ("--Xl", Arg.String add_linker_option, " <flag> pass <flag> to the linker");
        ("--ld", Arg.String (fun value -> linker := Some value), " <path> use <path> as the linker");
        ("--no-color", Arg.Set no_color, " disable color in diagnostics");
        ("--asan", Arg.Set asan, " accept the ASan flag and pass it through when linking");
        ("--only-parse", Arg.Set only_parse, " stop after parsing");
      ]
  in
  let usage = "usage: haven [options] [file]" in
  try
    let current = ref 0 in
    Arg.parse_argv ~current argv specs set_input usage;
    {
      output_mode = !output_mode;
      input = !input;
      output_file = !output_file;
      include_dirs = List.rev !include_dirs_rev;
      debug_ast = !debug_ast;
      debug_ir = !debug_ir;
      debug_llvm = !debug_llvm;
      verbose = !verbose;
      trace = !trace_logs;
      no_color = !no_color;
      no_preamble = !no_preamble;
      asan = !asan;
      sysroot = !sysroot;
      linker = !linker;
      linker_options = List.rev !linker_options_rev;
      only_parse = !only_parse;
      opt_level = !opt_level;
    }
  with
  | Arg.Bad msg ->
      eprintf "%s@.%s@." msg (Arg.usage_string specs usage);
      exit 1
  | Arg.Help msg ->
      print_string msg;
      exit 0

let string_of_output_mode = function
  | Emit_ir -> "ir"
  | Emit_core -> "core"
  | Emit_bitcode -> "bitcode"
  | Emit_object -> "object"
  | Emit_asm -> "assembly"
  | Emit_binary -> "binary"

let string_of_opt_level = function
  | Llvm_ir.O0 -> "O0"
  | Llvm_ir.O1 -> "O1"
  | Llvm_ir.O2 -> "O2"
  | Llvm_ir.O3 -> "O3"
  | Llvm_ir.Os -> "Os"

let summarize_config (config : config) =
  if config.verbose then (
    eprintf "input file: %s@."
      (match config.input with Some path -> path | None -> "<stdin>");
    (match default_output_file config with
    | Some path -> eprintf "output file: %s@." path
    | None -> ());
    eprintf "opt level: %s@." (string_of_opt_level config.opt_level);
    eprintf "output format: %s@." (string_of_output_mode config.output_mode);
    if config.no_color then eprintf "no color: true@.";
    if config.no_preamble then eprintf "no preamble: true@.";
    match config.sysroot with
    | Some path -> eprintf "sysroot: %s@." path
    | None -> ())

let default_linker_options () =
  if Haven.Ast.Platform_defaults_common.is_linux_host () then [ "-no-pie" ] else []

let run_linker config ~object_file ~output_file =
  let linker = match config.linker with Some path -> path | None -> "gcc" in
  let args =
    Array.of_list
      (linker :: "-o" :: output_file :: object_file
     :: default_linker_options ()
     :: (if config.asan then [ "-fsanitize=address" ] else [])
     @ config.linker_options)
  in
  trace config "phase: link %s@." (String.concat " " (Array.to_list args));
  let pid =
    Unix.create_process linker args Unix.stdin Unix.stdout Unix.stderr
  in
  match snd (Unix.waitpid [] pid) with
  | Unix.WEXITED 0 -> ()
  | Unix.WEXITED code -> exit code
  | Unix.WSIGNALED signal ->
      eprintf "haven: linker exited with signal %d@." signal;
      exit 1
  | Unix.WSTOPPED signal ->
      eprintf "haven: linker stopped with signal %d@." signal;
      exit 1

let with_temp_object_file f =
  let path, ch = Filename.open_temp_file "haven" ".o" in
  close_out_noerr ch;
  Fun.protect ~finally:(fun () -> if Sys.file_exists path then Sys.remove path) (fun () -> f path)

let codegen_options (config : config) =
  {
    Llvm_ir.opt_level = config.opt_level;
    debug_llvm = config.debug_llvm;
    emit_preamble = not config.no_preamble;
  }

let main () =
  let config = parse_args Sys.argv in
  summarize_config config;
  let parsed =
    try
      trace config "phase: parse@.";
      parse_input config.input
    with
    | Failure msg
    | Sys_error msg ->
        eprintf "haven: %s@." msg;
        exit 1
  in
  if config.only_parse then (
    if config.debug_ast then emit_debug_ast "== Parsed AST ==" (Convert.core_of_cst parsed);
    exit 0);
  trace config "phase: import expansion@.";
  let expanded = Imports.expand_cst ~search_dirs:config.include_dirs ?sysroot:config.sysroot parsed in
  if expanded.diagnostics <> [] then (
    List.iter print_diagnostic expanded.diagnostics;
    exit 1);
  trace config "phase: core lowering@.";
  let core = Convert.core_of_expanded_cst expanded.parsed in
  trace config "phase: analysis@.";
  let pipeline = Analysis.Pipeline.run_core core in
  let diagnostics = collect_pipeline_diagnostics pipeline in
  List.iter print_diagnostic diagnostics;
  if has_error diagnostics then (
    if config.debug_ast then emit_debug_ast "== Partial AST after failure ==" pipeline.cleaned;
    exit 1);
  if config.debug_ast then emit_debug_ast "== Pre-codegen AST ==" pipeline.cleaned;
  try
    match config.output_mode with
    | Emit_core ->
        emit_text config.output_file (render_core_program pipeline.cleaned)
    | Emit_ir | Emit_bitcode | Emit_object | Emit_asm | Emit_binary -> (
        trace config "phase: llvm lowering@.";
        let compiled = Llvm_ir.compile ~options:(codegen_options config) pipeline in
        if config.debug_ir then eprintf "%s@." (Llvm.string_of_llmodule compiled.llmodule);
        match config.output_mode with
        | Emit_ir ->
            emit_text config.output_file
              (Printf.sprintf "%s\n" (Llvm.string_of_llmodule compiled.llmodule))
        | Emit_bitcode -> (
            match default_output_file config with
            | Some path -> Llvm_ir.emit_bitcode_file compiled path
            | None -> assert false)
        | Emit_object -> (
            match default_output_file config with
            | Some path -> Llvm_ir.emit_object_file compiled path
            | None -> assert false)
        | Emit_asm -> (
            match default_output_file config with
            | Some path -> Llvm_ir.emit_assembly_file compiled path
            | None -> assert false)
        | Emit_binary -> (
            match default_output_file config with
            | Some output_file ->
                with_temp_object_file (fun object_file ->
                    Llvm_ir.emit_object_file compiled object_file;
                    run_linker config ~object_file ~output_file)
            | None -> assert false)
        | Emit_core -> assert false)
  with
  | Llvm_ir.Error (Some loc, message) ->
      eprintf "%s: llvm: error: %s@." (string_of_loc loc) message;
      exit 1
  | Llvm_ir.Error (None, message) ->
      eprintf "haven: llvm: error: %s@." message;
      exit 1

let () = main ()
