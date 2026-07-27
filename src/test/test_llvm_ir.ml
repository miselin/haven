open Test_support

let emit_ir source =
  let pipeline = parse_to_core source |> Analysis.Pipeline.run_core in
  assert_no_diagnostics "llvm ir typing" pipeline.typing.diagnostics;
  assert_no_diagnostics "llvm ir verify" pipeline.verify.diagnostics;
  assert_no_diagnostics "llvm ir semantic" pipeline.semantic.diagnostics;
  assert_no_diagnostics "llvm ir asserts" pipeline.asserts.diagnostics;
  assert_no_diagnostics "llvm ir purity" pipeline.purity.diagnostics;
  assert_no_diagnostics "llvm ir ownership" pipeline.ownership.diagnostics;
  Haven.Ast.Llvm_ir.emit_ir_string pipeline

let next_loc =
  let counter = ref 0 in
  fun () ->
    let start_cnum = !counter * 2 in
    incr counter;
    let start_pos : Lexing.position =
      {
        pos_fname = "manual-test.hv";
        pos_lnum = 1;
        pos_bol = 0;
        pos_cnum = start_cnum;
      }
    in
    let end_pos = { start_pos with pos_cnum = start_cnum + 1 } in
    { Haven_core.Loc.start_pos; end_pos }

let node value = { Core.value; loc = next_loc () }
let ident value = node value
let ty_i32 = node (Core.NumericType { Haven_token.Token.signedness = Signed; bits = 32 })
let ty_i8 = node (Core.NumericType { Haven_token.Token.signedness = Signed; bits = 8 })
let ty_i8_ptr = node (Core.PointerType ty_i8)
let ty_void = node Core.VoidType
let ty_buffer = node (Core.CustomType { name = ident "Buffer" })
let ty_buffer_cell = node (Core.CellType ty_buffer)
let int_lit value = node (Core.Literal (node (Core.Integer value)))
let nil_expr = node Core.Nil
let box_buffer_expr = node (Core.BoxType ty_buffer)

let field_expr target ~arrow name =
  node
    (Core.Field
       (node
          {
            Core.target = target;
            arrow;
            field = ident name;
          }))

let assign_stmt target value =
  node
    (Core.Expression
       (node
          (Core.Assign
             (node
                {
                  Core.target = target;
                  value;
                }))))

let let_stmt ~mut name init_expr =
  node
    (Core.Let
       (node
          {
            Core.mut = mut;
            ty = None;
            name = ident name;
            init_expr;
          }))

let block ?result statements = node { Core.statements = statements; result }

let fn_decl ?(public = false) ?(impure = false) ?(definition = None) ?(params = [])
    ?(return_type = Some ty_void) name =
  node
    {
      Core.public = public;
      impure;
      name = ident name;
      definition;
      intrinsic = None;
      params = node { Core.params = params; vararg = false };
      return_type;
      vararg = false;
    }

let param ty name = node { Core.name = ident name; ty }
let struct_field ty name = node ({ Core.name = ident name; ty } : Core.struct_field_desc)

let emit_core_ir program =
  let pipeline = Analysis.Pipeline.run_core { Core.program = program } in
  let fail_if_diagnostics label diagnostics =
    if diagnostics <> [] then
      let messages =
        String.concat " | "
          (List.map (fun (diag : Analysis.diagnostic) -> diag.message) diagnostics)
      in
      failwith (label ^ ": " ^ messages)
  in
  fail_if_diagnostics "core llvm ir typing" pipeline.typing.diagnostics;
  fail_if_diagnostics "core llvm ir verify" pipeline.verify.diagnostics;
  fail_if_diagnostics "core llvm ir semantic" pipeline.semantic.diagnostics;
  fail_if_diagnostics "core llvm ir purity" pipeline.purity.diagnostics;
  fail_if_diagnostics "core llvm ir ownership" pipeline.ownership.diagnostics;
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
  assert_true "startup-initialized data must remain writable in LLVM"
    (string_contains ctor_ir "@GLOBAL = internal global i32 0");

  let external_ir =
    emit_ir
      "pub state i32 supplied_elsewhere;\npub fn read() -> i32 { supplied_elsewhere }"
  in
  assert_true "initializer-less public state should remain an external declaration"
    (string_contains external_ir "@supplied_elsewhere = external global i32");
  assert_true "external declarations should not synthesize startup initialization"
    (not (string_contains external_ir "@__haven_global_init"));

  let constant_cast_ir =
    emit_ir
      "data u32 ZERO = as<u32>(0);\npub fn main() -> u32 { ZERO }"
  in
  assert_true "constant casts should match the declared global type"
    (string_contains constant_cast_ir "@ZERO = internal constant i32 0");

  let short_circuit_ir =
    emit_ir
      {|
state i32 calls = 0;
impure fn rhs() -> i32 {
  calls = calls + 1;
  1
}
pub impure fn test() -> i1 { 0 && rhs() }
|}
  in
  assert_true "logical RHS should only be emitted in the conditional block"
    (count_occurrences short_circuit_ir "call i32 @rhs()" = 1);

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

  let multi_payload_enum_ir =
    emit_ir
      {|
type Pair = enum {
  Both(i32, i32),
  Empty
};

pub fn sum(Pair value) -> i32 {
  match value {
    Both(left, right) => left + right,
    _ => 0
  }
}
|}
  in
  assert_true "multi-payload enums should lower payload storage as a struct"
    (string_contains multi_payload_enum_ir "{ i32, i32 }");

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
    (string_contains literal_ir "define <4 x float> @make_mat");

  let specialization_ir =
    emit_ir
      {|
fn vadd(fvec? a, fvec? b) { a + b }
pub fn main() -> fvec3 { vadd(Vec<1.0, 2.0, 3.0>, Vec<4.0, 5.0, 6.0>) }
|}
  in
  assert_true "specialization should clone concrete vector variants before LLVM"
    (string_contains specialization_ir "@vadd__spec__fvec3__fvec3");
  assert_true "specialized vector addition should lower with concrete vector ops"
    (string_contains specialization_ir "fadd <3 x float>");

  let shape_property_ir =
    emit_ir
      {|
fn width(mat? m) { m.cols }
pub fn main() -> u32 { width(Mat<Vec<1.0, 2.0>, Vec<3.0, 4.0>>) }
|}
  in
  assert_true "shape property specialization should lower concrete matrix helpers"
    (string_contains shape_property_ir "@width__spec__mat2x2");
  assert_true "shape properties should lower as plain integer constants"
    (string_contains shape_property_ir "store i32 2");

  let get_mat_row_ir =
    emit_ir
      {|
fn get_mat_row(mat? m, u32 row) { m[row] }
pub fn main() -> fvec3 {
  get_mat_row(Mat<Vec<1.0, 2.0, 3.0>, Vec<4.0, 5.0, 6.0>>, 1)
}
|}
  in
  assert_true "get_mat_row should clone a concrete helper before LLVM"
    (string_contains get_mat_row_ir "@get_mat_row__spec__mat2x3");
  assert_true "specialized matrix row access should still lower through row addressing"
    (string_contains get_mat_row_ir "getelementptr inbounds float");

  let compile_assert_ir =
    emit_ir
      {|
fn mat_width_eq(mat? a, mat? b) {
  @assert a.cols == b.cols, "matrix widths must match";
  a.cols
}

pub fn main() -> u32 {
  mat_width_eq(Mat<Vec<1.0, 2.0>, Vec<3.0, 4.0>>, Mat<Vec<5.0, 6.0>, Vec<7.0, 8.0>>)
}
|}
  in
  assert_true "successful compile asserts should not reach LLVM"
    (not (string_contains compile_assert_ir "compile-time assert"));
  assert_true "compile assert specializations should still lower normally"
    (string_contains compile_assert_ir "@mat_width_eq__spec__mat2x2__mat2x2");

  let surface_lifecycle_ir =
    emit_ir
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
pub impure fn main() -> i32 {
  let mut boxed = box Buffer(7);
  boxed = nil;
  0
}
|}
  in
  assert_true "surface lifecycle lowering should emit the synthesized constructor"
    (string_contains surface_lifecycle_ir "define internal void @__haven_construct_Buffer");
  assert_true "surface lifecycle lowering should emit the synthesized destructor"
    (string_contains surface_lifecycle_ir "define internal void @__haven_destruct_Buffer");
  assert_true "surface lifecycle lowering should call the synthesized constructor"
    (string_contains surface_lifecycle_ir "call void @__haven_construct_Buffer");
  assert_true "surface lifecycle lowering should call the synthesized destructor"
    (string_contains surface_lifecycle_ir "call void @__haven_destruct_Buffer");

  let ctor_decl =
    fn_decl "buffer_construct"
      ~params:[ param ty_buffer_cell "self" ]
      ~definition:(Some (block []))
  in
  let dtor_decl =
    fn_decl "buffer_destruct"
      ~params:[ param ty_buffer_cell "self" ]
      ~definition:(Some (block []))
  in
  let buffer_type =
    node
      (Core.TDecl
         (node
            {
              Core.name = ident "Buffer";
              data =
                Core.TypeDeclStruct
                  (node
                     {
                       Core.fields =
                         [
                           struct_field ty_i8_ptr "data";
                           struct_field ty_i32 "len";
                         ];
                       lifecycle = None;
                     });
              construct = Some ctor_decl;
              destruct = Some dtor_decl;
            }))
  in
  let global_buffer_decl =
    node
      {
        Core.name = ident "GLOBAL_BUFFER";
        public = false;
        is_mutable = false;
        ty = ty_buffer;
        init_expr = None;
      }
  in
  let main_decl =
    fn_decl "main" ~public:true ~impure:true ~return_type:(Some ty_i32)
      ~definition:
        (Some
           (block ~result:(int_lit 0)
              [
                let_stmt ~mut:true "boxed" box_buffer_expr;
                assign_stmt (node (Core.Identifier (ident "boxed"))) nil_expr;
              ]))
  in
  let lifecycle_ir =
    emit_core_ir
      (node
         {
           Core.decls =
             [
               buffer_type;
               node (Core.VDecl global_buffer_decl);
               node (Core.FDecl ctor_decl);
               node (Core.FDecl dtor_decl);
               node (Core.FDecl main_decl);
             ];
         })
  in
  assert_true "global default initialization should emit a synthesized ctor"
    (string_contains lifecycle_ir "@__haven_global_init");
  assert_true "global default initialization should call the type constructor"
    (string_contains lifecycle_ir "call void @buffer_construct");
  assert_true "box type construction should call the type constructor"
    (string_contains lifecycle_ir "call void @buffer_construct");
  assert_true "final box release should call the type destructor"
    (string_contains lifecycle_ir "call void @buffer_destruct")
