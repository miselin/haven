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
              imported_pipeline.core.program.value.decls)))
