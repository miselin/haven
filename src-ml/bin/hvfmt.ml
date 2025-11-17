let () =
  let parsed = Haven.Parser.parse_stdin () in
  Haven_cst.Emit.emit_program Format.std_formatter parsed
