module Analysis = Haven.Ast.Analysis
module Core = Haven.Ast.Analysis.Core

let assert_true message cond = if not cond then failwith message

let assert_no_diagnostics label diagnostics =
  assert_true (label ^ " unexpectedly produced diagnostics") (diagnostics = [])

let assert_has_diagnostics label diagnostics =
  assert_true (label ^ " unexpectedly produced no diagnostics") (diagnostics <> [])

let string_contains haystack needle =
  let haystack_len = String.length haystack in
  let needle_len = String.length needle in
  let rec loop index =
    if needle_len = 0 then true
    else if index + needle_len > haystack_len then false
    else if String.sub haystack index needle_len = needle then true
    else loop (index + 1)
  in
  loop 0

let assert_diagnostic_message_contains label needle diagnostics =
  match diagnostics with
  | [] -> failwith (label ^ " expected at least one diagnostic")
  | diagnostic :: _ ->
      assert_true (label ^ " did not include the expected diagnostic text")
        (string_contains diagnostic.Analysis.message needle)

let assert_diagnostic_category label expected_category diagnostics =
  match diagnostics with
  | [] -> failwith (label ^ " expected at least one diagnostic")
  | diagnostic :: _ ->
      assert_true (label ^ " had the wrong diagnostic category")
        (diagnostic.Analysis.category = expected_category)

let assert_has_ownership_action label predicate actions =
  assert_true (label ^ " did not include the expected ownership action")
    (List.exists predicate actions)

let parse_to_core text = Haven.Ast.Convert.core_of_cst (Haven.Parser.parse_string text)

let find_first_let_binding (program : Core.parsed_program) =
  let rec collect_in_statements acc (statements : Core.statement list) =
    match statements with
    | [] -> List.rev acc
    | stmt :: rest -> (
        match stmt.value with
        | Core.Let binding -> collect_in_statements (binding :: acc) rest
        | Core.Expression _
        | Core.Return _
        | Core.Defer _
        | Core.Break
        | Core.Continue ->
            collect_in_statements acc rest
        | Core.Loop loop ->
            collect_in_statements
              (List.rev_append
                 (List.rev (collect_in_statements [] loop.value.body.value.statements))
                 acc)
              rest)
  in
  let rec collect_in_decls acc (decls : Core.top_decl list) =
    match decls with
    | [] -> List.rev acc
    | decl :: rest -> (
        match decl.value with
        | Core.FDecl fn -> (
            match fn.value.definition with
            | Some body ->
                let acc =
                  List.rev_append
                    (List.rev (collect_in_statements [] body.value.statements))
                    acc
                in
                collect_in_decls acc rest
            | None -> collect_in_decls acc rest)
        | _ -> collect_in_decls acc rest)
  in
  match collect_in_decls [] program.program.value.decls with
  | binding :: _ -> binding
  | [] -> failwith "expected to find a let binding"

let find_let_binding_at index (program : Core.parsed_program) =
  let rec collect_stmt (bindings : Core.let_stmt list) (statements : Core.statement list) =
    match statements with
    | [] -> List.rev bindings
    | stmt :: stmt_rest -> (
        match stmt.value with
        | Core.Let binding ->
            collect_stmt (binding :: bindings) stmt_rest
        | Core.Loop loop ->
            let inner = collect_stmt [] loop.value.body.value.statements in
            collect_stmt (List.rev_append inner bindings) stmt_rest
        | Core.Expression _
        | Core.Return _
        | Core.Defer _
        | Core.Break
        | Core.Continue ->
            collect_stmt bindings stmt_rest)
  in
  let rec collect (bindings : Core.let_stmt list) (decls : Core.top_decl list) =
    match decls with
    | [] -> List.rev bindings
    | decl :: rest -> (
        match decl.value with
        | Core.FDecl fn -> (
            match fn.value.definition with
            | Some body ->
                collect (List.rev_append (collect_stmt [] body.value.statements) bindings) rest
            | None -> collect bindings rest)
        | _ -> collect bindings rest)
  in
  let bindings = collect [] program.program.value.decls in
  match List.nth_opt bindings index with
  | Some binding -> binding
  | None -> failwith "expected to find a let binding"

let find_if_scrutinee (program : Core.parsed_program) =
  let rec find_in_expression (expr : Core.expression) =
    match expr.value with
    | Core.Match match_expr -> Some match_expr.value.expr
    | Core.Block block -> Option.bind block.value.result find_in_expression
    | _ -> None
  in
  let rec find_in_decls (decls : Core.top_decl list) =
    match decls with
    | [] -> None
    | decl :: rest -> (
        match decl.value with
        | Core.FDecl fn -> (
            match fn.value.definition with
            | Some body -> Option.bind body.value.result find_in_expression
            | None -> find_in_decls rest)
        | _ -> find_in_decls rest)
  in
  match find_in_decls program.program.value.decls with
  | Some expr -> expr
  | None -> failwith "expected to find lowered if scrutinee"

let () =
  let typed_program =
    parse_to_core "pub fn main() -> void { let x = 5; }"
    |> Analysis.Typing.run
  in
  assert_no_diagnostics "typing integer inference" typed_program.diagnostics;
  let binding = find_first_let_binding typed_program.program in
  let binding_ann =
    Hashtbl.find typed_program.annotations.bindings
      (Analysis.binding_id binding)
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
  assert_no_diagnostics "semantic cleanup input semantic" pipeline.semantic.diagnostics;
  let scrutinee_before = find_if_scrutinee pipeline.core in
  let scrutinee_after = find_if_scrutinee pipeline.cleaned in
  (match scrutinee_before.value with
  | Core.ToBool _ -> ()
  | _ -> failwith "expected lowered if condition to contain ToBool before cleanup");
  (match scrutinee_after.value with
  | Core.Unary _ -> ()
  | _ -> failwith "expected cleanup to remove redundant ToBool around bool-valued condition");

  let bad_semantics =
    parse_to_core "pub fn main() -> void { break; }"
    |> Analysis.Pipeline.run_core
  in
  assert_true "expected break outside loop to fail semantic analysis"
    (bad_semantics.semantic.diagnostics <> []);
  assert_diagnostic_category "semantic diagnostic category"
    Analysis.Semantic bad_semantics.semantic.diagnostics;

  let bad_typing =
    parse_to_core "pub fn main() -> void { foo; }"
    |> Analysis.Typing.run
  in
  assert_has_diagnostics "expected unknown identifier to fail typing analysis"
    bad_typing.diagnostics;
  assert_diagnostic_category "typing diagnostic category" Analysis.TypeCheck
    bad_typing.diagnostics;

  let enum_pipeline =
    parse_to_core
      {|
type Result = enum <T> {
  Ok(T),
  Error
};

fn thing() -> Result::<i32> {
  Result::<i32>::Ok(5)
}

pub fn sut() -> i32 {
  match thing() {
    Ok(x) => x,
    _ => 1
  }
}
|}
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "generic enum constructor typing" enum_pipeline.typing.diagnostics;
  assert_no_diagnostics "generic enum pattern semantics" enum_pipeline.semantic.diagnostics;

  let expected_return_enum_pipeline =
    parse_to_core
      {|
type Result = enum <T> {
  Ok(T),
  Error
};

fn thing() -> Result::<i32> {
  Ok(5)
}

pub fn sut() -> i32 {
  match thing() {
    Ok(x) => x,
    _ => 1
  }
}
|}
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "expected return enum constructor typing"
    expected_return_enum_pipeline.typing.diagnostics;
  assert_no_diagnostics "expected return enum constructor semantics"
    expected_return_enum_pipeline.semantic.diagnostics;

  let expected_let_enum_pipeline =
    parse_to_core
      {|
type Result = enum <T> {
  Ok(T),
  Error
};

pub fn sut() -> i32 {
  let Result::<i32> value = Ok(5);
  match value {
    Ok(x) => x,
    _ => 1
  }
}
|}
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "expected let enum constructor typing"
    expected_let_enum_pipeline.typing.diagnostics;
  assert_no_diagnostics "expected let enum constructor semantics"
    expected_let_enum_pipeline.semantic.diagnostics;

  let expected_block_enum_pipeline =
    parse_to_core
      {|
type Result = enum <T> {
  Ok(T),
  Error
};

fn thing() -> Result::<i32> {
  { Ok(5) }
}

pub fn sut() -> i32 {
  match thing() {
    Ok(x) => x,
    _ => 1
  }
}
|}
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "expected block enum constructor typing"
    expected_block_enum_pipeline.typing.diagnostics;
  assert_no_diagnostics "expected block enum constructor semantics"
    expected_block_enum_pipeline.semantic.diagnostics;

  let parameter_enum_pipeline =
    parse_to_core
      {|
type Result = enum <T> {
  Ok(T),
  Error
};

fn pass(Result::<i32> value) -> Result::<i32> {
  value
}

pub fn sut() -> i32 {
  match pass(Ok(5)) {
    Ok(x) => x,
    _ => 1
  }
}
|}
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "parameter-driven enum constructor typing"
    parameter_enum_pipeline.typing.diagnostics;
  assert_no_diagnostics "parameter-driven enum constructor semantics"
    parameter_enum_pipeline.semantic.diagnostics;

  let vector_matrix_pipeline =
    parse_to_core
      {|
fn vadd(fvec3 a, fvec3 b) -> fvec3 {
  a + b
}

fn vscale(fvec3 a, float b) -> fvec3 {
  a * b
}

fn mmul(mat2x3 a, mat3x4 b) -> mat2x4 {
  a * b
}

fn mscale(mat2x3 a, float b) -> mat2x3 {
  a * b
}

fn vmul(fvec2 a, mat2x3 b) -> fvec3 {
  a * b
}

pub fn sut() -> void {}
|}
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "vector and matrix operator typing"
    vector_matrix_pipeline.typing.diagnostics;
  assert_no_diagnostics "vector and matrix operator semantic"
    vector_matrix_pipeline.semantic.diagnostics;

  let assignment_enum_pipeline =
    parse_to_core
      {|
type Result = enum <T> {
  Ok(T),
  Error
};

pub fn sut() -> i32 {
  let mut Result::<i32> value = Error;
  value = Ok(5);
  match value {
    Ok(x) => x,
    _ => 1
  }
}
|}
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "assignment-driven enum constructor typing"
    assignment_enum_pipeline.typing.diagnostics;
  assert_no_diagnostics "assignment-driven enum constructor semantics"
    assignment_enum_pipeline.semantic.diagnostics;

  let statement_match_pipeline =
    parse_to_core
      {|
pub fn sut() -> i32 {
  let mut i32 result = 0;
  match 5 {
    5 => {
      result = 5;
    }
  };
  result
}
|}
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "statement match without otherwise typing"
    statement_match_pipeline.typing.diagnostics;
  assert_no_diagnostics "statement match without otherwise semantic"
    statement_match_pipeline.semantic.diagnostics;

  let nil_pipeline =
    parse_to_core "pub fn main() -> void { let i32 x = nil; }"
    |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "nil assigned to integer binding" nil_pipeline.semantic.diagnostics;

  let box_copy_pipeline =
    parse_to_core
      "pub fn main() -> i32 { let boxed = box as<i32>(5); let inner = unbox boxed; inner }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "unbox copies in value contexts typing"
    box_copy_pipeline.typing.diagnostics;
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

  let box_load_pipeline =
    parse_to_core "pub fn main() -> i32 { let boxed = box as<i32>(5); load boxed }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "load from box typing" box_load_pipeline.typing.diagnostics;
  assert_no_diagnostics "load from box semantic" box_load_pipeline.semantic.diagnostics;

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
  assert_no_diagnostics "unbox passed as cell typing"
    box_cell_arg_pipeline.typing.diagnostics;
  assert_no_diagnostics "unbox passed as cell semantic"
    box_cell_arg_pipeline.semantic.diagnostics;

  let ownership_binding_pipeline =
    parse_to_core "pub fn main(i32^ input) -> void { let alias = input; }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "ownership binding typing"
    ownership_binding_pipeline.typing.diagnostics;
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
    parse_to_core "fn forward(i32^ input) -> i32^ { input }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "ownership return typing"
    ownership_return_pipeline.typing.diagnostics;
  assert_no_diagnostics "ownership return semantic"
    ownership_return_pipeline.semantic.diagnostics;
  assert_no_diagnostics "ownership return ownership"
    ownership_return_pipeline.ownership.diagnostics;
  assert_has_ownership_action "returning a shared box should retain it first"
    (fun action ->
      match (action.Analysis.kind, action.reason, action.subject) with
      | Analysis.Retain, Analysis.ReturnValue, Analysis.OwnershipExpr (_, Some "input")
        ->
          true
      | _ -> false)
    ownership_return_pipeline.ownership.actions;

  let ownership_call_pipeline =
    parse_to_core
      "fn consume(i32^ value) -> void { } pub fn main(i32^ input) -> void { consume(input); }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "ownership call typing"
    ownership_call_pipeline.typing.diagnostics;
  assert_no_diagnostics "ownership call semantic"
    ownership_call_pipeline.semantic.diagnostics;
  assert_no_diagnostics "ownership call ownership"
    ownership_call_pipeline.ownership.diagnostics;
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

  let untyped_initializer =
    parse_to_core "pub fn main() -> void { let values = { 1, 2 }; }"
    |> Analysis.Typing.run
  in
  assert_has_diagnostics "untyped initializer should fail typing"
    untyped_initializer.diagnostics;
  assert_diagnostic_message_contains "untyped initializer wording"
    "not enough information to infer type for initializer"
    untyped_initializer.diagnostics;

  let short_struct_initializer =
    parse_to_core
      {|
type Pair = struct {
  i32 left;
  i32 right;
};

pub fn main() -> void {
  let Pair pair = { 1 };
}
|}
    |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "short struct initializer should fail"
    short_struct_initializer.semantic.diagnostics;

  let bad_cast_pipeline =
    parse_to_core
      {|
type Pair = struct {
  i32 left;
  i32 right;
};

pub fn main() -> void {
  let Pair pair = as<Pair>(5);
}
|}
    |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "incompatible cast should fail"
    bad_cast_pipeline.semantic.diagnostics;

  let bad_ref_pipeline =
    parse_to_core "pub fn main() -> void { let x = ref as<i32>(5); }"
    |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "ref of non-lvalue should fail"
    bad_ref_pipeline.semantic.diagnostics;

  let bad_load_pipeline =
    parse_to_core "pub fn main() -> void { let x = load as<i32>(5); }"
    |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "load of plain value should fail"
    bad_load_pipeline.semantic.diagnostics;

  let bare_return_pipeline =
    parse_to_core "pub fn main() -> i32 { ret; }"
    |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "bare return in non-void function"
    bare_return_pipeline.semantic.diagnostics;

  let void_return_value_pipeline =
    parse_to_core "pub fn main() -> void { ret 5; }"
    |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "value return in void function"
    void_return_value_pipeline.semantic.diagnostics;

  let wrong_return_type_pipeline =
    parse_to_core "pub fn main() -> i32 { \"hello\" }"
    |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "wrong implicit return type"
    wrong_return_type_pipeline.semantic.diagnostics;

  let missing_return_pipeline =
    parse_to_core "pub fn main() -> i32 { let x = 5; }"
    |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "missing non-void return"
    missing_return_pipeline.semantic.diagnostics;

  let mutate_pipeline =
    parse_to_core
      "pub impure fn main() -> i32 { let mut i32 x = 0; let y = ref x := as<i32>(1); 0 }"
    |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "mutation used as a value" mutate_pipeline.semantic.diagnostics;

  let bad_pattern_pipeline =
    parse_to_core
      {|
type Result = enum <T> {
  Ok(T),
  Error
};

fn thing() -> Result::<i32> {
  Result::<i32>::Ok(5)
}

pub fn sut() -> i32 {
  match thing() {
    Ok => 1,
    _ => 0
  }
}
|}
    |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "enum payload pattern without binding"
    bad_pattern_pipeline.semantic.diagnostics;

  let non_exhaustive_match_pipeline =
    parse_to_core "pub fn main() -> i32 { match 5 { 5 => 5 } }"
    |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "non-exhaustive match should fail"
    non_exhaustive_match_pipeline.semantic.diagnostics;
  assert_diagnostic_message_contains "non-exhaustive match wording" "not exhaustive"
    non_exhaustive_match_pipeline.semantic.diagnostics;

  let mismatched_match_arms_pipeline =
    parse_to_core "pub fn main() -> i32 { match 5 { 5 => 5, _ => \"hi\" } }"
    |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "mismatched match arms should fail"
    mismatched_match_arms_pipeline.semantic.diagnostics;

  let bad_binary_pipeline =
    parse_to_core "pub fn main() -> void { let x = \"hi\" * 2; }"
    |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "invalid binary operands should fail"
    bad_binary_pipeline.semantic.diagnostics;

  let bad_vector_binary_pipeline =
    parse_to_core "pub fn main(fvec3 v) -> void { let x = v + 1.0; }"
    |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "invalid vector arithmetic should fail"
    bad_vector_binary_pipeline.semantic.diagnostics
