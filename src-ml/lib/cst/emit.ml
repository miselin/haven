(* Pretty-printer for actual source code with comment insertion. *)
open Format
open Cst
open Haven_token.Token
open Haven_core
module Raw = Cst.Raw

let unwrap (n : _ Cst.node) = n.value
let indent_size = 4
let spaces level = String.make (level * indent_size) ' '

type comment_queue = Cst.comment list ref

let empty_comments : comment_queue = ref []

let flush_comments ?inline_line ~indent ~limit_pos queue fmt =
  let rec loop () =
    match !queue with
    | { startp; tok; _ } :: rest
      when startp.Lexing.pos_cnum <= limit_pos
           ||
           match inline_line with
           | Some line when startp.Lexing.pos_lnum = line -> true
           | _ -> false ->
        queue := rest;
        let text =
          match tok.Raw.tok with
          | Raw.Trivia (Lexer.Comment { text; _ }) -> Some text
          | _ -> None
        in
        (match text with
        | None -> ()
        | Some text -> (
            match inline_line with
            | Some line when startp.Lexing.pos_lnum = line ->
                fprintf fmt " %s" text
            | _ -> fprintf fmt "%s%s\n" (spaces indent) text));
        loop ()
    | _ -> ()
  in
  loop ()

let emit_comments ~comments ~indent ~(loc : Loc.t) ~kind ?inline
    ?(separate = false) fmt =
  match kind with
  | `Leading ->
      flush_comments ~indent ~limit_pos:loc.start_pos.pos_cnum comments fmt
  | `Trailing ->
      let has_pending =
        match !comments with
        | { startp; _ } :: _ -> startp.pos_cnum <= loc.end_pos.pos_cnum
        | [] -> false
      in
      if has_pending then (
        (match (separate, inline) with
        | true, _ -> fprintf fmt "\n"
        | false, None -> fprintf fmt "\n"
        | _ -> ());
        flush_comments ~indent ~limit_pos:loc.end_pos.pos_cnum
          ?inline_line:inline comments fmt)

let flush_inline_on_line ~line queue fmt =
  let rec loop kept =
    match !queue with
    | { startp; tok; _ } :: rest when startp.Lexing.pos_lnum = line ->
        queue := rest;
        (match tok.Raw.tok with
        | Raw.Trivia (Lexer.Comment { text; _ }) -> fprintf fmt " %s" text
        | _ -> ());
        loop kept
    | c :: rest ->
        queue := rest;
        loop (c :: kept)
    | [] -> queue := List.rev kept
  in
  loop []

let emit_identifier fmt (id : identifier) = fprintf fmt "%s" id.value
let emit_string_lit fmt (s : string node) = fprintf fmt "%S" s.value

let binary_op_string = function
  | Add -> "+"
  | Subtract -> "-"
  | Multiply -> "*"
  | Divide -> "/"
  | Modulo -> "%%"
  | LeftShift -> "<<"
  | RightShift -> ">>"
  | IsEqual -> "=="
  | NotEqual -> "!="
  | LessThan -> "<"
  | LessThanOrEqual -> "<="
  | GreaterThan -> ">"
  | GreaterThanOrEqual -> ">="
  | BitwiseAnd -> "&"
  | BitwiseXor -> "^"
  | BitwiseOr -> "|"
  | LogicAnd -> "&&"
  | LogicOr -> "||"
  | Assign -> "="
  | Mutate -> ":="

let binary_precedence = function
  | Assign | Mutate -> 1
  | LogicOr -> 2
  | LogicAnd -> 3
  | BitwiseOr -> 4
  | BitwiseXor -> 5
  | BitwiseAnd -> 6
  | IsEqual | NotEqual -> 7
  | LessThan | LessThanOrEqual | GreaterThan | GreaterThanOrEqual -> 8
  | LeftShift | RightShift -> 9
  | Add | Subtract -> 10
  | Multiply | Divide | Modulo -> 11

let unary_precedence = 12
let postfix_precedence = 13
let primary_precedence = 14

let rec emit_expression ?(ctx_prec = 0) ~indent ~comments fmt expr =
  flush_comments ~indent ~limit_pos:expr.loc.start_pos.pos_cnum comments fmt;
  let self_prec =
    match expr.value with
    | Binary b -> binary_precedence (unwrap b).op
    | Unary _ | BoxExpr _ | BoxType _ | Unbox _ | Ref _ | Load _ ->
        unary_precedence
    | Call _ | Index _ | Field _ -> postfix_precedence
    | _ -> primary_precedence
  in
  let needs_paren = self_prec < ctx_prec in
  if needs_paren then fprintf fmt "(";
  (match expr.value with
  | Binary b_node ->
      let b = unwrap b_node in
      let prec = binary_precedence b.op in
      fprintf fmt "%a %s %a"
        (emit_expression ~ctx_prec:prec ~indent ~comments)
        b.left (binary_op_string b.op)
        (emit_expression ~ctx_prec:(prec + 1) ~indent ~comments)
        b.right
  | Unary u ->
      let u = unwrap u in
      let symbol =
        match u.op with Not -> "!" | Negate -> "-" | Complement -> "~"
      in
      fprintf fmt "%s%a" symbol
        (emit_expression ~ctx_prec:unary_precedence ~indent ~comments)
        u.inner
  | Literal lit -> emit_literal ~comments fmt lit
  | Block block -> emit_block ~indent ~comments fmt block
  | ParenthesizedExpression e ->
      fprintf fmt "(%a)" (emit_expression ~ctx_prec:0 ~indent ~comments) e
  | Identifier s -> emit_identifier fmt s
  | Initializer i -> emit_initializer ~indent ~comments fmt i.value.exprs
  | As a_node ->
      let a = unwrap a_node in
      fprintf fmt "as<%a>(%a)" emit_type a.target_type
        (emit_expression ~ctx_prec:0 ~indent ~comments)
        a.inner
  | SizeExpr e ->
      fprintf fmt "size(%a)" (emit_expression ~ctx_prec:0 ~indent ~comments) e
  | SizeType t -> fprintf fmt "size<%a>" emit_type t
  | Nil -> fprintf fmt "nil"
  | If i -> emit_if_expr ~indent ~comments fmt i
  | Match m -> emit_match_expr ~indent ~comments fmt m
  | BoxExpr e ->
      fprintf fmt "box %a"
        (emit_expression ~ctx_prec:unary_precedence ~indent ~comments)
        e
  | BoxType t -> fprintf fmt "box %a" emit_type t
  | Unbox e ->
      fprintf fmt "unbox %a"
        (emit_expression ~ctx_prec:unary_precedence ~indent ~comments)
        e
  | Ref e ->
      fprintf fmt "ref %a"
        (emit_expression ~ctx_prec:unary_precedence ~indent ~comments)
        e
  | Load e ->
      fprintf fmt "load %a"
        (emit_expression ~ctx_prec:unary_precedence ~indent ~comments)
        e
  | Call c_node -> emit_call ~indent ~comments fmt c_node
  | Index i -> emit_index ~indent ~comments fmt i
  | Field f_node -> emit_field ~indent ~comments fmt f_node);
  if needs_paren then fprintf fmt ")"

and emit_vec_literal ~comments fmt (vec : vec_literal) =
  let vec = unwrap vec in
  fprintf fmt "Vec<%a>"
    (pp_print_list
       ~pp_sep:(fun fmt () -> fprintf fmt ", ")
       (fun fmt e -> emit_expression ~ctx_prec:0 ~indent:0 ~comments fmt e))
    vec.elements

and emit_mat_literal ~comments fmt (mat : mat_literal) =
  let mat = unwrap mat in
  fprintf fmt "Mat<%a>"
    (pp_print_list
       ~pp_sep:(fun fmt () -> fprintf fmt ", ")
       (emit_vec_literal ~comments))
    mat.rows

and emit_binary_literal fmt i =
  let rec to_binary acc n =
    if n < 2 then string_of_int n ^ acc
    else to_binary (string_of_int (n mod 2) ^ acc) (n / 2)
  in
  let prefix, n = if i < 0 then ("-", abs i) else ("", i) in
  fprintf fmt "0b%s%s" prefix (to_binary "" n)

and emit_literal ?(comments = empty_comments) fmt lit =
  match lit.value with
  | HexInt i -> fprintf fmt "0x%x" i
  | OctInt i -> fprintf fmt "0o%o" i
  | BinInt i -> emit_binary_literal fmt i
  | DecInt i -> fprintf fmt "%d" i
  | Float f ->
      let is_int = Float.equal (Float.floor f) f in
      let raw =
        if is_int then Printf.sprintf "%.0f" f else Printf.sprintf "%.15g" f
      in
      let s =
        if (String.contains raw 'e' || String.contains raw 'E') && not is_int
        then Printf.sprintf "%.15f" f
        else raw
      in
      let trimmed =
        if String.contains s '.' then
          let len = String.length s in
          let rec trim i =
            if i <= 0 then s
            else
              match s.[i - 1] with
              | '0' -> trim (i - 1)
              | '.' -> String.sub s 0 (i - 1)
              | _ -> String.sub s 0 i
          in
          trim len
        else s
      in
      let rendered =
        if String.contains trimmed '.' then trimmed else trimmed ^ ".0"
      in
      fprintf fmt "%s" rendered
  | String s -> fprintf fmt "%S" s
  | Char c -> fprintf fmt "'%s'" (Char.escaped c)
  | Matrix m -> emit_mat_literal ~comments fmt m
  | Vector v -> emit_vec_literal ~comments fmt v
  | Enum e -> emit_enum_literal ~comments fmt e

and emit_enum_literal ?(comments : comment_queue option) fmt e =
  let _ = comments in
  let e = unwrap e in
  fprintf fmt "%a%a::%a" emit_identifier e.enum_name emit_enum_literal_type_args
    e.types emit_identifier e.enum_variant

and emit_enum_literal_type_args fmt = function
  | [] -> ()
  | types -> fprintf fmt "::<%a>" emit_type_list types

and emit_type fmt ty =
  match ty.value with
  | NumericType n -> fprintf fmt "%s" (numeric_type_to_string n)
  | VecType v -> fprintf fmt "%s" (vec_type_to_string v)
  | MatrixType m -> fprintf fmt "%s" (mat_type_to_string m)
  | FloatType -> fprintf fmt "float"
  | VoidType -> fprintf fmt "void"
  | StringType -> fprintf fmt "str"
  | CustomType c -> emit_identifier fmt c.name
  | CellType t -> fprintf fmt "Cell<%a>" emit_type t
  | FunctionType f_node ->
      let f = unwrap f_node in
      fprintf fmt "%sFunction<(%a) -> %a>"
        (if f.vararg then "VA" else "")
        emit_type_list f.param_types emit_type f.return_type
  | PointerType p -> fprintf fmt "%a *" emit_type p
  | BoxType b -> fprintf fmt "%a^" emit_type b
  | ArrayType a ->
      fprintf fmt "%a[%a]" emit_type a.value.element
        (emit_literal ~comments:empty_comments)
        a.value.count
  | TemplatedType t ->
      let t = unwrap t in
      fprintf fmt "%a::<%a>" emit_identifier t.outer emit_type_list t.inner

and emit_type_list fmt l =
  (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt ",@ ") emit_type) fmt l

and emit_call ~indent ~comments fmt c_node =
  let c = unwrap c_node in
  fprintf fmt "%a(%a)"
    (emit_expression ~ctx_prec:postfix_precedence ~indent ~comments)
    c.target
    (pp_print_list
       ~pp_sep:(fun fmt () -> fprintf fmt ", ")
       (emit_expression ~ctx_prec:0 ~indent ~comments))
    c.params

and emit_index ~indent ~comments fmt i_node =
  let i = unwrap i_node in
  fprintf fmt "%a[%a]"
    (emit_expression ~ctx_prec:postfix_precedence ~indent ~comments)
    i.target
    (emit_expression ~ctx_prec:0 ~indent ~comments)
    i.index

and emit_field ~indent ~comments fmt f_node =
  let f = unwrap f_node in
  let sep = if f.arrow then "->" else "." in
  fprintf fmt "%a%s%a"
    (emit_expression ~ctx_prec:postfix_precedence ~indent ~comments)
    f.target sep emit_identifier f.field

and emit_match_expr ~indent ~comments fmt m =
  let m = unwrap m in
  fprintf fmt "match %a {"
    (emit_expression ~ctx_prec:0 ~indent ~comments)
    m.expr;
  List.iteri
    (fun idx arm ->
      fprintf fmt "\n%s" (spaces (indent + 1));
      emit_match_arm ~indent:(indent + 1) ~comments fmt arm;
      if idx < List.length m.arms - 1 then fprintf fmt ",")
    m.arms;
  fprintf fmt "\n%s}" (spaces indent)

and emit_match_arm ~indent ~comments fmt arm =
  let arm = unwrap arm in
  fprintf fmt "%a => %a" emit_pattern arm.pattern
    (emit_expression ~ctx_prec:0 ~indent ~comments)
    arm.expr

and emit_pattern_binding fmt (b : pattern_binding) =
  match b.value with
  | BindingIgnored -> fprintf fmt "_"
  | BindingNamed s -> emit_identifier fmt s

and emit_pattern_binding_list fmt (bindings : pattern_binding list) =
  (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt ",@ ") emit_pattern_binding)
    fmt bindings

and emit_enum_pattern fmt (pat : pattern_enum) =
  let pat = unwrap pat in
  match pat.enum_name with
  | Some name ->
      fprintf fmt "%a::%a%a" emit_identifier name emit_identifier
        pat.enum_variant emit_pattern_binding_group pat.binding
  | None ->
      fprintf fmt "%a%a" emit_identifier pat.enum_variant
        emit_pattern_binding_group pat.binding

and emit_pattern_binding_group fmt bindings =
  match bindings with
  | [] -> ()
  | bindings -> fprintf fmt "(%a)" emit_pattern_binding_list bindings

and emit_pattern fmt pat =
  match pat.value with
  | PatternDefault -> fprintf fmt "_"
  | PatternLiteral lit -> emit_literal ~comments:empty_comments fmt lit
  | PatternEnum e -> emit_enum_pattern fmt e

and emit_if_expr ~indent ~comments fmt i =
  let i = unwrap i in
  fprintf fmt "if %a %a%a"
    (emit_expression ~ctx_prec:0 ~indent ~comments)
    i.cond
    (emit_block ~indent ~comments)
    i.then_block
    (emit_else_block ~indent ~comments)
    i.else_block

and emit_else_block ~indent ~comments fmt blk =
  match blk with
  | None -> ()
  | Some (ElseIf e) -> fprintf fmt " else %a" (emit_if_expr ~indent ~comments) e
  | Some (Else b) -> fprintf fmt " else %a" (emit_block ~indent ~comments) b

and emit_initializer ~indent ~comments fmt exprs =
  match exprs with
  | [] -> fprintf fmt "{}"
  | [ single ] ->
      fprintf fmt "{%a}" (emit_expression ~ctx_prec:0 ~indent ~comments) single
  | _ ->
      fprintf fmt "{";
      List.iteri
        (fun idx expr ->
          fprintf fmt "\n%s%a"
            (spaces (indent + 1))
            (emit_expression ~ctx_prec:0 ~indent:(indent + 1) ~comments)
            expr;
          if idx < List.length exprs - 1 then fprintf fmt ",")
        exprs;
      fprintf fmt "\n%s}" (spaces indent)

and emit_statement ~indent ~comments fmt stmt =
  flush_comments ~indent ~limit_pos:stmt.loc.start_pos.pos_cnum comments fmt;
  match stmt.value with
  | Expression e ->
      fprintf fmt "%a;" (emit_expression ~ctx_prec:0 ~indent ~comments) e;
      flush_inline_on_line ~line:stmt.loc.start_pos.pos_lnum comments fmt
  | Let s ->
      let s = unwrap s in
      fprintf fmt "let ";
      if s.mut then fprintf fmt "mut ";
      (pp_print_option (fun fmt ty -> fprintf fmt "%a " emit_type ty)) fmt s.ty;
      emit_identifier fmt s.name;
      fprintf fmt " = %a;"
        (emit_expression ~ctx_prec:0 ~indent ~comments)
        s.init_expr
  | Return (Some e) ->
      fprintf fmt "ret %a;" (emit_expression ~ctx_prec:0 ~indent ~comments) e;
      flush_inline_on_line ~line:stmt.loc.start_pos.pos_lnum comments fmt
  | Return None -> fprintf fmt "ret;"
  | Defer e ->
      fprintf fmt "defer %a;" (emit_expression ~ctx_prec:0 ~indent ~comments) e;
      flush_inline_on_line ~line:stmt.loc.start_pos.pos_lnum comments fmt
  | Iter i_node ->
      let i = unwrap i_node in
      fprintf fmt "iter %a:%a"
        (emit_expression ~ctx_prec:0 ~indent ~comments)
        i.range.value.range_start
        (emit_expression ~ctx_prec:0 ~indent ~comments)
        i.range.value.range_end;
      (pp_print_option (fun fmt incr ->
           fprintf fmt ":%a"
             (emit_expression ~ctx_prec:0 ~indent ~comments)
             incr))
        fmt i.range.value.range_incr;
      fprintf fmt " %a" emit_identifier i.var;
      fprintf fmt " %a;" (emit_block ~indent ~comments) i.body
  | While w_node ->
      let w = unwrap w_node in
      fprintf fmt "while %a %a"
        (emit_expression ~ctx_prec:0 ~indent ~comments)
        w.cond
        (emit_block ~indent ~comments)
        w.body;
      fprintf fmt ";"
  | Break -> fprintf fmt "break;"
  | Continue -> fprintf fmt "continue;"
  | Empty -> fprintf fmt ";"

and emit_block_item ~indent ~comments fmt item =
  match item.value with
  | BlockStatement s ->
      fprintf fmt "%s" (spaces indent);
      emit_statement ~indent ~comments fmt s
  | BlockExpression e ->
      fprintf fmt "%s%a" (spaces indent)
        (emit_expression ~ctx_prec:0 ~indent ~comments)
        e;
      flush_inline_on_line ~line:e.loc.start_pos.pos_lnum comments fmt

and block_item_kind item =
  match item.value with
  | BlockStatement s -> ( match s.value with Let _ -> `Let | _ -> `Stmt)
  | BlockExpression _ -> `Block

and emit_block ~indent ~comments fmt block =
  let block_node = block in
  let block = unwrap block in
  match block.items with
  | [] -> fprintf fmt "{}"
  | _ ->
      fprintf fmt "{";
      let rec loop prev_kind = function
        | [] -> prev_kind
        | item :: rest ->
            let kind = block_item_kind item in
            fprintf fmt "\n";
            (match prev_kind with
            | None -> ()
            | Some `Let when kind = `Let -> ()
            | Some `Let when kind = `Expr -> ()
            | Some _ -> fprintf fmt "\n");
            flush_comments ~indent:(indent + 1)
              ~limit_pos:item.loc.start_pos.pos_cnum comments fmt;
            emit_block_item ~indent:(indent + 1) ~comments fmt item;
            loop (Some kind) rest
      in
      let _last_kind = loop None block.items in
      emit_comments ~comments ~indent:(indent + 1) ~loc:block_node.loc
        ~kind:`Trailing ~separate:false fmt;
      fprintf fmt "\n%s}" (spaces indent)

let emit_param fmt (p : param) =
  let p = unwrap p in
  fprintf fmt "%a %a" emit_type p.ty emit_identifier p.name

let emit_params fmt (l : param_list) =
  let l = unwrap l in
  match (l.params, l.vararg) with
  | [], false -> ()
  | [], true -> fprintf fmt "*"
  | params, false ->
      fprintf fmt "%a"
        (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt ", ") emit_param)
        params
  | params, true ->
      fprintf fmt "%a, *"
        (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt ", ") emit_param)
        params

let emit_intrinsic fmt (i : intrinsic) =
  let i = unwrap i in
  fprintf fmt "intrinsic %a" emit_string_lit i.name;
  match i.types with
  | [] -> ()
  | types ->
      fprintf fmt " %a"
        (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt ", ") emit_type)
        types

let emit_fdecl ~comments fmt (decl : function_decl) =
  let decl = unwrap decl in
  fprintf fmt "%s%sfn %s("
    (if decl.public then "pub " else "")
    (if decl.impure then "impure " else "")
    decl.name.value;
  emit_params fmt decl.params;
  fprintf fmt ")";
  (pp_print_option (fun fmt ty -> fprintf fmt " -> %a" emit_type ty))
    fmt decl.return_type;
  (pp_print_option (fun fmt i -> fprintf fmt " %a" emit_intrinsic i))
    fmt decl.intrinsic;
  match decl.definition with
  | Some block ->
      fprintf fmt " ";
      emit_block ~indent:0 ~comments fmt block
  | None -> fprintf fmt ";"

let emit_fdecl_list ~comments fmt decls =
  (pp_print_list
     ~pp_sep:(fun fmt () -> fprintf fmt "\n")
     (emit_fdecl ~comments))
    fmt decls

let emit_var_decl ~comments fmt (decl : var_decl) =
  let decl = unwrap decl in
  fprintf fmt "%s%s %a %a"
    (if decl.public then "pub " else "")
    (if decl.is_mutable then "state" else "data")
    emit_type decl.ty emit_identifier decl.name;
  (pp_print_option (fun fmt expr ->
       fprintf fmt " = %a"
         (emit_expression ~ctx_prec:0 ~indent:0 ~comments)
         expr))
    fmt decl.init_expr;
  fprintf fmt ";"

let emit_struct_field fmt (f : struct_field) =
  let f = unwrap f in
  fprintf fmt "%a %a;" emit_type f.ty emit_identifier f.name

let emit_struct_decl fmt (d : struct_decl) =
  let d = unwrap d in
  fprintf fmt "struct {";
  List.iter
    (fun field -> fprintf fmt "\n%s%a" (spaces 1) emit_struct_field field)
    d.fields;
  fprintf fmt "\n}"

let emit_enum_variant fmt (v : enum_variant) =
  let v = unwrap v in
  fprintf fmt "%a%a" emit_identifier v.name
    (pp_print_option (fun fmt inner -> fprintf fmt "(%a)" emit_type inner))
    v.inner_ty

let emit_enum_decl fmt (d : enum_decl) =
  let d = unwrap d in
  fprintf fmt "enum {";
  List.iteri
    (fun idx variant ->
      fprintf fmt "\n%s%a" (spaces 1) emit_enum_variant variant;
      if idx < List.length d.variants - 1 then fprintf fmt ",")
    d.variants;
  fprintf fmt "\n}"

let emit_type_decl fmt (ty : type_decl) =
  let ty = unwrap ty in
  match ty.data with
  | TypeDeclForward -> fprintf fmt "type %a;" emit_identifier ty.name
  | TypeDeclAlias t ->
      fprintf fmt "type %a = %a;" emit_identifier ty.name emit_type t
  | TypeDeclStruct s ->
      fprintf fmt "type %a = %a;" emit_identifier ty.name emit_struct_decl s
  | TypeDeclEnum e ->
      fprintf fmt "type %a = %a;" emit_identifier ty.name emit_enum_decl e

let emit_foreign ~comments fmt (f : foreign) =
  let f = unwrap f in
  fprintf fmt "foreign %a {\n" emit_string_lit f.lib;
  List.iteri
    (fun idx decl ->
      emit_comments ~comments ~indent:1 ~loc:decl.loc ~kind:`Leading fmt;
      emit_fdecl ~comments fmt decl;
      if idx < List.length f.decls - 1 then fprintf fmt "\n")
    f.decls;
  fprintf fmt "\n}"

let emit_decl ~comments fmt decl =
  emit_comments ~comments ~indent:0 ~loc:decl.loc ~kind:`Leading fmt;
  let needs_separate_trailing =
    match decl.value with
    | FDecl d -> (unwrap d).definition <> None
    | Foreign _ -> true
    | _ -> false
  in
  (match decl.value with
  | FDecl d -> emit_fdecl ~comments fmt d
  | TDecl t -> emit_type_decl fmt t
  | VDecl v -> emit_var_decl ~comments fmt v
  | Import i -> fprintf fmt "import %a;" emit_string_lit i
  | CImport i -> fprintf fmt "cimport %a;" emit_string_lit i
  | Foreign f -> emit_foreign ~comments fmt f);
  emit_comments ~comments ~indent:0 ~loc:decl.loc ~kind:`Trailing
    ~separate:needs_separate_trailing fmt

let is_import decl =
  match decl.value with Import _ | CImport _ -> true | _ -> false

let emit_program fmt (parsed : parsed_program) =
  Format.pp_set_margin fmt 120;
  let items = parsed.items in
  let comments = ref parsed.comments in
  let print_comments_block cs =
    List.iter
      (fun c ->
        flush_comments ~indent:0 ~limit_pos:c.startp.pos_cnum comments fmt;
        flush_comments ~indent:0 ~limit_pos:c.endp.pos_cnum comments fmt)
      cs
  in
  let rec loop = function
    | [] ->
        if !comments <> [] then fprintf fmt "\n";
        flush_comments ~indent:0 ~limit_pos:max_int comments fmt
    | [ Comments cs ] ->
        print_comments_block cs;
        flush_comments ~indent:0 ~limit_pos:max_int comments fmt;
        pp_print_newline fmt ()
    | [ Decl decl ] ->
        emit_decl ~comments fmt decl;
        if !comments <> [] then fprintf fmt "\n";
        flush_comments ~indent:0 ~limit_pos:max_int comments fmt;
        pp_print_newline fmt ()
    | item :: rest ->
        (match item with
        | Decl decl -> emit_decl ~comments fmt decl
        | Comments cs -> print_comments_block cs);
        let next_item = List.hd rest in
        let spacing =
          match (item, next_item) with
          | Decl decl, Decl next when is_import decl && is_import next -> 1
          | Decl _, Decl _ -> 2
          | Comments _, Decl _ -> 0
          | Decl _, Comments _ -> 0
          | Comments _, Comments _ -> 1
        in
        fprintf fmt "%s" (String.make spacing '\n');
        loop rest
  in
  loop items

let emit_program_to_string (parsed : parsed_program) =
  let b = Buffer.create 256 in
  let fmt = Format.formatter_of_buffer b in
  emit_program fmt parsed;
  Buffer.contents b
