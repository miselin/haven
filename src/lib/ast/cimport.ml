open Analysis_types

module Cst = Haven_cst.Cst
module Parser = Haven_parser.Parser
module Yojson = Yojson.Basic

(* TODO: replace the clang CLI bridge with a proper libclang integration once the
   OCaml toolchain situation is less hostile. Keeping this logic in one module
   should make that swap straightforward. *)

type command_result = {
  status : Unix.process_status;
  stdout : string;
  stderr : string;
}

type target_info = {
  char_bits : int;
  short_bits : int;
  int_bits : int;
  long_bits : int;
  long_long_bits : int;
  pointer_bits : int;
  char_unsigned : bool;
}

type c_type =
  | CVoid
  | CInt of Haven_token.Token.signedness * int
  | CFloat
  | CString
  | CPointer of c_type
  | CArray of c_type * int
  | CFunction of c_type list * c_type * bool
  | CNamed of string

type c_decl =
  | CTypeForward of string
  | CTypeStruct of string * (string * c_type) list
  | CTypeAlias of string * c_type
  | CFunctionDecl of {
      name : string;
      params : (string * c_type) list;
      return_type : c_type;
      vararg : bool;
    }
  | CVarDecl of { name : string; ty : c_type }
  | CConstDecl of { name : string; ty : c_type; value : string }

type result = {
  decls : Cst.top_decl list;
  diagnostics : diagnostic list;
}

let target_info_cache : target_info option ref = ref None

let read_file path =
  let ch = open_in path in
  Fun.protect
    ~finally:(fun () -> close_in_noerr ch)
    (fun () ->
      let len = in_channel_length ch in
      really_input_string ch len)

let write_file path contents =
  let ch = open_out path in
  Fun.protect ~finally:(fun () -> close_out_noerr ch) (fun () -> output_string ch contents)

let rec waitpid_nointr pid =
  try Unix.waitpid [] pid with Unix.Unix_error (Unix.EINTR, "waitpid", _) -> waitpid_nointr pid

let run_command_capture ?stdin ~prog ~args () =
  let stdout_path = Filename.temp_file "haven-cimport-stdout" ".txt" in
  let stderr_path = Filename.temp_file "haven-cimport-stderr" ".txt" in
  let stdin_path =
    match stdin with
    | Some contents ->
        let path = Filename.temp_file "haven-cimport-stdin" ".txt" in
        write_file path contents;
        Some path
    | None -> None
  in
  let stdin_fd =
    match stdin_path with
    | Some path -> Unix.openfile path [ Unix.O_RDONLY ] 0
    | None -> Unix.stdin
  in
  let stdout_fd =
    Unix.openfile stdout_path [ Unix.O_WRONLY; Unix.O_CREAT; Unix.O_TRUNC ] 0o600
  in
  let stderr_fd =
    Unix.openfile stderr_path [ Unix.O_WRONLY; Unix.O_CREAT; Unix.O_TRUNC ] 0o600
  in
  let argv = Array.of_list (prog :: args) in
  let pid =
    Unix.create_process_env prog argv (Unix.environment ()) stdin_fd stdout_fd stderr_fd
  in
  if stdin_fd <> Unix.stdin then Unix.close stdin_fd;
  Unix.close stdout_fd;
  Unix.close stderr_fd;
  let _, status = waitpid_nointr pid in
  let stdout = read_file stdout_path in
  let stderr = read_file stderr_path in
  Sys.remove stdout_path;
  Sys.remove stderr_path;
  Option.iter Sys.remove stdin_path;
  { status; stdout; stderr }

let starts_with ~prefix text =
  let prefix_len = String.length prefix in
  String.length text >= prefix_len
  && String.equal prefix (String.sub text 0 prefix_len)

let ends_with ~suffix text =
  let suffix_len = String.length suffix in
  let len = String.length text in
  len >= suffix_len
  && String.equal suffix (String.sub text (len - suffix_len) suffix_len)

let trim = String.trim

let replace_all text needle replacement =
  let needle_len = String.length needle in
  if needle_len = 0 then text
  else
    let len = String.length text in
    let buffer = Buffer.create len in
    let rec loop index =
      if index >= len then ()
      else if index + needle_len <= len && String.equal needle (String.sub text index needle_len) then (
        Buffer.add_string buffer replacement;
        loop (index + needle_len))
      else (
        Buffer.add_char buffer text.[index];
        loop (index + 1))
    in
    loop 0;
    Buffer.contents buffer

let starts_with_at text ~index ~prefix =
  let prefix_len = String.length prefix in
  index + prefix_len <= String.length text
  && String.equal prefix (String.sub text index prefix_len)

let contains_substring text needle =
  let needle_len = String.length needle in
  let len = String.length text in
  if needle_len = 0 then true
  else
    let rec loop index =
      index + needle_len <= len
      && (String.equal needle (String.sub text index needle_len) || loop (index + 1))
    in
    loop 0

let skip_spaces text index =
  let len = String.length text in
  let rec loop index =
    if index < len then
      match text.[index] with ' ' | '\t' | '\n' | '\r' -> loop (index + 1) | _ -> index
    else index
  in
  loop index

let skip_balanced text index =
  let len = String.length text in
  if index >= len || text.[index] <> '(' then index
  else
    let rec loop index depth =
      if index >= len then len
      else
        match text.[index] with
        | '(' -> loop (index + 1) (depth + 1)
        | ')' ->
            if depth = 1 then index + 1 else loop (index + 1) (depth - 1)
        | _ -> loop (index + 1) depth
    in
    loop index 0

let remove_gnu_attributes text =
  let prefixes = [ "__attribute__"; "__attribute" ] in
  let len = String.length text in
  let buffer = Buffer.create len in
  let rec loop index =
    if index >= len then ()
    else
      match List.find_opt (fun prefix -> starts_with_at text ~index ~prefix) prefixes with
      | Some prefix ->
          let after_prefix = index + String.length prefix |> skip_spaces text in
          let after_attr =
            if after_prefix < len && text.[after_prefix] = '(' then skip_balanced text after_prefix
            else after_prefix
          in
          loop after_attr
      | None ->
          Buffer.add_char buffer text.[index];
          loop (index + 1)
  in
  loop 0;
  Buffer.contents buffer

let split_lines text =
  if String.equal text "" then []
  else String.split_on_char '\n' text

let remove_prefix prefix text =
  if starts_with ~prefix text then
    String.sub text (String.length prefix) (String.length text - String.length prefix)
  else text

let strip_tag_prefix text =
  text
  |> remove_prefix "struct "
  |> fun text -> remove_prefix "union " text
  |> fun text -> remove_prefix "enum " text
  |> trim

let should_skip_exported_name name = String.equal name "" || starts_with ~prefix:"__" name

let is_haven_keyword =
  let keywords =
    [
      "as";
      "box";
      "break";
      "cimport";
      "continue";
      "data";
      "defer";
      "else";
      "enum";
      "fn";
      "foreign";
      "Function";
      "if";
      "impure";
      "import";
      "iter";
      "let";
      "load";
      "match";
      "Mat";
      "mut";
      "nil";
      "pub";
      "ref";
      "ret";
      "size";
      "state";
      "struct";
      "type";
      "unbox";
      "until";
      "VAFunction";
      "Vec";
      "while";
    ]
  in
  fun name -> List.mem name keywords

let is_valid_identifier name =
  let len = String.length name in
  let is_start = function
    | 'a' .. 'z' | 'A' .. 'Z' | '_' -> true
    | _ -> false
  in
  let is_inner = function
    | 'a' .. 'z' | 'A' .. 'Z' | '0' .. '9' | '_' -> true
    | _ -> false
  in
  len > 0
  && is_start name.[0]
  && not (is_haven_keyword name)
  &&
  let rec loop index =
    if index >= len then true else if is_inner name.[index] then loop (index + 1) else false
  in
  loop 1

let add_diagnostic diagnostics_rev loc message =
  diagnostics_rev :=
    { category = Import; level = Error; loc; message } :: !diagnostics_rev

let get_macro_define name lines =
  let prefix = Printf.sprintf "#define %s " name in
  let rec loop = function
    | [] -> None
    | line :: rest ->
        if starts_with ~prefix line then
          Some (String.trim (String.sub line (String.length prefix) (String.length line - String.length prefix)))
        else loop rest
  in
  loop lines

let load_target_info () =
  match !target_info_cache with
  | Some info -> Ok info
  | None ->
      let result = run_command_capture ~prog:"clang" ~args:[ "-dM"; "-E"; "-x"; "c"; "/dev/null" ] () in
      (match result.status with
      | Unix.WEXITED 0 ->
          let lines = split_lines result.stdout in
          let find_int name =
            match get_macro_define name lines with
            | Some value -> int_of_string value
            | None -> failwith ("missing clang macro " ^ name)
          in
          let info =
            {
              char_bits = find_int "__CHAR_BIT__";
              short_bits = find_int "__SIZEOF_SHORT__" * 8;
              int_bits = find_int "__SIZEOF_INT__" * 8;
              long_bits = find_int "__SIZEOF_LONG__" * 8;
              long_long_bits = find_int "__SIZEOF_LONG_LONG__" * 8;
              pointer_bits = find_int "__SIZEOF_POINTER__" * 8;
              char_unsigned = Option.is_some (get_macro_define "__CHAR_UNSIGNED__" lines);
            }
          in
          target_info_cache := Some info;
          Ok info
      | Unix.WEXITED code ->
          Error (Printf.sprintf "clang macro query failed with exit code %d\n%s" code result.stderr)
      | Unix.WSIGNALED signal ->
          Error (Printf.sprintf "clang macro query terminated by signal %d" signal)
      | Unix.WSTOPPED signal ->
          Error (Printf.sprintf "clang macro query stopped by signal %d" signal))

let yojson_member_opt name (json : Yojson.t) =
  match json with
  | `Assoc fields -> List.assoc_opt name fields
  | _ -> None

let yojson_string_opt name json =
  match yojson_member_opt name json with Some (`String value) -> Some value | _ -> None

let yojson_bool name json =
  match yojson_member_opt name json with Some (`Bool value) -> value | _ -> false

let yojson_list name json =
  match yojson_member_opt name json with Some (`List items) -> items | _ -> []

let loc_file json =
  match yojson_member_opt "loc" json with
  | Some loc -> (
      match yojson_string_opt "file" loc with
      | Some _ as file -> file
      | None -> (
          match yojson_member_opt "range" json with
          | Some range -> (
              match yojson_member_opt "begin" range with
              | Some begin_loc -> yojson_string_opt "file" begin_loc
              | None -> None)
          | None -> None))
  | None -> None

let node_kind json = Option.value ~default:"" (yojson_string_opt "kind" json)
let node_name json = Option.value ~default:"" (yojson_string_opt "name" json)
let node_id json = Option.value ~default:"" (yojson_string_opt "id" json)

let qual_type json =
  match yojson_member_opt "type" json with
  | Some ty -> yojson_string_opt "qualType" ty
  | None -> None

let split_top_level text ~on_char =
  let len = String.length text in
  let rec loop index paren_depth bracket_depth current acc =
    if index >= len then
      let part = String.sub text current (len - current) |> trim in
      List.rev (if String.equal part "" then acc else part :: acc)
    else
      let ch = text.[index] in
      let paren_depth, bracket_depth =
        match ch with
        | '(' -> (paren_depth + 1, bracket_depth)
        | ')' -> (paren_depth - 1, bracket_depth)
        | '[' -> (paren_depth, bracket_depth + 1)
        | ']' -> (paren_depth, bracket_depth - 1)
        | _ -> (paren_depth, bracket_depth)
      in
      if ch = on_char && paren_depth = 0 && bracket_depth = 0 then
        let part = String.sub text current (index - current) |> trim in
        loop (index + 1) paren_depth bracket_depth (index + 1)
          (if String.equal part "" then acc else part :: acc)
      else loop (index + 1) paren_depth bracket_depth current acc
  in
  loop 0 0 0 0 []

let find_matching_left text right_index left_char right_char =
  let rec loop index depth =
    if index < 0 then None
    else
      let ch = text.[index] in
      if ch = right_char then loop (index - 1) (depth + 1)
      else if ch = left_char then
        if depth = 0 then Some index else loop (index - 1) (depth - 1)
      else loop (index - 1) depth
  in
  loop (right_index - 1) 0

let split_array_suffix text =
  let text = trim text in
  let len = String.length text in
  if len = 0 || text.[len - 1] <> ']' then None
  else
    match find_matching_left text (len - 1) '[' ']' with
    | None -> None
    | Some left ->
        let before = String.sub text 0 left |> trim in
        let size =
          String.sub text (left + 1) (len - left - 2) |> trim
        in
        Some (before, size)

let split_call_suffix text =
  let text = trim text in
  let len = String.length text in
  if len = 0 || text.[len - 1] <> ')' then None
  else
    match find_matching_left text (len - 1) '(' ')' with
    | None -> None
    | Some left ->
        let before = String.sub text 0 left |> trim in
        let inside =
          String.sub text (left + 1) (len - left - 2) |> trim
        in
        Some (before, inside)

let remove_qualifiers text =
  let text = remove_gnu_attributes text in
  let text = replace_all text "__restrict" "" in
  let text = replace_all text "__const" "" in
  let text = replace_all text "restrict" "" in
  let text = replace_all text "volatile" "" in
  let text = replace_all text "const" "" in
  let text = replace_all text "_Nonnull" "" in
  let text = replace_all text "_Nullable" "" in
  let text = replace_all text "_Noreturn" "" in
  let text = replace_all text "noreturn" "" in
  text
  |> String.split_on_char ' '
  |> List.filter (fun token -> not (String.equal token ""))
  |> String.concat " "

let has_unsupported_c_syntax text =
  String.contains text '^'
  || contains_substring text "__bsearch_noescape"
  || contains_substring text "noescape"

let rec parse_c_type target_info text =
  let text = text |> remove_qualifiers |> trim in
  if String.equal text "" then failwith "empty C type"
  else
    match split_array_suffix text with
    | Some (before, size_text) when not (String.equal before "") ->
        let inner = parse_c_type target_info before in
        if String.equal size_text "" then CPointer inner
        else CArray (inner, int_of_string size_text)
    | _ -> (
        match split_call_suffix text with
        | Some (before, args_text) ->
            let before = trim before in
            if ends_with ~suffix:"(*)" before then
              let ret_text =
                String.sub before 0 (String.length before - String.length "(*)") |> trim
              in
              let ret = parse_c_type target_info ret_text in
              let params, vararg = parse_param_types target_info args_text in
              CFunction (params, ret, vararg)
            else if not (String.equal before "") then
              let ret = parse_c_type target_info before in
              let params, vararg = parse_param_types target_info args_text in
              CFunction (params, ret, vararg)
            else parse_base_type target_info text
        | None ->
            let len = String.length text in
            if text.[len - 1] = '*' then
              let inner =
                String.sub text 0 (len - 1) |> trim |> parse_c_type target_info
              in
              (match inner with
              | CInt (_, bits) when bits = target_info.char_bits -> CString
              | _ -> CPointer inner)
            else parse_base_type target_info text)

and parse_param_types target_info args_text =
  let args_text = trim args_text in
  if String.equal args_text "" || String.equal args_text "void" then ([], false)
  else
    let items = split_top_level args_text ~on_char:',' in
    let rec loop params vararg = function
      | [] -> (List.rev params, vararg)
      | item :: rest ->
          let item = trim item in
          if String.equal item "..." then loop params true rest
          else loop (parse_c_type target_info item :: params) vararg rest
    in
    loop [] false items

and parse_base_type target_info text =
  let text = strip_tag_prefix text in
  match text with
  | "void" -> CVoid
  | "_Bool" | "bool" -> CInt (Haven_token.Token.Unsigned, 1)
  | "__builtin_va_list" | "__builtin_ms_va_list" -> CPointer CVoid
  | "float" | "double" | "long double" -> CFloat
  | "char" ->
      CInt
        ( (if target_info.char_unsigned then Haven_token.Token.Unsigned else Haven_token.Token.Signed),
          target_info.char_bits )
  | "signed char" -> CInt (Haven_token.Token.Signed, target_info.char_bits)
  | "unsigned char" -> CInt (Haven_token.Token.Unsigned, target_info.char_bits)
  | "short" | "short int" | "signed short" | "signed short int" ->
      CInt (Haven_token.Token.Signed, target_info.short_bits)
  | "unsigned short" | "unsigned short int" ->
      CInt (Haven_token.Token.Unsigned, target_info.short_bits)
  | "int" | "signed" | "signed int" ->
      CInt (Haven_token.Token.Signed, target_info.int_bits)
  | "unsigned" | "unsigned int" ->
      CInt (Haven_token.Token.Unsigned, target_info.int_bits)
  | "long" | "long int" | "signed long" | "signed long int" ->
      CInt (Haven_token.Token.Signed, target_info.long_bits)
  | "unsigned long" | "unsigned long int" ->
      CInt (Haven_token.Token.Unsigned, target_info.long_bits)
  | "long long" | "long long int" | "signed long long" | "signed long long int" ->
      CInt (Haven_token.Token.Signed, target_info.long_long_bits)
  | "unsigned long long" | "unsigned long long int" ->
      CInt (Haven_token.Token.Unsigned, target_info.long_long_bits)
  | other -> CNamed other

let rec c_type_to_haven = function
  | CVoid -> "void"
  | CInt (signedness, bits) ->
      Printf.sprintf "%c%d"
        (match signedness with Haven_token.Token.Signed -> 'i' | Haven_token.Token.Unsigned -> 'u')
        bits
  | CFloat -> "float"
  | CString -> "str"
  | CPointer inner -> Printf.sprintf "%s*" (c_type_to_haven inner)
  | CArray (inner, count) -> Printf.sprintf "%s[%d]" (c_type_to_haven inner) count
  | CFunction (params, ret, true) ->
      Printf.sprintf "VAFunction<(%s) -> %s>"
        (String.concat ", " (List.map c_type_to_haven params))
        (c_type_to_haven ret)
  | CFunction (params, ret, false) ->
      Printf.sprintf "Function<(%s) -> %s>"
        (String.concat ", " (List.map c_type_to_haven params))
        (c_type_to_haven ret)
  | CNamed name -> name

let rec is_renderable_type = function
  | CVoid | CInt _ | CFloat | CString | CNamed _ -> true
  | CPointer (CPointer _) -> false
  | CPointer inner -> is_renderable_type inner
  | CArray (inner, _) -> is_renderable_type inner
  | CFunction (params, ret, _) ->
      is_renderable_type ret && List.for_all is_renderable_type params

let rec collect_named_types acc = function
  | CVoid | CInt _ | CFloat | CString -> acc
  | CPointer inner -> collect_named_types acc inner
  | CArray (inner, _) -> collect_named_types acc inner
  | CFunction (params, ret, _) ->
      List.fold_left collect_named_types (collect_named_types acc ret) params
  | CNamed name -> name :: acc

let decl_defined_names = function
  | CTypeForward name | CTypeStruct (name, _) | CTypeAlias (name, _) -> [ name ]
  | CFunctionDecl _ | CVarDecl _ | CConstDecl _ -> []

let decl_value_defined_names = function
  | CTypeStruct (name, _) | CTypeAlias (name, _) -> [ name ]
  | CTypeForward _ | CFunctionDecl _ | CVarDecl _ | CConstDecl _ -> []

let decl_referenced_names = function
  | CTypeForward _ -> []
  | CTypeStruct (_, fields) ->
      List.fold_left (fun acc (_, ty) -> collect_named_types acc ty) [] fields
  | CTypeAlias (_, ty) -> collect_named_types [] ty
  | CFunctionDecl { params; return_type; _ } ->
      List.fold_left (fun acc (_, ty) -> collect_named_types acc ty)
        (collect_named_types [] return_type) params
  | CVarDecl { ty; _ } -> collect_named_types [] ty
  | CConstDecl { ty; _ } -> collect_named_types [] ty

let rec collect_value_named_types acc ?(behind_pointer = false) = function
  | CVoid | CInt _ | CFloat | CString -> acc
  | CPointer inner -> collect_value_named_types acc ~behind_pointer:true inner
  | CArray (inner, _) -> collect_value_named_types acc ~behind_pointer inner
  | CFunction (params, ret, _) ->
      List.fold_left
        (fun acc ty -> collect_value_named_types acc ~behind_pointer ty)
        (collect_value_named_types acc ~behind_pointer ret)
        params
  | CNamed name -> if behind_pointer then acc else name :: acc

let decl_value_referenced_names = function
  | CTypeForward _ -> []
  | CTypeStruct (_, fields) ->
      List.fold_left (fun acc (_, ty) -> collect_value_named_types acc ty) [] fields
  | CTypeAlias (_, ty) -> collect_value_named_types [] ty
  | CFunctionDecl { params; return_type; _ } ->
      List.fold_left
        (fun acc (_, ty) -> collect_value_named_types acc ty)
        (collect_value_named_types [] return_type)
        params
  | CVarDecl { ty; _ } -> collect_value_named_types [] ty
  | CConstDecl { ty; _ } -> collect_value_named_types [] ty

let escape_string text =
  let buf = Buffer.create (String.length text) in
  String.iter
    (function
      | '\\' -> Buffer.add_string buf "\\\\"
      | '"' -> Buffer.add_string buf "\\\""
      | '\n' -> Buffer.add_string buf "\\n"
      | '\r' -> Buffer.add_string buf "\\r"
      | '\t' -> Buffer.add_string buf "\\t"
      | ch -> Buffer.add_char buf ch)
    text;
  Buffer.contents buf

let render_decl = function
  | CTypeForward name -> Printf.sprintf "type %s;" name
  | CTypeStruct (name, fields) ->
      let rendered_fields =
        fields
        |> List.map (fun (field_name, ty) -> Printf.sprintf "  %s %s;" (c_type_to_haven ty) field_name)
        |> String.concat "\n"
      in
      Printf.sprintf "type %s = struct {\n%s\n};" name rendered_fields
  | CTypeAlias (name, ty) -> Printf.sprintf "type %s = %s;" name (c_type_to_haven ty)
  | CFunctionDecl { name; params; return_type; vararg } ->
      let fixed =
        params
        |> List.map (fun (param_name, ty) -> Printf.sprintf "%s %s" (c_type_to_haven ty) param_name)
      in
      let params =
        if vararg then
          match fixed with
          | [] -> "*"
          | _ -> String.concat ", " fixed ^ ", *"
        else String.concat ", " fixed
      in
      Printf.sprintf "pub impure fn %s(%s) -> %s;" name params (c_type_to_haven return_type)
  | CVarDecl { name; ty } -> Printf.sprintf "pub state %s %s;" (c_type_to_haven ty) name
  | CConstDecl { name; ty; value } -> Printf.sprintf "pub data %s %s = %s;" (c_type_to_haven ty) name value

let parse_constant_value json =
  let rec loop node =
    match yojson_string_opt "value" node with
    | Some value -> Some value
    | None ->
        let rec inner = function
          | [] -> None
          | child :: rest -> (
              match loop child with Some value -> Some value | None -> inner rest)
        in
        inner (yojson_list "inner" node)
  in
  loop json

let field_decls target_info fields =
  let rec loop acc = function
    | [] -> Some (List.rev acc)
    | field :: rest ->
        let name = node_name field in
        let ty_text = Option.value ~default:"" (qual_type field) in
        if should_skip_exported_name name || not (is_valid_identifier name) then
          None
        else if has_unsupported_c_syntax ty_text then
          None
        else
          try
            let ty = parse_c_type target_info ty_text in
            if is_renderable_type ty then loop ((name, ty) :: acc) rest else None
          with Failure _ -> None
  in
  loop [] fields

let record_is_union json qual =
  match yojson_string_opt "tagUsed" json with
  | Some "union" -> true
  | _ -> starts_with ~prefix:"union " qual

let make_struct_decl target_info ~name json qual =
  let fields =
    yojson_list "inner" json
    |> List.filter (fun child -> String.equal (node_kind child) "FieldDecl")
  in
  if fields = [] then Some (CTypeForward name)
  else if record_is_union json qual then
    Some (CTypeForward name)
  else
    match field_decls target_info fields with
    | Some rendered_fields -> Some (CTypeStruct (name, rendered_fields))
    | None -> Some (CTypeForward name)

let enum_decls target_info name json =
  let underlying = CInt (Haven_token.Token.Signed, target_info.int_bits) in
  let constants =
    yojson_list "inner" json
    |> List.filter (fun child -> String.equal (node_kind child) "EnumConstantDecl")
    |> List.filter_map (fun constant ->
           let constant_name = node_name constant in
           if should_skip_exported_name constant_name || not (is_valid_identifier constant_name) then
             None
           else
             match parse_constant_value constant with
             | Some value -> Some (CConstDecl { name = constant_name; ty = underlying; value })
             | None -> None)
  in
  CTypeAlias (name, underlying) :: constants

let typedef_owned_tag json =
  let rec find_owned_tag = function
    | [] -> None
    | child :: rest -> (
        match yojson_member_opt "ownedTagDecl" child with Some tag -> Some tag | None -> find_owned_tag rest)
  in
  find_owned_tag (yojson_list "inner" json)

let decls_of_node target_info node_by_id json =
  let kind = node_kind json in
  let name = node_name json in
  if yojson_bool "isImplicit" json then []
  else
    match kind with
    | "FunctionDecl" ->
        if should_skip_exported_name name || not (is_valid_identifier name) then []
        else (
          try
            let ty_text = Option.value ~default:"" (qual_type json) in
            if has_unsupported_c_syntax ty_text then []
            else
              match parse_c_type target_info ty_text with
            | CFunction (param_types, return_type, vararg) ->
                let param_nodes =
                  yojson_list "inner" json
                  |> List.filter (fun child -> String.equal (node_kind child) "ParmVarDecl")
                in
                let params =
                  List.mapi
                    (fun index ty ->
                      let param_name =
                        match List.nth_opt param_nodes index with
                        | Some param ->
                            let name = node_name param in
                            if String.equal name "" then Printf.sprintf "p%d" index else name
                        | None -> Printf.sprintf "p%d" index
                      in
                      (param_name, ty))
                    param_types
                in
                if List.exists (fun (param_name, _) -> not (is_valid_identifier param_name)) params then (
                  [])
                else if is_renderable_type return_type && List.for_all (fun (_, ty) -> is_renderable_type ty) params then
                  [ CFunctionDecl { name; params; return_type; vararg } ]
                else []
            | _ -> []
          with Failure _ -> [])
    | "VarDecl" ->
        if should_skip_exported_name name || not (is_valid_identifier name) then []
        else (
          try
            let ty_text = Option.value ~default:"" (qual_type json) in
            if has_unsupported_c_syntax ty_text then []
            else
              let ty = parse_c_type target_info ty_text in
            if is_renderable_type ty then [ CVarDecl { name; ty } ] else []
          with Failure _ -> [])
    | "TypedefDecl" ->
        if String.equal name "" || not (is_valid_identifier name) then []
        else (
          match typedef_owned_tag json with
          | Some tag -> (
              let tag =
                match Hashtbl.find_opt node_by_id (node_id tag) with Some node -> node | None -> tag
              in
              let tag_kind = node_kind tag in
              let typedef_qual = Option.value ~default:"" (qual_type json) in
              match tag_kind with
              | "RecordDecl" -> (
                  match make_struct_decl target_info ~name tag typedef_qual with
                  | Some decl -> [ decl ]
                  | None -> [])
              | "EnumDecl" -> enum_decls target_info name tag
              | _ -> [])
          | None ->
              let typedef_qual = Option.value ~default:"" (qual_type json) in
              let stripped = strip_tag_prefix typedef_qual in
              if String.equal stripped name then []
              else if has_unsupported_c_syntax typedef_qual then []
              else
                try
                  let ty = parse_c_type target_info typedef_qual in
                  if is_renderable_type ty then [ CTypeAlias (name, ty) ] else []
                with Failure _ -> [])
    | "RecordDecl" ->
        if String.equal name "" || not (is_valid_identifier name) then []
        else (
          let qual = Printf.sprintf "struct %s" name in
          match make_struct_decl target_info ~name json qual with
          | Some decl -> [ decl ]
          | None -> [])
    | "EnumDecl" ->
        if String.equal name "" || not (is_valid_identifier name) then []
        else enum_decls target_info name json
    | _ -> []

let render_source decls =
  let defined_names =
    decls |> List.concat_map decl_defined_names |> List.sort_uniq String.compare
  in
  let referenced_names =
    decls |> List.concat_map decl_referenced_names |> List.sort_uniq String.compare
  in
  let forward_decls =
    referenced_names
    |> List.filter (fun name -> not (List.mem name defined_names) && is_valid_identifier name)
    |> List.map (fun name -> CTypeForward name)
  in
  String.concat "\n\n" (List.map render_decl (forward_decls @ decls))

let prune_unlowerable_decls decls =
  let rec loop decls =
    let value_defined_names =
      decls |> List.concat_map decl_value_defined_names |> List.sort_uniq String.compare
    in
    let filtered =
      decls
      |> List.filter (fun decl ->
             decl_value_referenced_names decl
             |> List.for_all (fun name -> List.mem name value_defined_names))
    in
    if List.length filtered = List.length decls then decls else loop filtered
  in
  loop decls

let include_args ?sysroot search_dirs current_file =
  let current_dir = Filename.dirname current_file in
  let dirs =
    current_dir
    :: List.filter (fun dir -> not (String.equal dir current_dir)) search_dirs
  in
  let include_dirs = List.concat_map (fun dir -> [ "-I"; dir ]) dirs in
  match sysroot with
  | Some path -> "-isysroot" :: path :: include_dirs
  | None -> include_dirs

let top_level_nodes ast =
  let nodes = yojson_list "inner" ast in
  let rec drop_prelude = function
    | [] -> []
    | node :: rest ->
        if Option.is_some (loc_file node) then node :: rest else drop_prelude rest
  in
  drop_prelude nodes

let expand_header ~search_dirs ~sysroot ~current_file ~header ~loc =
  let diagnostics_rev = ref [] in
  match load_target_info () with
  | Error message ->
      add_diagnostic diagnostics_rev loc message;
      { decls = []; diagnostics = List.rev !diagnostics_rev }
  | Ok target_info ->
      let wrapper = Filename.temp_file "haven-cimport" ".c" in
      write_file wrapper (Printf.sprintf "#include \"%s\"\n" (escape_string header));
      let command =
        [
          "-Xclang";
          "-ast-dump=json";
          "-fsyntax-only";
          "-x";
          "c";
        ]
        @ include_args ?sysroot search_dirs current_file
        @ [ wrapper ]
      in
      let result = run_command_capture ~prog:"clang" ~args:command () in
      Sys.remove wrapper;
      (match result.status with
      | Unix.WEXITED 0 -> (
          try
            let ast = Yojson.from_string result.stdout in
            let nodes = top_level_nodes ast in
            let node_by_id : (string, Yojson.t) Hashtbl.t = Hashtbl.create 128 in
            List.iter
              (fun node ->
                let id = node_id node in
                if not (String.equal id "") then Hashtbl.replace node_by_id id node)
              nodes;
            let table : (string, int * c_decl) Hashtbl.t = Hashtbl.create 64 in
            let remember key order decl = Hashtbl.replace table key (order, decl) in
            nodes
            |> List.iteri (fun index node ->
                   List.iter
                     (fun decl ->
                       let key =
                         match decl with
                         | CTypeForward name -> "type-forward:" ^ name
                         | CTypeStruct (name, _) -> "type-struct:" ^ name
                         | CTypeAlias (name, _) -> "type-alias:" ^ name
                         | CFunctionDecl { name; _ } -> "fn:" ^ name
                         | CVarDecl { name; _ } -> "var:" ^ name
                         | CConstDecl { name; _ } -> "const:" ^ name
                       in
                       remember key index decl)
                     (decls_of_node target_info node_by_id node));
            let decls =
              Hashtbl.to_seq_values table
              |> List.of_seq
              |> List.sort (fun (left, _) (right, _) -> Int.compare left right)
              |> List.map snd
              |> prune_unlowerable_decls
            in
            let source = render_source decls in
            if Sys.getenv_opt "HAVEN_CIMPORT_DEBUG" = Some "1" then
              prerr_endline
                (Printf.sprintf "[haven-cimport] %s\n%s" header source);
            if String.equal source "" then { decls = []; diagnostics = List.rev !diagnostics_rev }
            else
              let parsed =
                Parser.parse_string ~filename:(Printf.sprintf "<cimport:%s>" header) source
              in
              { decls = parsed.program.value.decls; diagnostics = List.rev !diagnostics_rev }
          with
          | Failure message ->
              add_diagnostic diagnostics_rev loc
                (Printf.sprintf "failed to parse generated Haven for cimport %S: %s" header message);
              { decls = []; diagnostics = List.rev !diagnostics_rev }
          | exn ->
              let message = Printexc.to_string exn in
              add_diagnostic diagnostics_rev loc
                (Printf.sprintf "failed to decode clang JSON for cimport %S: %s" header message);
              { decls = []; diagnostics = List.rev !diagnostics_rev })
      | Unix.WEXITED code ->
          add_diagnostic diagnostics_rev loc
            (Printf.sprintf "clang failed while expanding cimport %S with exit code %d\n%s" header
               code result.stderr);
          { decls = []; diagnostics = List.rev !diagnostics_rev }
      | Unix.WSIGNALED signal ->
          add_diagnostic diagnostics_rev loc
            (Printf.sprintf "clang terminated with signal %d while expanding cimport %S" signal header);
          { decls = []; diagnostics = List.rev !diagnostics_rev }
      | Unix.WSTOPPED signal ->
          add_diagnostic diagnostics_rev loc
            (Printf.sprintf "clang stopped with signal %d while expanding cimport %S" signal header);
          { decls = []; diagnostics = List.rev !diagnostics_rev })
