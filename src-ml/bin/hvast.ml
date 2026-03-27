let mode =
  if Array.length Sys.argv > 1 then Sys.argv.(1) else "core"

let () =
  let parsed = Haven.Parser.parse_stdin () in
  match mode with
  | "cst" -> Haven.Cst.Pretty.pp_program Format.std_formatter parsed.program
  | "surface" ->
      let surface = Haven.Ast.Convert.surface_of_cst parsed in
      Haven.Ast.Pretty.pp_surface_program Format.std_formatter surface
  | "core" ->
      let core = Haven.Ast.Convert.core_of_cst parsed in
      Haven.Ast.Pretty.pp_core_program Format.std_formatter core
  | other -> failwith ("unknown mode: " ^ other)
