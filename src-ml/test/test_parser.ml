open Test_support

let run () =
  assert_parse_ok "vararg parameter list"
    "pub fn printf(str fmt, *) -> i32;";

  assert_parse_ok "matrix literal without trailing comma"
    "pub fn main() -> i32 { let x = Mat<Vec<1.0, 0.0>, Vec<0.0, 1.0>>; 0 }";

  assert_parse_ok "comma-separated match arms"
    "pub fn main() -> i32 { match 5 { 5 => 0, _ => 1 } }";

  assert_parse_error_contains "missing match comma" "parse error"
    "pub fn main() -> i32 { match 5 { 5 => 0 _ => 1 } }";

  assert_parse_ok "initializer without trailing comma"
    "type Thing = struct { i32 value; }; pub fn main() -> i32 { let Thing thing = { 1 }; thing.value }";

  assert_parse_error_contains "initializer trailing comma" "parse error"
    "type Thing = struct { i32 value; }; pub fn main() -> i32 { let Thing thing = { 1, }; thing.value }";

  assert_parse_error_contains "legacy store statement removed" "parse error"
    "pub impure fn main() -> void { let mut i32 x = 0; store ref x as<i32>(1); }"
