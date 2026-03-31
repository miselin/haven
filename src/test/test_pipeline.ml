open Test_support

let run () =
  let typed_program =
    parse_to_core "pub fn main() -> void { let x = 5; }" |> Analysis.Typing.run
  in
  assert_no_diagnostics "typing integer inference" typed_program.diagnostics;
  let binding = find_first_let_binding typed_program.program in
  let binding_ann =
    Hashtbl.find typed_program.annotations.bindings (Analysis.binding_id binding)
  in
  (match binding_ann.inferred_type with
  | Some ty -> (
      match ty.value with
      | Core.NumericType { signedness = Haven.Token.Unsigned; bits = 3 } -> ()
      | _ -> failwith "expected let x = 5 to infer an unsigned 3-bit numeric type")
  | None -> failwith "expected inferred type for let binding");
  (match binding_ann.metavar.integer with
  | Some { exact_value = Some 5; minimum_bits = Some 3; _ } -> ()
  | _ -> failwith "expected integer metavar to record exact value and width");

  let pipeline =
    Haven.Parser.parse_string "pub fn main() -> i32 { if !0 { 1 } else { 0 } }"
    |> Analysis.Pipeline.run_cst
  in
  assert_no_diagnostics "semantic cleanup input typing" pipeline.typing.diagnostics;
  assert_no_diagnostics "semantic cleanup input verify" pipeline.verify.diagnostics;
  assert_no_diagnostics "semantic cleanup input semantic" pipeline.semantic.diagnostics;
  let scrutinee_before = find_if_scrutinee pipeline.core in
  let scrutinee_folded = find_if_scrutinee pipeline.cfold in
  let scrutinee_after = find_if_scrutinee pipeline.cleaned in
  (match scrutinee_before.value with
  | Core.ToBool _ -> ()
  | _ -> failwith "expected lowered if condition to contain ToBool before cleanup");
  (match scrutinee_folded.value with
  | Core.Literal lit -> (
      match lit.value with
      | Core.Bool true -> ()
      | _ -> failwith "expected constant folding to reduce the if condition to true")
  | _ -> failwith "expected constant folding to reduce the if condition to a literal");
  (match scrutinee_after.value with
  | Core.Literal lit -> (
      match lit.value with
      | Core.Bool true -> ()
      | _ -> failwith "expected cleanup to preserve the folded boolean condition")
  | _ -> failwith "expected cleanup to preserve the folded boolean condition");

  let folded_pipeline =
    parse_to_core "pub fn main() -> i32 { let x = 1 + 2 * 3; x }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "constant folding typing" folded_pipeline.typing.diagnostics;
  assert_no_diagnostics "constant folding semantic" folded_pipeline.semantic.diagnostics;
  let folded_binding = find_first_let_binding folded_pipeline.cfold in
  (match folded_binding.value.init_expr.value with
  | Core.Literal lit -> (
      match lit.value with
      | Core.Integer 7 -> ()
      | _ -> failwith "expected arithmetic constant folding to produce 7")
  | _ -> failwith "expected arithmetic constant folding to produce a literal");

  let specialization_pipeline =
    parse_to_core
      "fn vadd(fvec? a, fvec? b) { a + b }\n\
       fn width(mat? m) { m.cols }\n\
       fn main() -> i32 { 0 }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "specialization typing" specialization_pipeline.typing.diagnostics;
  assert_no_diagnostics "specialization verify" specialization_pipeline.verify.diagnostics;
  assert_no_diagnostics "specialization semantic" specialization_pipeline.semantic.diagnostics;

  let specialization_call_pipeline =
    parse_to_core
      "fn vadd(fvec? a, fvec? b) { a + b }\n\
       fn main() -> fvec3 {\n\
       \  vadd(Vec<1.0, 2.0, 3.0>, Vec<4.0, 5.0, 6.0>)\n\
       }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "specialization call typing"
    specialization_call_pipeline.typing.diagnostics;
  assert_no_diagnostics "specialization call verify"
    specialization_call_pipeline.verify.diagnostics;
  assert_no_diagnostics "specialization call semantic"
    specialization_call_pipeline.semantic.diagnostics;
  let specialization_core =
    Haven.Ast.Pretty.core_program_to_string specialization_call_pipeline.cleaned
  in
  assert_true "specialized pipeline should emit a concrete clone"
    (string_contains specialization_core "vadd__spec__fvec3__fvec3");
  assert_true "specialized pipeline should erase hole types from the lowered program"
    (not (string_contains specialization_core "fvec?"));

  let shape_property_pipeline =
    parse_to_core
      "fn width(mat? m) { m.cols }\n\
       pub fn main() -> u32 {\n\
       \  width(Mat<Vec<1.0, 2.0>, Vec<3.0, 4.0>>)\n\
       }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "shape property typing" shape_property_pipeline.typing.diagnostics;
  assert_no_diagnostics "shape property verify" shape_property_pipeline.verify.diagnostics;
  assert_no_diagnostics "shape property semantic" shape_property_pipeline.semantic.diagnostics;
  let shape_property_core =
    Haven.Ast.Pretty.core_program_to_string shape_property_pipeline.cleaned
  in
  assert_true "shape property specialization should clone the function"
    (string_contains shape_property_core "width__spec__mat2x2");
  assert_true "shape properties should lower to integer literals before LLVM"
    (string_contains shape_property_core "Literal(2)");

  let get_mat_row_pipeline =
    parse_to_core
      "fn get_mat_row(mat? m, u32 row) { m[row] }\n\
       pub fn main() -> fvec3 {\n\
       \  get_mat_row(Mat<Vec<1.0, 2.0, 3.0>, Vec<4.0, 5.0, 6.0>>, 1)\n\
       }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "get_mat_row typing" get_mat_row_pipeline.typing.diagnostics;
  assert_no_diagnostics "get_mat_row verify" get_mat_row_pipeline.verify.diagnostics;
  assert_no_diagnostics "get_mat_row semantic" get_mat_row_pipeline.semantic.diagnostics;
  let get_mat_row_core =
    Haven.Ast.Pretty.core_program_to_string get_mat_row_pipeline.cleaned
  in
  assert_true "get_mat_row should specialize to a concrete matrix helper"
    (string_contains get_mat_row_core "get_mat_row__spec__mat2x3");
  assert_true "get_mat_row specialization should infer a concrete vector return"
    (string_contains get_mat_row_core "return=fvec3");

  let specialization_dedup_pipeline =
    parse_to_core
      "fn get_mat_row(mat? m, u32 row) { m[row] }\n\
       pub fn main() -> fvec3 {\n\
       \  let u32 row = 1;\n\
       \  get_mat_row(Mat<Vec<1.0, 2.0, 3.0>, Vec<4.0, 5.0, 6.0>>, row)\n\
       }\n\
       pub fn other() -> fvec3 {\n\
       \  get_mat_row(Mat<Vec<7.0, 8.0, 9.0>, Vec<10.0, 11.0, 12.0>>, 1)\n\
       }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "specialization dedup typing"
    specialization_dedup_pipeline.typing.diagnostics;
  let specialization_dedup_core =
    Haven.Ast.Pretty.core_program_to_string specialization_dedup_pipeline.cleaned
  in
  assert_true "equivalent concrete signatures should reuse one specialization"
    (count_occurrences specialization_dedup_core
       "name=get_mat_row__spec__mat2x3__u32"
    = 1);

  let compile_assert_pipeline =
    parse_to_core
      "fn mat_width_eq(mat? a, mat? b) {\n\
       \  @assert a.cols == b.cols, \"matrix widths must match\";\n\
       \  a.cols\n\
       }\n\
       pub fn main() -> u32 {\n\
       \  mat_width_eq(Mat<Vec<1.0, 2.0>, Vec<3.0, 4.0>>, Mat<Vec<5.0, 6.0>, Vec<7.0, 8.0>>)\n\
       }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "compile assert typing" compile_assert_pipeline.typing.diagnostics;
  assert_no_diagnostics "compile assert verify" compile_assert_pipeline.verify.diagnostics;
  assert_no_diagnostics "compile assert semantic" compile_assert_pipeline.semantic.diagnostics;
  assert_no_diagnostics "compile assert pass" compile_assert_pipeline.asserts.diagnostics;
  let compile_assert_core =
    Haven.Ast.Pretty.core_program_to_string compile_assert_pipeline.cleaned
  in
  assert_true "successful compile asserts should be erased before the cleaned AST"
    (not (string_contains compile_assert_core "CompileAssert"));

  let compile_assert_fail_pipeline =
    parse_to_core
      "fn mat_width_eq(mat? a, mat? b) {\n\
       \  @assert a.cols == b.cols, \"matrix widths must match\";\n\
       \  a.cols\n\
       }\n\
       pub fn main() -> u32 {\n\
       \  mat_width_eq(Mat<Vec<1.0, 2.0>, Vec<3.0, 4.0>>, Mat<Vec<5.0>, Vec<6.0>>)\n\
       }"
    |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "failing compile assert should produce diagnostics"
    compile_assert_fail_pipeline.asserts.diagnostics;
  assert_any_diagnostic_message_contains "failing compile assert should preserve the user message"
    "matrix widths must match" compile_assert_fail_pipeline.asserts.diagnostics;
  assert_any_diagnostic_message_contains
    "failing compile assert should include the rendered condition"
    "compile-time assertion failed: a.cols == b.cols"
    compile_assert_fail_pipeline.asserts.diagnostics;
  assert_any_diagnostic_message_contains
    "failing compile assert should include the specialized condition"
    "specialized as: 2 == 1"
    compile_assert_fail_pipeline.asserts.diagnostics;

  let compile_assert_short_circuit_pipeline =
    parse_to_core
      "fn vadd(fvec? a, fvec? b) {\n\
       \  @assert a.dim == b.dim, \"vector dimensions must match\";\n\
       \  a + b\n\
       }\n\
       pub fn main() -> fvec3 {\n\
       \  vadd(Vec<1.0, 2.0, 3.0>, Vec<4.0, 5.0>)\n\
       }"
    |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "failing compile assert should still report the assert"
    compile_assert_short_circuit_pipeline.asserts.diagnostics;
  assert_any_diagnostic_message_contains
    "failing compile assert should keep the user-facing message"
    "vector dimensions must match"
    compile_assert_short_circuit_pipeline.asserts.diagnostics;
  assert_any_diagnostic_message_contains
    "failing compile assert should include the rendered vector condition"
    "compile-time assertion failed: a.dim == b.dim"
    compile_assert_short_circuit_pipeline.asserts.diagnostics;
  assert_any_diagnostic_message_contains
    "failing compile assert should include the specialized vector condition"
    "specialized as: 3 == 2"
    compile_assert_short_circuit_pipeline.asserts.diagnostics;
  assert_no_diagnostics
    "failing compile assert should short-circuit later semantic analysis"
    compile_assert_short_circuit_pipeline.semantic.diagnostics
