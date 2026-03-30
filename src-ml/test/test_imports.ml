open Test_support

let run () =
  with_temp_dir "haven-import" (fun root ->
      let lib_dir = Filename.concat root "lib" in
      let nested_dir = Filename.concat root "nested" in
      Unix.mkdir lib_dir 0o700;
      Unix.mkdir nested_dir 0o700;

      write_file (Filename.concat lib_dir "index.hv")
        {|
pub fn helper() -> i32 {
  7
}
|};

      write_file (Filename.concat nested_dir "index.hv")
        {|
import "../lib";

pub fn nested() -> i32 {
  helper()
}
|};

      let main_path = Filename.concat root "main.hv" in
      write_file main_path
        {|
import "lib";
import "nested";

pub fn main() -> i32 {
  helper() + nested()
}
|};

      let imported_pipeline = Analysis.Pipeline.run_cst (Haven.Parser.parse_file main_path) in
      assert_no_diagnostics "imported module typing" imported_pipeline.typing.diagnostics;
      assert_no_diagnostics "imported module verify" imported_pipeline.verify.diagnostics;
      assert_no_diagnostics "imported module semantic" imported_pipeline.semantic.diagnostics;

      let helper_count =
        List.fold_left
          (fun acc (decl : Core.top_decl) ->
            match decl.value with
            | Core.FDecl fn when fn.value.name.value = "helper" -> acc + 1
            | _ -> acc)
          0 imported_pipeline.core.program.value.decls
      in
      assert_true "expected helper to be imported exactly once" (helper_count = 1);
      assert_true "expected import declarations to be expanded away"
        (not
           (List.exists
              (fun (decl : Core.top_decl) ->
                match decl.value with Core.Import _ -> true | _ -> false)
              imported_pipeline.core.program.value.decls)));

  with_temp_dir "haven-missing-import" (fun root ->
      let main_path = Filename.concat root "main.hv" in
      write_file main_path
        {|
import "missing";

pub fn main() -> void {
}
|};

      let unresolved = Haven.Ast.Imports.expand_cst (Haven.Parser.parse_file main_path) in
      assert_has_diagnostics "missing import should produce diagnostics"
        unresolved.diagnostics;
      assert_diagnostic_category "missing import diagnostic category" Analysis.Import
        unresolved.diagnostics;
      assert_any_diagnostic_message_contains "missing import wording" "failed to resolve"
        unresolved.diagnostics);

  with_temp_dir "haven-include-import" (fun root ->
      let include_dir = Filename.concat root "include" in
      let src_dir = Filename.concat root "src" in
      Unix.mkdir include_dir 0o700;
      Unix.mkdir src_dir 0o700;

      write_file (Filename.concat include_dir "helper.hv")
        {|
pub fn helper() -> i32 {
  11
}
|};

      let main_path = Filename.concat src_dir "main.hv" in
      write_file main_path
        {|
import "helper";

pub fn main() -> i32 {
  helper()
}
|};

      let imported_pipeline =
        Analysis.Pipeline.run_cst ~search_dirs:[ include_dir ]
          (Haven.Parser.parse_file main_path)
      in
      assert_no_diagnostics "include search path typing" imported_pipeline.typing.diagnostics;
      assert_no_diagnostics "include search path verify" imported_pipeline.verify.diagnostics;
      assert_no_diagnostics "include search path semantic" imported_pipeline.semantic.diagnostics;
      ignore (find_named_function "helper" imported_pipeline.core));

  with_temp_dir "haven-cimport-local" (fun root ->
      write_file (Filename.concat root "add.h")
        {|
int add(int left, int right);
|};

      let add_main_path = Filename.concat root "add-main.hv" in
      write_file add_main_path
        {|
cimport "add.h";

pub impure fn main() -> i32 {
  add(2, 3)
}
|};

      let add_pipeline =
        Analysis.Pipeline.run_cst ~search_dirs:[ root ] (Haven.Parser.parse_file add_main_path)
      in
      assert_no_diagnostics "function-only cimport typing" add_pipeline.typing.diagnostics;
      assert_no_diagnostics "function-only cimport verify" add_pipeline.verify.diagnostics;
      assert_no_diagnostics "function-only cimport semantic" add_pipeline.semantic.diagnostics;
      ignore (find_named_function "add" add_pipeline.core);

      write_file (Filename.concat root "sample.h")
        {|
typedef unsigned long size_t;
typedef struct {
  int x;
  int y;
} Point;
extern int counter;
int add(int left, int right);
typedef int (*callback_t)(int);
enum Color { Red = 1, Blue = 2 };
|};

      let main_path = Filename.concat root "main.hv" in
      write_file main_path
        {|
cimport "sample.h";

pub impure fn main() -> i32 {
  let Point point = { 1, 2 };
  let size_t n = 0;
  add(point.x + point.y + Blue, Red) + as<i32>(n)
}
|};

      let imported_pipeline =
        Analysis.Pipeline.run_cst ~search_dirs:[ root ] (Haven.Parser.parse_file main_path)
      in
      assert_no_diagnostics "local cimport typing" imported_pipeline.typing.diagnostics;
      assert_no_diagnostics "local cimport verify" imported_pipeline.verify.diagnostics;
      assert_no_diagnostics "local cimport semantic" imported_pipeline.semantic.diagnostics;
      ignore (find_named_function "add" imported_pipeline.core);
      ignore (find_named_function "main" imported_pipeline.core));

  let stdio_pipeline =
    Analysis.Pipeline.run_cst
      (Haven.Parser.parse_string
         {|
cimport "stdio.h";

pub impure fn main() -> i32 {
  printf("hi\n");
  0
}
|})
  in
  assert_no_diagnostics "system cimport typing" stdio_pipeline.typing.diagnostics;
  assert_no_diagnostics "system cimport verify" stdio_pipeline.verify.diagnostics;
  assert_no_diagnostics "system cimport semantic" stdio_pipeline.semantic.diagnostics;
  let printf_decl = find_named_function "printf" stdio_pipeline.core in
  assert_true "expected printf to remain variadic" printf_decl.value.vararg;

  let stdlib_pipeline =
    Analysis.Pipeline.run_cst
      (Haven.Parser.parse_string
         {|
cimport "stdlib.h";

pub impure fn main() -> i32 {
  let ptr = malloc(16);
  free(ptr);
  rand()
}
|})
  in
  assert_no_diagnostics "stdlib cimport typing" stdlib_pipeline.typing.diagnostics;
  assert_no_diagnostics "stdlib cimport verify" stdlib_pipeline.verify.diagnostics;
  assert_no_diagnostics "stdlib cimport semantic" stdlib_pipeline.semantic.diagnostics;
  ignore (find_named_function "malloc" stdlib_pipeline.core);
  ignore (find_named_function "free" stdlib_pipeline.core)
