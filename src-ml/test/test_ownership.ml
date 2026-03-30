open Test_support

let run () =
  let box_copy_pipeline =
    parse_to_core
      "pub fn main() -> i32 { let boxed = box as<i32>(5); let inner = unbox boxed; inner }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "unbox copies in value contexts typing"
    box_copy_pipeline.typing.diagnostics;
  assert_no_diagnostics "unbox copies in value contexts verify"
    box_copy_pipeline.verify.diagnostics;
  assert_no_diagnostics "unbox copies in value contexts semantic"
    box_copy_pipeline.semantic.diagnostics;
  let inner_binding = find_let_binding_at 1 box_copy_pipeline.core in
  let inner_binding_ann =
    Hashtbl.find box_copy_pipeline.typing.annotations.bindings
      (Analysis.binding_id inner_binding)
  in
  (match inner_binding_ann.resolved_type with
  | Some (Analysis.ResolvedInt (_, _)) -> ()
  | _ -> failwith "expected let inner = unbox boxed to infer a copied inner value");

  let box_cell_pipeline =
    parse_to_core
      "pub fn main() -> i32 { let boxed = box as<i32>(5); let Cell<i32> inner = unbox boxed; load inner }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "unbox preserves cell when requested typing"
    box_cell_pipeline.typing.diagnostics;
  assert_no_diagnostics "unbox preserves cell when requested semantic"
    box_cell_pipeline.semantic.diagnostics;
  let cell_binding = find_let_binding_at 1 box_cell_pipeline.core in
  let cell_binding_ann =
    Hashtbl.find box_cell_pipeline.typing.annotations.bindings
      (Analysis.binding_id cell_binding)
  in
  (match cell_binding_ann.resolved_type with
  | Some (Analysis.ResolvedCell (Analysis.ResolvedInt (_, _))) -> ()
  | _ -> failwith "expected explicit Cell binding to preserve the unbox reference");

  let box_arg_pipeline =
    parse_to_core
      "fn take(i32 value) -> i32 { value } pub fn main() -> i32 { let boxed = box as<i32>(5); take(unbox boxed) }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "unbox passed by copied value typing"
    box_arg_pipeline.typing.diagnostics;
  assert_no_diagnostics "unbox passed by copied value semantic"
    box_arg_pipeline.semantic.diagnostics;

  let box_cell_arg_pipeline =
    parse_to_core
      "fn take(Cell<i32> value) -> i32 { load value } pub fn main() -> i32 { let boxed = box as<i32>(5); take(unbox boxed) }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "unbox passed as cell typing" box_cell_arg_pipeline.typing.diagnostics;
  assert_no_diagnostics "unbox passed as cell semantic"
    box_cell_arg_pipeline.semantic.diagnostics;

  let ownership_binding_pipeline =
    parse_to_core "pub fn main(i32^ input) -> void { let alias = input; }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "ownership binding typing"
    ownership_binding_pipeline.typing.diagnostics;
  assert_no_diagnostics "ownership binding verify" ownership_binding_pipeline.verify.diagnostics;
  assert_no_diagnostics "ownership binding semantic"
    ownership_binding_pipeline.semantic.diagnostics;
  assert_no_diagnostics "ownership binding ownership"
    ownership_binding_pipeline.ownership.diagnostics;
  assert_has_ownership_action "binding init should retain shared box handles"
    (fun action ->
      match (action.Analysis.kind, action.reason, action.subject) with
      | Analysis.Retain, Analysis.BindingInit, Analysis.OwnershipExpr (_, Some "input") ->
          true
      | _ -> false)
    ownership_binding_pipeline.ownership.actions;
  assert_has_ownership_action "binding scope exit should release box bindings"
    (fun action ->
      match (action.Analysis.kind, action.reason, action.subject) with
      | Analysis.Release, Analysis.ScopeExit, Analysis.OwnershipBinding "alias" -> true
      | _ -> false)
    ownership_binding_pipeline.ownership.actions;
  assert_has_ownership_action "function exit should release box params"
    (fun action ->
      match (action.Analysis.kind, action.reason, action.subject) with
      | Analysis.Release, Analysis.FunctionExit, Analysis.OwnershipParam "input" -> true
      | _ -> false)
    ownership_binding_pipeline.ownership.actions;

  let ownership_return_pipeline =
    parse_to_core "fn forward(i32^ input) -> i32^ { input }" |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "ownership return typing" ownership_return_pipeline.typing.diagnostics;
  assert_no_diagnostics "ownership return semantic"
    ownership_return_pipeline.semantic.diagnostics;
  assert_no_diagnostics "ownership return ownership"
    ownership_return_pipeline.ownership.diagnostics;
  assert_has_ownership_action "returning a shared box should retain it first"
    (fun action ->
      match (action.Analysis.kind, action.reason, action.subject) with
      | Analysis.Retain, Analysis.ReturnValue, Analysis.OwnershipExpr (_, Some "input") ->
          true
      | _ -> false)
    ownership_return_pipeline.ownership.actions;
  let forward_fn = find_named_function "forward" ownership_return_pipeline.core in
  let forward_body =
    match forward_fn.value.definition with
    | Some body -> body
    | None -> failwith "expected forward to have a body"
  in
  let forward_result =
    match forward_body.value.result with
    | Some expr -> expr
    | None -> failwith "expected forward to have an implicit return expression"
  in
  assert_has_ownership_action "indexed ownership after-expr lookup should expose return retains"
    (fun action ->
      match (action.Analysis.kind, action.reason) with
      | Analysis.Retain, Analysis.ReturnValue -> true
      | _ -> false)
    (Analysis.Ownership.actions_after_expr ownership_return_pipeline.ownership
       forward_result);
  assert_has_ownership_action
    "indexed ownership function-exit lookup should expose param releases"
    (fun action ->
      match (action.Analysis.kind, action.reason, action.subject) with
      | Analysis.Release, Analysis.FunctionExit, Analysis.OwnershipParam "input" -> true
      | _ -> false)
    (Analysis.Ownership.actions_on_function_exit ownership_return_pipeline.ownership forward_fn);

  let ownership_call_pipeline =
    parse_to_core
      "fn consume(i32^ value) -> void { } pub fn main(i32^ input) -> void { consume(input); }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "ownership call typing" ownership_call_pipeline.typing.diagnostics;
  assert_no_diagnostics "ownership call semantic" ownership_call_pipeline.semantic.diagnostics;
  assert_no_diagnostics "ownership call ownership" ownership_call_pipeline.ownership.diagnostics;
  assert_has_ownership_action "passing a shared box should retain it for the call"
    (fun action ->
      match (action.Analysis.kind, action.reason, action.subject) with
      | Analysis.Retain, Analysis.CallArg, Analysis.OwnershipExpr (_, Some "input") ->
          true
      | _ -> false)
    ownership_call_pipeline.ownership.actions;

  let ownership_assign_pipeline =
    parse_to_core
      "pub fn main(i32^ left, i32^ right) -> void { let mut alias = left; alias = right; }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "ownership assign typing"
    ownership_assign_pipeline.typing.diagnostics;
  assert_no_diagnostics "ownership assign semantic"
    ownership_assign_pipeline.semantic.diagnostics;
  assert_no_diagnostics "ownership assign ownership"
    ownership_assign_pipeline.ownership.diagnostics;
  assert_has_ownership_action "assignment should retain shared incoming box values"
    (fun action ->
      match (action.Analysis.kind, action.reason, action.subject) with
      | Analysis.Retain, Analysis.AssignValue, Analysis.OwnershipExpr (_, Some "right") ->
          true
      | _ -> false)
    ownership_assign_pipeline.ownership.actions;
  assert_has_ownership_action "assignment should release the overwritten box target"
    (fun action ->
      match (action.Analysis.kind, action.reason, action.subject) with
      | Analysis.Release, Analysis.AssignOverwrite, Analysis.OwnershipTarget (_, Some "alias")
        ->
          true
      | _ -> false)
    ownership_assign_pipeline.ownership.actions;
  let assign_main = find_named_function "main" ownership_assign_pipeline.core in
  let assign_stmt =
    match assign_main.value.definition with
    | Some body -> (
        match List.nth_opt body.value.statements 1 with
        | Some stmt -> stmt
        | None -> failwith "expected assignment statement")
    | None -> failwith "expected main to have a body"
  in
  let assign_expr =
    match assign_stmt.value with
    | Core.Expression expr -> expr
    | _ -> failwith "expected assignment expression statement"
  in
  assert_has_ownership_action
    "indexed ownership before-expr lookup should expose overwrite releases"
    (fun action ->
      match (action.Analysis.kind, action.reason) with
      | Analysis.Release, Analysis.AssignOverwrite -> true
      | _ -> false)
    (Analysis.Ownership.actions_before_expr ownership_assign_pipeline.ownership assign_expr)
