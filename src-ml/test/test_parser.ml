open Test_support

let run () =
  assert_parse_ok "vararg parameter list"
    "pub fn printf(str fmt, *) -> i32;";

  assert_parse_ok "ignores preprocessor directives"
    "#if 0\n#line 1 \"orig.hv\"\npub fn main() -> i32 { 0 }";

  assert_parse_ok "matrix literal without trailing comma"
    "pub fn main() -> i32 { let x = Mat<Vec<1.0, 0.0>, Vec<0.0, 1.0>>; 0 }";

  assert_parse_ok "matrix literal accepts vector expressions"
    "pub fn main() -> i32 { let v = Vec<3.0, 4.0>; let x = Mat<Vec<1.0, 0.0>, v>; 0 }";

  assert_parse_ok "multi-payload enum variants"
    "type Pair = enum { Both(i32, i32), Empty }; pub fn main() -> i32 { 0 }";

  assert_parse_ok "comma-separated match arms"
    "pub fn main() -> i32 { match 5 { 5 => 0, _ => 1 } }";

  assert_parse_error_contains "missing match comma" "parse error"
    "pub fn main() -> i32 { match 5 { 5 => 0 _ => 1 } }";

  assert_parse_ok "initializer without trailing comma"
    "type Thing = struct { i32 value; }; pub fn main() -> i32 { let Thing thing = { 1 }; thing.value }";

  assert_parse_error_contains "initializer trailing comma" "parse error"
    "type Thing = struct { i32 value; }; pub fn main() -> i32 { let Thing thing = { 1, }; thing.value }";

  assert_parse_error_contains "legacy store statement removed" "parse error"
    "pub impure fn main() -> void { let mut i32 x = 0; store ref x as<i32>(1); }";

  let remapped =
    Haven.Parser.parse_string
      "# 41 \"generated/original.hv\" 1 3\npub fn main() -> i32 { 0 }"
  in
  let decl =
    match remapped.program.value.decls with
    | decl :: _ -> decl
    | [] -> failwith "expected remapped program to contain a declaration"
  in
  assert_true "linemarker should update declaration filename"
    (String.equal decl.loc.start_pos.Lexing.pos_fname "generated/original.hv");
  assert_true "linemarker should update declaration line"
    (decl.loc.start_pos.Lexing.pos_lnum = 41);

  let parse_error =
    try
      ignore
        (Haven.Parser.parse_string
           "#line 7 \"preprocessed/input.hv\"\npub fn main( -> i32 { 0 }");
      failwith "expected preprocessed parse failure"
    with Failure msg -> msg
  in
  assert_true "parse errors should report remapped preprocessor filename"
    (string_contains parse_error "preprocessed/input.hv:7:");
  assert_true "parse errors should report remapped preprocessor line"
    (string_contains parse_error "at preprocessed/input.hv:7:")
