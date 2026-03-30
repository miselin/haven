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
  | Core.Unary _ -> ()
  | _ -> failwith "expected cleanup to remove redundant ToBool around bool-valued condition");

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
  assert_no_diagnostics "specialization semantic" specialization_pipeline.semantic.diagnostics
