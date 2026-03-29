open Test_support

let run () =
  let bad_semantics =
    parse_to_core "pub fn main() -> void { break; }" |> Analysis.Pipeline.run_core
  in
  assert_true "expected break outside loop to fail semantic analysis"
    (bad_semantics.semantic.diagnostics <> []);
  assert_diagnostic_category "semantic diagnostic category" Analysis.Semantic
    bad_semantics.semantic.diagnostics;

  let bad_typing =
    parse_to_core "pub fn main() -> void { foo; }" |> Analysis.Typing.run
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
  assert_no_diagnostics "generic enum constructor verify" enum_pipeline.verify.diagnostics;
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
  assert_no_diagnostics "vector and matrix operator verify"
    vector_matrix_pipeline.verify.diagnostics;
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
    parse_to_core "pub fn main() -> void { let i32 x = nil; }" |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "nil assigned to integer binding" nil_pipeline.semantic.diagnostics;

  let verify_unknown_type_pipeline =
    parse_to_core "pub fn main(Missing value) -> void { }" |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "verify should reject unresolved parameter types"
    verify_unknown_type_pipeline.verify.diagnostics;
  assert_diagnostic_category "verify diagnostic category" Analysis.TypeVerify
    verify_unknown_type_pipeline.verify.diagnostics;

  let stpq_pipeline =
    parse_to_core "pub fn main(fvec4 uv) -> float { uv.s + uv.t + uv.p + uv.q }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "stpq vector aliases typing" stpq_pipeline.typing.diagnostics;
  assert_no_diagnostics "stpq vector aliases verify" stpq_pipeline.verify.diagnostics;
  assert_no_diagnostics "stpq vector aliases semantic" stpq_pipeline.semantic.diagnostics;

  let untyped_initializer =
    parse_to_core "pub fn main() -> void { let values = { 1, 2 }; }" |> Analysis.Typing.run
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
  assert_has_diagnostics "incompatible cast should fail" bad_cast_pipeline.semantic.diagnostics;

  let bad_ref_pipeline =
    parse_to_core "pub fn main() -> void { let x = ref as<i32>(5); }" |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "ref of non-lvalue should fail" bad_ref_pipeline.semantic.diagnostics;

  let bad_load_pipeline =
    parse_to_core "pub fn main() -> void { let x = load as<i32>(5); }" |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "load of plain value should fail" bad_load_pipeline.semantic.diagnostics;

  let bare_return_pipeline =
    parse_to_core "pub fn main() -> i32 { ret; }" |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "bare return in non-void function" bare_return_pipeline.semantic.diagnostics;

  let void_return_value_pipeline =
    parse_to_core "pub fn main() -> void { ret 5; }" |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "value return in void function"
    void_return_value_pipeline.semantic.diagnostics;

  let wrong_return_type_pipeline =
    parse_to_core "pub fn main() -> i32 { \"hello\" }" |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "wrong implicit return type" wrong_return_type_pipeline.semantic.diagnostics;

  let missing_return_pipeline =
    parse_to_core "pub fn main() -> i32 { let x = 5; }" |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "missing non-void return" missing_return_pipeline.semantic.diagnostics;

  let immutable_assign_pipeline =
    parse_to_core "pub fn main() -> void { let i32 x = 0; x = as<i32>(1); }"
    |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "assignment to immutable binding should fail"
    immutable_assign_pipeline.semantic.diagnostics;

  let immutable_field_assign_pipeline =
    parse_to_core
      {|
type Pair = struct {
  i32 left;
};

pub fn main() -> void {
  let Pair pair = { 0 };
  pair.left = as<i32>(1);
}
|}
    |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "assignment through immutable root binding should fail"
    immutable_field_assign_pipeline.semantic.diagnostics;

  let mutable_assign_pipeline =
    parse_to_core "pub fn main() -> void { let mut i32 x = 0; x = as<i32>(1); }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "assignment to mutable binding should pass"
    mutable_assign_pipeline.semantic.diagnostics;

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
    parse_to_core "pub fn main() -> i32 { match 5 { 5 => 5 } }" |> Analysis.Pipeline.run_core
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
    parse_to_core "pub fn main() -> void { let x = \"hi\" * 2; }" |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "invalid binary operands should fail" bad_binary_pipeline.semantic.diagnostics;

  let bad_vector_binary_pipeline =
    parse_to_core "pub fn main(fvec3 v) -> void { let x = v + 1.0; }"
    |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "invalid vector arithmetic should fail"
    bad_vector_binary_pipeline.semantic.diagnostics
