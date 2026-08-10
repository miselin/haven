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

  assert_parse_ok "specialization hole parameter types"
    "fn vadd(fvec? a, mat? b) { a }";

  assert_parse_ok "nested pointer type"
    "pub fn follow(i8** cursor) -> i8* { load cursor }";

  assert_parse_ok "composed pointer and array postfixes"
    "pub fn first(i8*[2] values) -> i8* { values[0] }";

  assert_parse_ok "compile-time assert statement"
    "fn vadd(fvec? a, fvec? b) { @assert a.dim == b.dim, \"dims must match\"; a + b }";

  assert_parse_ok "multi-payload enum variants"
    "type Pair = enum { Both(i32, i32), Empty }; pub fn main() -> i32 { 0 }";

  assert_parse_ok "comma-separated match arms"
    "pub fn main() -> i32 { match 5 { 5 => 0, _ => 1 } }";

  assert_parse_error_contains "missing match comma" "parse error"
    "pub fn main() -> i32 { match 5 { 5 => 0 _ => 1 } }";

  assert_parse_ok "initializer without trailing comma"
    "type Thing = struct { i32 value; }; pub fn main() -> i32 { let Thing thing = { 1 }; thing.value }";

  assert_parse_ok "aggregate zero initializer"
    "pub state i32[4] values = zero;";

  let visibility_core =
    parse_to_core
      {|
fn file_helper() -> i32 { 0 }
pub(module) fn module_helper() -> i32 { 1 }
pub fn public_helper() -> i32 { 2 }

pub(module) {
  fn grouped_helper() -> i32 { 3 }
  pub fn grouped_public_helper() -> i32 { 4 }
  type Shared = i32;
  state i32 cache;
}
|}
  in
  let assert_function_visibility name expected =
    let fn_decl = find_named_function name visibility_core in
    assert_true
      (Printf.sprintf "%s should have %s visibility" name
         (Haven_core.Visibility.to_string expected))
      (fn_decl.value.visibility = expected)
  in
  assert_function_visibility "file_helper" Haven_core.Visibility.File;
  assert_function_visibility "module_helper" Haven_core.Visibility.Module;
  assert_function_visibility "public_helper" Haven_core.Visibility.External;
  assert_function_visibility "grouped_helper" Haven_core.Visibility.Module;
  assert_function_visibility "grouped_public_helper" Haven_core.Visibility.External;
  let find_decl name =
    List.find
      (fun (decl : Core.top_decl) ->
        match decl.value with
        | Core.TDecl ty -> String.equal ty.value.name.value name
        | Core.VDecl var -> String.equal var.value.name.value name
        | _ -> false)
      visibility_core.program.value.decls
  in
  (match (find_decl "Shared").value with
  | Core.TDecl ty ->
      assert_true "grouped type should have module visibility"
        (ty.value.visibility = Haven_core.Visibility.Module)
  | _ -> failwith "expected grouped type declaration");
  (match (find_decl "cache").value with
  | Core.VDecl var ->
      assert_true "grouped state should have module visibility"
        (var.value.visibility = Haven_core.Visibility.Module)
  | _ -> failwith "expected grouped state declaration");

  assert_parse_ok "extend lifecycle block"
    {|
type Buffer = struct {
  i8* ptr;
  i32 len;
};

extend Buffer with {
  construct(i32 len) {
    self->len = len;
  }

  destruct {
    self->len = 0;
  }
}
|};

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
    (string_contains parse_error "at preprocessed/input.hv:7:");

  let specialized_core = parse_to_core "fn vadd(fvec? a, mat? b) { a }" in
  let specialized_fn = find_named_function "vadd" specialized_core in
  (match specialized_fn.value.params.value.params with
  | [ vec_param; mat_param ] -> (
      match (vec_param.value.ty.value, mat_param.value.ty.value) with
      | Core.VecHoleType, Core.MatrixHoleType -> ()
      | _ -> failwith "expected specialization hole parameter types to survive into core AST")
  | _ -> failwith "expected vadd to have two parameters");
  assert_true "specialization function should keep omitted return type through core conversion"
    (specialized_fn.value.return_type = None);

  let associativity_core =
    parse_to_core "pub fn calculate() -> i32 { 15 * 100 / 400 }"
  in
  let calculate_fn = find_named_function "calculate" associativity_core in
  (match Option.bind calculate_fn.value.definition (fun body -> body.value.result) with
  | Some { value = Core.Binary divide; _ }
    when divide.value.op = Core.Divide -> (
      match divide.value.left.value with
      | Core.Binary multiply when multiply.value.op = Core.Multiply -> ()
      | _ -> failwith "expected multiplication to be the left operand of division")
  | _ -> failwith "expected same-tier arithmetic operators to associate left");

  let assert_core =
    parse_to_core
      "fn vadd(fvec? a, fvec? b) { @assert a.dim == b.dim, \"dims must match\"; a + b }"
  in
  let assert_fn = find_named_function "vadd" assert_core in
  (match
     Option.bind assert_fn.value.definition (fun body ->
         match body.value.statements with stmt :: _ -> Some stmt | [] -> None)
   with
  | Some { value = Core.CompileAssert _; _ } -> ()
  | _ -> failwith "expected compile assert to survive into core AST");

  let missing_return_error =
    try
      ignore (parse_to_core "fn add(i32 a, i32 b) { a + b }");
      failwith "expected omitted non-specialization return type to fail"
    with Failure msg -> msg
  in
  assert_true "omitted return type should require specialization parameters"
    (string_contains missing_return_error
       "only specialization functions may infer returns")
