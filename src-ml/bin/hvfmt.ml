
let () =
  let program = Haven.Parser.parse_stdin in
  Haven_cst.Pretty.pp_program Format.std_formatter program
