module Cst = Haven_cst.Cst
module Parser = Haven_parser.Parser

type state = {
  seen : (string, unit) Hashtbl.t;
  active : (string, unit) Hashtbl.t;
}

let create_state () =
  { seen = Hashtbl.create 32; active = Hashtbl.create 32 }

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

let resolve_import_path ~current_file import_path =
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
      let cwd = Sys.getcwd () in
      if String.equal cwd (current_dir current_file) then None else resolve_from cwd

let rec expand_program state (parsed : Cst.parsed_program) : Cst.parsed_program =
  let current_file = program_file parsed.program in
  let decls = List.concat_map (expand_top_decl state ~current_file) parsed.program.value.decls in
  { parsed with program = { parsed.program with value = { Cst.decls } } }

and expand_top_decl state ~current_file (decl : Cst.top_decl) : Cst.top_decl list =
  match decl.value with
  | Cst.Import import_path ->
      expand_import state ~current_file import_path.value import_path.loc
  | Cst.CImport _ | Cst.Foreign _ | Cst.FDecl _ | Cst.TDecl _ | Cst.VDecl _ ->
      [ decl ]

and expand_import state ~current_file import_path _loc =
  match resolve_import_path ~current_file import_path with
  | None ->
      failwith
        (Printf.sprintf "failed to resolve Haven import %S from %s" import_path current_file)
  | Some resolved ->
      let key = file_key resolved in
      if Hashtbl.mem state.seen key || Hashtbl.mem state.active key then []
      else (
        Hashtbl.add state.active key ();
        Fun.protect
          ~finally:(fun () -> Hashtbl.remove state.active key)
          (fun () ->
            let imported = Parser.parse_file resolved in
            let expanded = expand_program state imported in
            Hashtbl.add state.seen key ();
            expanded.program.value.decls))

let expand_cst parsed =
  let state = create_state () in
  expand_program state parsed
