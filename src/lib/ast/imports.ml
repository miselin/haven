open Analysis_types

module Cst = Haven_cst.Cst
module Parser = Haven_parser.Parser

type state = {
  seen : (string, unit) Hashtbl.t;
  active : (string, unit) Hashtbl.t;
  search_dirs : string list;
  sysroot : string option;
  mutable diagnostics_rev : diagnostic list;
}

let create_state ?(search_dirs = []) ?sysroot () =
  {
    seen = Hashtbl.create 32;
    active = Hashtbl.create 32;
    search_dirs;
    sysroot;
    diagnostics_rev = [];
  }

type result = {
  parsed : Cst.parsed_program;
  diagnostics : diagnostic list;
}

let add_diagnostic state loc message =
  state.diagnostics_rev <- { category = Import; level = Error; loc; message } :: state.diagnostics_rev

let program_file (program : Cst.program) = program.loc.start_pos.Lexing.pos_fname

let file_stats path =
  try Some (Unix.stat path) with Unix.Unix_error _ -> None

let is_regular_file path =
  match file_stats path with Some st -> st.st_kind = Unix.S_REG | None -> false

let is_directory path =
  match file_stats path with Some st -> st.st_kind = Unix.S_DIR | None -> false

let file_key path =
  let st = Unix.stat path in
  Printf.sprintf "%d:%d" st.st_dev st.st_ino

let current_dir file =
  let dir = Filename.dirname file in
  if dir = "" then "." else dir

let resolve_candidate path =
  if is_regular_file path then Some path
  else if is_directory path then
    let index = Filename.concat path "index.hv" in
    if is_regular_file index then Some index else None
  else if not (Filename.check_suffix path ".hv") then
    let with_suffix = path ^ ".hv" in
    if is_regular_file with_suffix then Some with_suffix else None
  else None

let resolve_import_path ~search_dirs ~current_file import_path =
  let resolve_from base =
    let candidate =
      if Filename.is_relative import_path then Filename.concat base import_path
      else import_path
    in
    resolve_candidate candidate
  in
  match resolve_from (current_dir current_file) with
  | Some path -> Some path
  | None ->
      let rec resolve_search_dirs = function
        | [] -> None
        | base :: rest -> (
            match resolve_from base with
            | Some path -> Some path
            | None -> resolve_search_dirs rest)
      in
      (match resolve_search_dirs search_dirs with
      | Some path -> Some path
      | None ->
          let cwd = Sys.getcwd () in
          if String.equal cwd (current_dir current_file) then None else resolve_from cwd)

let rec expand_program state (parsed : Cst.parsed_program) : Cst.parsed_program =
  let current_file = program_file parsed.program in
  let decls = List.concat_map (expand_top_decl state ~current_file) parsed.program.value.decls in
  { parsed with program = { parsed.program with value = { Cst.decls } } }

and expand_top_decl state ~current_file (decl : Cst.top_decl) : Cst.top_decl list =
  match decl.value with
  | Cst.Import import_path ->
      expand_import state ~current_file import_path.value import_path.loc
  | Cst.CImport import_path ->
      expand_cimport state ~current_file import_path.value import_path.loc
  | Cst.Foreign _ | Cst.FDecl _ | Cst.TDecl _ | Cst.VDecl _ ->
      [ decl ]

and expand_import state ~current_file import_path loc =
  match resolve_import_path ~search_dirs:state.search_dirs ~current_file import_path with
  | None ->
      add_diagnostic state loc
        (Printf.sprintf "failed to resolve Haven import %S from %s" import_path current_file);
      []
  | Some resolved ->
      let key = file_key resolved in
      if Hashtbl.mem state.seen key || Hashtbl.mem state.active key then []
      else (
        Hashtbl.add state.active key ();
        Fun.protect
          ~finally:(fun () -> Hashtbl.remove state.active key)
          (fun () ->
            try
              let imported = Parser.parse_file resolved in
              let expanded = expand_program state imported in
              Hashtbl.add state.seen key ();
              expanded.program.value.decls
            with exn ->
              add_diagnostic state loc
                (Printf.sprintf "failed to load Haven import %S from %s: %s"
                   import_path current_file (Printexc.to_string exn));
              []))

and expand_cimport state ~current_file import_path loc =
  let expanded =
    Cimport.expand_header ~search_dirs:state.search_dirs ~sysroot:state.sysroot ~current_file
      ~header:import_path ~loc
  in
  state.diagnostics_rev <-
    List.rev_append (List.rev expanded.diagnostics) state.diagnostics_rev;
  expanded.decls

let expand_cst ?(search_dirs = []) ?sysroot parsed =
  let defaults = Platform_defaults.resolve ~search_dirs ?sysroot () in
  let state = create_state ~search_dirs:defaults.search_dirs ?sysroot:defaults.sysroot () in
  let parsed = expand_program state parsed in
  { parsed; diagnostics = List.rev state.diagnostics_rev }
