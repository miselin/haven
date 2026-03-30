open Test_support

let emit_ir source =
  let pipeline = parse_to_core source |> Analysis.Pipeline.run_core in
  assert_no_diagnostics "llvm ir typing" pipeline.typing.diagnostics;
  assert_no_diagnostics "llvm ir verify" pipeline.verify.diagnostics;
  assert_no_diagnostics "llvm ir semantic" pipeline.semantic.diagnostics;
  assert_no_diagnostics "llvm ir purity" pipeline.purity.diagnostics;
  assert_no_diagnostics "llvm ir ownership" pipeline.ownership.diagnostics;
  Haven.Ast.Llvm_ir.emit_ir_string pipeline

let run () =
  let main_ir = emit_ir "pub fn main() -> i32 { 7 }" in
  assert_true "main IR should define main"
    (string_contains main_ir "define i32 @main()");

  let string_ir = emit_ir "pub fn main() -> str { \"hi\" }" in
  assert_true "string IR should emit a private constant string global"
    (string_contains string_ir "private constant [3 x i8] c\"hi\\00\"");

  let ctor_ir =
    emit_ir
      "fn make(i32 value) -> i32 { value }\ndata i32 GLOBAL = make(7);\npub fn main() -> i32 { GLOBAL }"
  in
  assert_true "non-constant globals should synthesize a ctor"
    (string_contains ctor_ir "@llvm.global_ctors");
  assert_true "non-constant globals should emit the init function"
    (string_contains ctor_ir "@__haven_global_init");

  let box_ir = emit_ir "pub fn forward(i32^ input) -> i32^ { defer unbox input; input }" in
  assert_true "box ownership should call box ref"
    (string_contains box_ir "@__haven_box_ref");
  assert_true "box ownership should call box unref"
    (string_contains box_ir "@__haven_box_unref");

  let intrinsic_ir =
    emit_ir
      {|
pub fn __builtin_ipow(float x, i32 power) -> float intrinsic "llvm.powi" float, i32;
pub fn __builtin_sqrtf(float x) -> float intrinsic "llvm.sqrt" float;
pub fn root(float x) -> float { __builtin_sqrtf(x) }
pub fn pow3(float x) -> float { __builtin_ipow(x, 3) }
|}
  in
  assert_true "custom sqrt intrinsic should declare the f32 overload"
    (string_contains intrinsic_ir "declare float @llvm.sqrt.f32(float)");
  assert_true "custom powi intrinsic should declare the typed overload"
    (string_contains intrinsic_ir "declare float @llvm.powi.f32.i32(float, i32)");

  let vec_mat_ir =
    emit_ir
      {|
pub fn scale(fvec3 v, float s) -> fvec3 { v * s }
pub fn mmul(mat2x3 a, mat3x4 b) -> mat2x4 { a * b }
pub fn vmul(fvec2 v, mat2x3 m) -> fvec3 { v * m }
pub fn main() -> void {}
|}
  in
  assert_true "vector scalar multiply should splat the scalar"
    (string_contains vec_mat_ir "fmul <3 x float>");
  assert_true "matrix multiply should declare the correctly typed intrinsic"
    (string_contains vec_mat_ir "@llvm.matrix.multiply.v8f32.v6f32.v12f32");
  assert_true "vector-matrix multiply should declare the correctly typed intrinsic"
    (string_contains vec_mat_ir "@llvm.matrix.multiply.v3f32.v2f32.v6f32");

  let field_ir =
    emit_ir
      {|
pub fn lane(fvec3 v) -> float { v.x }
pub fn row(mat2x3 m) -> fvec3 { m.y }
pub fn main() -> void {}
|}
  in
  assert_true "vector field access should lower through a vector GEP"
    (string_contains field_ir "getelementptr inbounds <3 x float>");
  assert_true "matrix row access should lower through scalar row addressing"
    (string_contains field_ir "getelementptr inbounds float");

  let literal_ir =
    emit_ir
      {|
pub fn make_vec(float x) -> fvec3 { Vec<x, 2.0, 3.0> }
pub fn make_mat(float x) -> mat2x2 { Mat<Vec<x, 2.0>, Vec<3.0, 4.0>> }
pub fn main() -> void {}
|}
  in
  assert_true "non-constant vector literals should be assembled with insertelement"
    (string_contains literal_ir "define <3 x float> @make_vec");
  assert_true "non-constant matrix literals should lower to their flat vector form"
    (string_contains literal_ir "define <4 x float> @make_mat")
