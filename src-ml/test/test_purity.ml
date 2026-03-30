open Test_support

let run () =
  let foreign_pipeline =
    Haven.Parser.parse_string
      {|
foreign "c" {
  fn puts(str s) -> i32;
}

pub fn main() -> i32 {
  puts("hello")
}
|}
    |> Analysis.Pipeline.run_cst
  in
  let puts_decl = find_named_function "puts" foreign_pipeline.core in
  assert_true "foreign declarations should be marked public"
    puts_decl.value.public;
  assert_true "foreign declarations should be marked impure"
    puts_decl.value.impure;
  assert_has_diagnostics "calling foreign from a pure function should fail purity"
    foreign_pipeline.purity.diagnostics;
  assert_any_diagnostic_message_contains
    "foreign purity diagnostic should mention the calling function" "main"
    foreign_pipeline.purity.diagnostics;

  let box_load_pipeline =
    parse_to_core "pub fn main() -> i32 { let boxed = box as<i32>(5); load boxed }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "load from box typing" box_load_pipeline.typing.diagnostics;
  assert_no_diagnostics "load from box semantic" box_load_pipeline.semantic.diagnostics;
  assert_has_diagnostics "load from box purity" box_load_pipeline.purity.diagnostics;
  assert_diagnostic_category "load from box purity category" Analysis.Purity
    box_load_pipeline.purity.diagnostics;

  let mutate_pipeline =
    parse_to_core
      "pub impure fn main() -> i32 { let mut i32 x = 0; let y = ref x := as<i32>(1); 0 }"
    |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "mutation used as a value" mutate_pipeline.semantic.diagnostics;
  assert_no_diagnostics "marked impure function should pass purity"
    mutate_pipeline.purity.diagnostics;

  let pure_call_pipeline =
    parse_to_core "fn helper() -> i32 { 1 } pub fn main() -> i32 { helper() }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "pure call purity" pure_call_pipeline.purity.diagnostics;

  let transitive_impurity_pipeline =
    parse_to_core
      "fn helper() -> i32 { let mut i32 x = 0; ref x := as<i32>(1); 1 } pub fn main() -> i32 { helper() }"
    |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "transitive impurity should fail purity"
    transitive_impurity_pipeline.purity.diagnostics;
  assert_diagnostic_category "transitive impurity category" Analysis.Purity
    transitive_impurity_pipeline.purity.diagnostics;
  assert_any_diagnostic_message_contains "transitive impurity should flag helper" "helper"
    transitive_impurity_pipeline.purity.diagnostics;
  assert_any_diagnostic_message_contains "transitive impurity should flag wrapper" "main"
    transitive_impurity_pipeline.purity.diagnostics
