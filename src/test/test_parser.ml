open Test_support

let run () =
  (* WITH is a new follower of the existing BOX identifier value/type boundary.
     Keep the canonical value parse; lowering resolves registered type names. *)
  let box_boundary_source name prefix =
    prefix
    ^ "fn boundary(fvec2 vector) -> float = fold each value of unbox box "
    ^ name ^ " with acc = 0.0 { acc + value };"
  in
  let check_box_boundary label source name =
    let parsed = Haven.Parser.parse_string source in
    let fn =
      List.find_map
        (fun (decl : Haven_cst.Cst.top_decl) ->
          match decl.value with
          | Haven_cst.Cst.FDecl fn when fn.value.name.value = "boundary" ->
              Some fn
          | _ -> None)
        parsed.program.value.decls
      |> Option.get
    in
    (match (Option.get fn.value.definition).value.items with
    | [
     {
       value =
         Haven_cst.Cst.BlockExpression { value = Haven_cst.Cst.Fold fold; _ };
       _;
     };
    ] -> (
        match fold.value.source.value with
        | Haven_cst.Cst.Unbox
            {
              value =
                Haven_cst.Cst.BoxExpr { value = Haven_cst.Cst.Identifier id; _ };
              _;
            } ->
            assert_true
              (label ^ " uses the canonical value parse before WITH")
              (id.value = name)
        | _ -> failwith (label ^ " unexpected boxing parse"))
    | _ -> failwith "expected fold expression body");
    let format text =
      Format.asprintf "%a" Haven_cst.Emit.emit_program
        (Haven.Parser.parse_string text)
    in
    let formatted = format source in
    assert_true
      (label ^ " round-trip preserves box source boundary")
      (string_contains formatted ("unbox box " ^ name ^ " with acc"));
    assert_true
      (label ^ " formatting is idempotent")
      (format formatted = formatted)
  in
  check_box_boundary "boxed variable fold source"
    (box_boundary_source "vector" "")
    "vector";
  check_box_boundary "boxed named type fold source"
    (box_boundary_source "Vector" "type Vector = fvec2;\n")
    "Vector";
  assert_parse_ok "fold vector, nested matrix, typed array and empty source"
    {|fn sums(fvec? v) -> float = fold each value of v with acc = 0.0 { acc + value };
    fn total(mat? m) -> float = fold each row of m with acc = 0.0 {
      acc + (fold each cell of row with subtotal = 0.0 { subtotal + cell })
    };
    fn integers(i32[0] a) -> i32 = fold each value of a with i32 acc = 7 { acc + value };|};
  List.iter
    (fun source ->
      assert_parse_error_contains "invalid fold syntax" "parse error" source)
    [
      "fn f(fvec3 v) -> float = fold value of v with acc = 0.0 { acc + value };";
      "fn f(fvec3 v) -> float = fold each value v with acc = 0.0 { acc + value \
       };";
      "fn f(fvec3 v) -> float = fold each value of v with acc = { acc + value \
       };";
      "fn f(fvec3 v) -> float = fold each value of v with acc = 0.0, other = \
       1.0 { acc + value };";
      "fn f(fvec3 v) -> float = fold each value of v with mut acc = 0.0 { acc \
       + value };";
      "fn f(fvec3 v) -> float = fold each value of v with acc float = 0.0 { \
       acc + value };";
    ];
  let fold_source =
    "// fold source\n\
     fn sum(fvec? v) -> float = fold each value of v with float acc = 0.0 { /* \
     step */ acc + value };"
  in
  let fold_format source =
    Format.asprintf "%a" Haven_cst.Emit.emit_program
      (Haven.Parser.parse_string source)
  in
  let formatted_fold = fold_format fold_source in
  assert_true "fold formatting retains grammar and body comment"
    (string_contains formatted_fold "fold each value of v with float acc = 0.0"
    && string_contains formatted_fold "/* step */");
  assert_true "fold formatter is idempotent"
    (fold_format formatted_fold = formatted_fold);
  assert_parse_ok
    "expression-bodied ordinary, generic, aggregate and void functions"
    {|fn add(i32 a, i32 b) -> i32 = a + b;
      fn scaled(fvec? v) = v * 2.0;
      type Pair = struct { i32 x; i32 y; };
      fn pair(i32 x) -> Pair = { x, 2 };
      fn empty() -> void = {};
      fn branch(i32 x) -> i32 = if x { 1 } else { 2 };|};
  assert_parse_ok "value iteration and optional ordinal"
    {|fn main() -> void {
      iter each value of Vec<1.0, 2.0> {};
      iter each row of Mat<Vec<1.0, 2.0>, Vec<3.0, 4.0>> indexed by index {
        iter each value of row indexed by column {};
      };
    }|};
  assert_parse_ok "sentence range iteration retains optional step expressions"
    {|fn main(i32 step) -> void {
        iter each i of 0:3 {};
        iter each i of 3:0:-1 {};
        iter each i of 3:0:step {};
      }|};
  assert_parse_ok "indexed and by remain ordinary identifiers"
    {|fn walk(fvec2 by) -> void {
        let indexed = by;
        iter indexed by {};
        iter each indexed of by indexed /* clause */ by ordinal {};
      }|};
  List.iter
    (fun source ->
      assert_parse_error_contains "sentence index clause has explicit words"
        "parse error" source)
    [
      "fn f(fvec2 v) -> void { iter each value of v, i {}; }";
      "fn f(fvec2 v) -> void { iter each value, i of v {}; }";
      "fn f(fvec2 v) -> void { iter each value of v ordinal by i {}; }";
      "fn f(fvec2 v) -> void { iter each value of v indexed i {}; }";
      "fn f(fvec2 v) -> void { iter each value of v indexed by {}; }";
    ];
  let format source =
    Format.asprintf "%a" Haven_cst.Emit.emit_program
      (Haven.Parser.parse_string source)
  in
  let fill_map_source =
    {|fn f(mat2x3 m) -> mat2x3 {
      let fvec3 v = fill (1.0 + 2.0);
      map each row of m indexed by r { map each x of row indexed by c { x + as<float>(r + c) } }
    }|}
  in
  assert_parse_ok "contextual fill and nested indexed maps" fill_map_source;
  let fill_map_formatted = format fill_map_source in
  assert_true "fill/map formatter round-trip is idempotent"
    (format fill_map_formatted = fill_map_formatted);
  assert_true "fill operand retains unary parentheses"
    (string_contains fill_map_formatted "fill (1.0 + 2.0)");
  List.iter
    (fun source ->
      assert_parse_error_contains "map requires sentence binders" "parse error"
        source)
    [
      "fn f(fvec2 v) -> fvec2 = map v x { x };";
      "fn f(fvec2 v) -> fvec2 = map each x of v, i { x };";
      "fn f(fvec2 v) -> fvec2 = map each x of v indexed i { x };";
    ];
  assert_parse_ok "shifts inside nested aggregate lanes remain shifts"
    "fn example(i32 shift) -> mat1x2 = Mat<Vec<as<float>(8 >> shift), 2.0>>;";
  assert_parse_error_contains "expression body needs a terminating semicolon"
    "parse error" "fn add(i32 a) -> i32 = a + 1";
  assert_parse_error_contains "expression body cannot be empty" "parse error"
    "fn bad() -> i32 = ;";
  assert_parse_error_contains "value iteration needs a value binding"
    "parse error" "fn main() -> void { iter each of Vec<1.0> {}; }";
  assert_parse_error_contains "value iteration has at most one ordinal"
    "parse error"
    "fn main() -> void { iter each value of Vec<1.0> indexed by index, other \
     {}; }";
  assert_parse_error_contains
    "value iteration does not implicitly bind references" "parse error"
    "fn main() -> void { iter each ref value of Vec<1.0> {}; }";
  let expression_source = "fn add(i32 a) -> i32 = a + 1;" in
  let expression_core = parse_to_core expression_source in
  let expression_fn = find_named_function "add" expression_core in
  (match expression_fn.value.definition with
  | Some { value = { statements = []; result = Some result; _ }; _ } ->
      assert_true "expression result keeps its original source span"
        (result.loc.start_pos.pos_cnum = String.index expression_source '=' + 2
        && result.loc.end_pos.pos_cnum = String.index expression_source ';')
  | _ -> failwith "expression body must lower to the ordinary block result");
  let formatted_source =
    {|// strict function
fn add(i32 x) -> i32 = x + 1;
fn walk(fvec? v) -> float { let mut total = 0.0; iter each value of v indexed by index { total = total + value; }; total }
|}
  in
  let format source =
    let parsed = Haven.Parser.parse_string source in
    Format.asprintf "%a" Haven_cst.Emit.emit_program parsed
  in
  let formatted = format formatted_source in
  assert_true "formatter retains expression-body syntax"
    (string_contains formatted " = x + 1;");
  assert_true "formatter retains value iteration and ordinal"
    (string_contains formatted "iter each value of v indexed by index");
  assert_true "formatter retains source comments"
    (string_contains formatted "// strict function");
  assert_true "new syntax formatter round-trip is idempotent"
    (format formatted = formatted);

  let legacy =
    {|fn loops(fvec2 v) -> void {
      iter 3:0:-1 frame {};
      iter v value, index {};
      iter Mat<Vec<1.0, 2.0>, Vec<3.0, 4.0>> row {};
    }|}
  in
  let canonical = format legacy in
  assert_true "formatter migrates both legacy iteration headers"
    (string_contains canonical "iter each frame of 3:0:-1"
    && string_contains canonical "iter each value of v indexed by index");
  let cst source =
    Format.asprintf "%a" Haven_cst.Pretty.pp_program
      (Haven.Parser.parse_string source).program
  in
  assert_true "canonical and legacy spellings produce the same CST"
    (cst legacy = cst canonical);
  assert_true "legacy iteration formats idempotently to sentence form"
    (format canonical = canonical);

  assert_parse_ok "vararg parameter list" "pub fn printf(str fmt, *) -> i32;";

  assert_parse_ok "ignores preprocessor directives"
    "#if 0\n#line 1 \"orig.hv\"\npub fn main() -> i32 { 0 }";

  assert_parse_ok "matrix literal without trailing comma"
    "pub fn main() -> i32 { let x = Mat<Vec<1.0, 0.0>, Vec<0.0, 1.0>>; 0 }";

  assert_parse_ok "matrix literal accepts vector expressions"
    "pub fn main() -> i32 { let v = Vec<3.0, 4.0>; let x = Mat<Vec<1.0, 0.0>, \
     v>; 0 }";

  assert_parse_ok "specialization hole parameter types"
    "fn vadd(fvec? a, mat? b) { a }";

  assert_parse_ok "nested pointer type"
    "pub fn follow(i8** cursor) -> i8* { load cursor }";

  assert_parse_ok "composed pointer and array postfixes"
    "pub fn first(i8*[2] values) -> i8* { values[0] }";

  assert_parse_ok "compile-time assert statement"
    "fn vadd(fvec? a, fvec? b) { @assert a.dim == b.dim, \"dims must match\"; \
     a + b }";

  assert_parse_ok "multi-payload enum variants"
    "type Pair = enum { Both(i32, i32), Empty }; pub fn main() -> i32 { 0 }";

  assert_parse_ok "comma-separated match arms"
    "pub fn main() -> i32 { match 5 { 5 => 0, _ => 1 } }";

  assert_parse_error_contains "missing match comma" "parse error"
    "pub fn main() -> i32 { match 5 { 5 => 0 _ => 1 } }";

  assert_parse_ok "initializer without trailing comma"
    "type Thing = struct { i32 value; }; pub fn main() -> i32 { let Thing \
     thing = { 1 }; thing.value }";

  assert_parse_ok "aggregate zero initializer" "pub state i32[4] values = zero;";

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
  assert_function_visibility "grouped_public_helper"
    Haven_core.Visibility.External;
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
    "type Thing = struct { i32 value; }; pub fn main() -> i32 { let Thing \
     thing = { 1, }; thing.value }";

  assert_parse_error_contains "legacy store statement removed" "parse error"
    "pub impure fn main() -> void { let mut i32 x = 0; store ref x as<i32>(1); \
     }";

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
      | _ ->
          failwith
            "expected specialization hole parameter types to survive into core \
             AST")
  | _ -> failwith "expected vadd to have two parameters");
  assert_true
    "specialization function should keep omitted return type through core \
     conversion"
    (specialized_fn.value.return_type = None);

  let associativity_core =
    parse_to_core "pub fn calculate() -> i32 { 15 * 100 / 400 }"
  in
  let calculate_fn = find_named_function "calculate" associativity_core in
  (match
     Option.bind calculate_fn.value.definition (fun body -> body.value.result)
   with
  | Some { value = Core.Binary divide; _ } when divide.value.op = Core.Divide
    -> (
      match divide.value.left.value with
      | Core.Binary multiply when multiply.value.op = Core.Multiply -> ()
      | _ ->
          failwith "expected multiplication to be the left operand of division")
  | _ -> failwith "expected same-tier arithmetic operators to associate left");

  let assert_core =
    parse_to_core
      "fn vadd(fvec? a, fvec? b) { @assert a.dim == b.dim, \"dims must \
       match\"; a + b }"
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
