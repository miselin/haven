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

let parse_to_core text = Haven.Ast.Convert.core_of_cst (Haven.Parser.parse_string text)

let find_first_let_binding (program : Core.parsed_program) =
  let rec find_in_statements (statements : Core.statement list) =
    match statements with
    | [] -> None
    | stmt :: rest -> (
        match stmt.value with
        | Core.Let binding -> Some binding
        | Core.Expression _
        | Core.Return _
        | Core.Defer _
        | Core.Break
        | Core.Continue ->
            find_in_statements rest
        | Core.Loop loop -> (
            match find_in_statements loop.value.body.value.statements with
            | Some binding -> Some binding
            | None -> find_in_statements rest))
  in
  let rec find_in_decls (decls : Core.top_decl list) =
    match decls with
    | [] -> None
    | decl :: rest -> (
        match decl.value with
        | Core.FDecl fn -> (
            match fn.value.definition with
            | Some body -> (
                match find_in_statements body.value.statements with
                | Some binding -> Some binding
                | None -> find_in_decls rest)
            | None -> find_in_decls rest)
        | _ -> find_in_decls rest)
  in
  match find_in_decls program.program.value.decls with
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
    non_exhaustive_match_pipeline.semantic.diagnostics
