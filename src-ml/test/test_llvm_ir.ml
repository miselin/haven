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
    (string_contains box_ir "@__haven_box_unref")
