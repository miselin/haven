module Analysis = Haven.Ast.Analysis
module Llvm_ir = Haven.Ast.Llvm_ir
module Platform_defaults_common = Haven.Ast.Platform_defaults_common

type rc_case = {
  name : string;
  expected_rc : int;
}

type opt_case = {
  label : string;
  opt_level : Llvm_ir.opt_level;
}

let rc_cases =
  [
    { name = "simplest"; expected_rc = 0 };
    { name = "add"; expected_rc = 6 };
    { name = "prec1"; expected_rc = 90 };
    { name = "prec2"; expected_rc = 0 };
    { name = "shortcircuit"; expected_rc = 1 };
    { name = "constant"; expected_rc = 0 };
    { name = "struct"; expected_rc = 3 };
    { name = "match"; expected_rc = 0 };
    { name = "enum"; expected_rc = 2 };
    { name = "enum_nobind"; expected_rc = 0 };
    { name = "enum_multi_payload"; expected_rc = 5 };
    { name = "ptr"; expected_rc = 5 };
    { name = "array"; expected_rc = 16 };
    { name = "defer"; expected_rc = 1 };
    { name = "type_alias"; expected_rc = 0 };
    { name = "block"; expected_rc = 0 };
    { name = "iter_up"; expected_rc = 0 };
    { name = "iter_down"; expected_rc = 0 };
    { name = "llist"; expected_rc = 1 };
    { name = "comment"; expected_rc = 0 };
    { name = "preproc"; expected_rc = 0 };
    { name = "if"; expected_rc = 1 };
    { name = "unary"; expected_rc = -12 };
    { name = "printf"; expected_rc = 4 };
    { name = "char"; expected_rc = 97 };
    { name = "while"; expected_rc = 21 };
    { name = "enum_template"; expected_rc = 5 };
    { name = "sizeof"; expected_rc = 12 };
    { name = "box"; expected_rc = 5 };
    { name = "box_return"; expected_rc = 5 };
    { name = "fnptr"; expected_rc = 5 };
    { name = "until"; expected_rc = 10 };
    { name = "box_store"; expected_rc = 10 };
    { name = "box_pass"; expected_rc = 5 };
    { name = "enum_template_multi"; expected_rc = 0 };
    { name = "struct_ret"; expected_rc = 6 };
    { name = "match_stmt"; expected_rc = 5 };
    { name = "mat_extract"; expected_rc = 2 };
    { name = "array_local"; expected_rc = 16 };
  ]

let opt_cases =
  [
    { label = "Os"; opt_level = Llvm_ir.Os };
    { label = "O0"; opt_level = Llvm_ir.O0 };
    { label = "O1"; opt_level = Llvm_ir.O1 };
    { label = "O2"; opt_level = Llvm_ir.O2 };
    { label = "O3"; opt_level = Llvm_ir.O3 };
  ]

let runtime_harness_c =
  {|
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int sut_rc = 0;

extern int sut(void);

int sut_exit(int rc) {
  sut_rc = rc;
  return rc;
}

void *__haven_new_empty_box(int box_size) {
  uint32_t *box = (uint32_t *)calloc(1, (size_t)box_size);
  if (box != NULL) {
    box[0] = 1;
  }
  return box;
}

void *__haven_new_box(void *payload, int box_size, int value_size) {
  char *box = (char *)__haven_new_empty_box(box_size);
  if (box != NULL && payload != NULL && value_size > 0) {
    memcpy(box + (sizeof(uint32_t) * 4), payload, (size_t)value_size);
  }
  return box;
}

void __haven_box_ref(void *box) {
  if (box != NULL) {
    ((uint32_t *)box)[0] += 1;
  }
}

void __haven_box_unref(void *box) {
  if (box != NULL) {
    uint32_t *header = (uint32_t *)box;
    if (--header[0] == 0) {
      free(box);
    }
  }
}

int main(void) {
  int rc = sut();
  if (sut_rc != 0) {
    rc = sut_rc;
  }
  fprintf(stderr, "HAVEN_RC=%d\n", rc);
  return 0;
}
|}

type command_result = {
  status : Unix.process_status;
  output : string;
}

let rec find_repo_root dir =
  let marker = Filename.concat dir "tests/inputs/add.hv" in
  if Sys.file_exists marker then dir
  else
    let parent = Filename.dirname dir in
    if String.equal parent dir then failwith "failed to locate repository root" else find_repo_root parent

let resolve_repo_root () =
  match Sys.getenv_opt "HAVEN_REPO_ROOT" with
  | Some dir ->
      let marker = Filename.concat dir "tests/inputs/add.hv" in
      if Sys.file_exists marker then dir
      else failwith "HAVEN_REPO_ROOT does not contain tests/inputs/add.hv"
  | None -> find_repo_root (Sys.getcwd ())

let read_all fd =
  let buffer = Buffer.create 1024 in
  let bytes = Bytes.create 4096 in
  let rec loop () =
    match Unix.read fd bytes 0 (Bytes.length bytes) with
    | 0 -> ()
    | count ->
        Buffer.add_subbytes buffer bytes 0 count;
        loop ()
  in
  loop ();
  Buffer.contents buffer

let run_command_capture ~prog ~args =
  let read_fd, write_fd = Unix.pipe () in
  let argv = Array.of_list (prog :: args) in
  let pid =
    Unix.create_process_env prog argv (Unix.environment ()) Unix.stdin write_fd write_fd
  in
  Unix.close write_fd;
  let output = read_all read_fd in
  Unix.close read_fd;
  let _, status = Unix.waitpid [] pid in
  { status; output }

let require_success label result =
  match result.status with
  | Unix.WEXITED 0 -> ()
  | Unix.WEXITED code ->
      failwith
        (Printf.sprintf "%s failed with exit code %d\n%s" label code result.output)
  | Unix.WSIGNALED signal ->
      failwith
        (Printf.sprintf "%s terminated by signal %d\n%s" label signal result.output)
  | Unix.WSTOPPED signal ->
      failwith
        (Printf.sprintf "%s stopped by signal %d\n%s" label signal result.output)

let pipeline_errors (pipeline : Analysis.Pipeline.result) =
  let diagnostics =
    pipeline.typing.diagnostics
    @ pipeline.verify.diagnostics
    @ pipeline.semantic.diagnostics
    @ pipeline.purity.diagnostics
    @ pipeline.ownership.diagnostics
  in
  List.filter (fun (d : Analysis.diagnostic) -> d.level = Analysis.Error) diagnostics

let format_diagnostic (diagnostic : Analysis.diagnostic) =
  let loc = diagnostic.loc in
  let file =
    if loc.start_pos.Lexing.pos_fname = "" then "<stdin>" else loc.start_pos.Lexing.pos_fname
  in
  let line = loc.start_pos.Lexing.pos_lnum in
  let col = loc.start_pos.Lexing.pos_cnum - loc.start_pos.Lexing.pos_bol + 1 in
  Printf.sprintf "%s:%d:%d: %s" file line col diagnostic.message

let compile_case_to_object ~source ~output_path opt_level =
  let parsed = Haven.Parser.parse_file source in
  let pipeline = Analysis.Pipeline.run_cst parsed in
  match pipeline_errors pipeline with
  | [] ->
      let module_ir =
        Llvm_ir.compile ~options:{ Llvm_ir.opt_level; debug_llvm = false; emit_preamble = true } pipeline
      in
      Llvm_ir.emit_object_file module_ir output_path
  | diagnostic :: _ ->
      failwith
        (Printf.sprintf "unexpected compiler diagnostics while compiling %s\n%s" source
           (format_diagnostic diagnostic))

let link_case_executable ~harness_obj ~sut_obj ~output_path =
  let linker_args =
    [ harness_obj; sut_obj ]
    @ (if Platform_defaults_common.is_linux_host () then [ "-no-pie" ] else [])
    @ [ "-o"; output_path ]
  in
  let result =
    run_command_capture ~prog:"cc" ~args:linker_args
  in
  require_success ("link " ^ output_path) result

let extract_reported_rc output =
  let lines = String.split_on_char '\n' output in
  let prefix = "HAVEN_RC=" in
  let prefix_len = String.length prefix in
  let rec loop = function
    | [] -> None
    | line :: rest ->
        if String.length line >= prefix_len && String.sub line 0 prefix_len = prefix then
          Some (int_of_string (String.sub line prefix_len (String.length line - prefix_len)))
        else loop rest
  in
  loop lines

let run_case temp_dir harness_obj root (case : rc_case) (opt : opt_case) =
  let source = Filename.concat root ("tests/inputs/" ^ case.name ^ ".hv") in
  let sut_obj = Filename.concat temp_dir (case.name ^ "." ^ opt.label ^ ".o") in
  let exe = Filename.concat temp_dir (case.name ^ "." ^ opt.label ^ ".exe") in
  try
    compile_case_to_object ~source ~output_path:sut_obj opt.opt_level;
    link_case_executable ~harness_obj ~sut_obj ~output_path:exe;
    let result = run_command_capture ~prog:exe ~args:[] in
    require_success ("run " ^ case.name ^ " " ^ opt.label) result;
    match extract_reported_rc result.output with
    | Some actual when actual = case.expected_rc -> None
    | Some actual ->
        Some
          (Printf.sprintf "%s/%s returned %d, expected %d\n%s" case.name opt.label actual
             case.expected_rc result.output)
    | None ->
        Some
          (Printf.sprintf "%s/%s did not report a result marker\n%s" case.name opt.label
             result.output)
  with Failure message -> Some (Printf.sprintf "%s/%s failed\n%s" case.name opt.label message)

let run () =
  let root = resolve_repo_root () in
  Test_support.with_temp_dir "haven-rc" (fun temp_dir ->
      let harness_c = Filename.concat temp_dir "rc_harness.c" in
      let harness_obj = Filename.concat temp_dir "rc_harness.o" in
      Test_support.write_file harness_c runtime_harness_c;
      require_success "compile rc harness"
        (run_command_capture ~prog:"cc" ~args:[ "-c"; harness_c; "-o"; harness_obj ]);
      let failures =
        List.fold_left
          (fun failures case ->
            List.fold_left
              (fun failures opt ->
                match run_case temp_dir harness_obj root case opt with
                | Some failure -> failure :: failures
                | None -> failures)
              failures opt_cases)
          [] rc_cases
      in
      match List.rev failures with
      | [] -> ()
      | failures ->
          failwith
            (Printf.sprintf "rc integration failures:\n%s"
               (String.concat "\n" failures)))
