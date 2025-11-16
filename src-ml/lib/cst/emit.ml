(* Pretty-printer for actual source code. See pretty.ml for a debug emission *)
open Format
open Cst
open Haven_token.Token

let unwrap (n : _ Cst.node) = n.value

let indent_size = 4

let spaces level = String.make (level * indent_size) ' '

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

let rec emit_expression ?(ctx_prec = 0) ~indent fmt expr =
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
        (emit_expression ~ctx_prec:prec ~indent)
        b.left (binary_op_string b.op)
        (emit_expression ~ctx_prec:(prec + 1) ~indent)
        b.right
  | Unary u ->
      let u = unwrap u in
      let symbol =
        match u.op with Not -> "!" | Negate -> "-" | Complement -> "~"
      in
      fprintf fmt "%s%a" symbol
        (emit_expression ~ctx_prec:unary_precedence ~indent)
        u.inner
  | Literal lit -> emit_literal fmt lit
  | Block block -> emit_block ~indent fmt block
  | ParenthesizedExpression e ->
      fprintf fmt "(%a)" (emit_expression ~ctx_prec:0 ~indent) e
  | Identifier s -> emit_identifier fmt s
  | Initializer i -> emit_initializer ~indent fmt i.value.exprs
  | As a_node ->
      let a = unwrap a_node in
      fprintf fmt "as<%a>(%a)" emit_type a.target_type
        (emit_expression ~ctx_prec:0 ~indent)
        a.inner
  | SizeExpr e -> fprintf fmt "size(%a)" (emit_expression ~ctx_prec:0 ~indent) e
  | SizeType t -> fprintf fmt "size<%a>" emit_type t
  | Nil -> fprintf fmt "nil"
  | If i -> emit_if_expr ~indent fmt i
  | Match m -> emit_match_expr ~indent fmt m
  | BoxExpr e ->
      fprintf fmt "box %a" (emit_expression ~ctx_prec:unary_precedence ~indent) e
  | BoxType t -> fprintf fmt "box %a" emit_type t
  | Unbox e ->
      fprintf fmt "unbox %a"
        (emit_expression ~ctx_prec:unary_precedence ~indent)
        e
  | Ref e ->
      fprintf fmt "ref %a" (emit_expression ~ctx_prec:unary_precedence ~indent) e
  | Load e ->
      fprintf fmt "load %a" (emit_expression ~ctx_prec:unary_precedence ~indent)
        e
  | Call c_node -> emit_call ~indent fmt c_node
  | Index i -> emit_index ~indent fmt i
  | Field f_node -> emit_field ~indent fmt f_node);
  if needs_paren then fprintf fmt ")"

and emit_vec_literal fmt (vec : vec_literal) =
  let vec = unwrap vec in
  fprintf fmt "Vec<%a>"
    (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt ", ")
       (emit_expression ~ctx_prec:0 ~indent:0))
    vec.elements

and emit_mat_literal fmt (mat : mat_literal) =
  let mat = unwrap mat in
  fprintf fmt "Mat<%a>"
    (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt ", ") emit_vec_literal)
    mat.rows

and emit_binary_literal fmt i =
  let rec to_binary acc n =
    if n < 2 then string_of_int n ^ acc
    else to_binary (string_of_int (n mod 2) ^ acc) (n / 2)
  in
  let prefix, n = if i < 0 then ("-", abs i) else ("", i) in
  fprintf fmt "0b%s%s" prefix (to_binary "" n)

and emit_literal fmt lit =
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
        if String.contains s '.' then (
          let len = String.length s in
          let rec trim i =
            if i <= 0 then s
            else (
              match s.[i - 1] with
              | '0' -> trim (i - 1)
              | '.' -> String.sub s 0 (i - 1)
              | _ -> String.sub s 0 i)
          in
          trim len)
        else s
      in
      let rendered =
        if String.contains trimmed '.' then trimmed else trimmed ^ ".0"
      in
      fprintf fmt "%s" rendered
  | String s -> fprintf fmt "%S" s
  | Char c -> fprintf fmt "'%s'" (Char.escaped c)
  | Matrix m -> emit_mat_literal fmt m
  | Vector v -> emit_vec_literal fmt v
  | Enum e -> emit_enum_literal fmt e

and emit_enum_literal fmt e =
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
      fprintf fmt "%a[%a]" emit_type a.value.element emit_literal a.value.count
  | TemplatedType t ->
      let t = unwrap t in
      fprintf fmt "%a::<%a>" emit_identifier t.outer emit_type_list t.inner

and emit_type_list fmt l =
  (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt ",@ ") emit_type) fmt l

and emit_call ~indent fmt c_node =
  let c = unwrap c_node in
  fprintf fmt "%a(%a)"
    (emit_expression ~ctx_prec:postfix_precedence ~indent)
    c.target
    (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt ", ")
       (emit_expression ~ctx_prec:0 ~indent))
    c.params

and emit_index ~indent fmt i_node =
  let i = unwrap i_node in
  fprintf fmt "%a[%a]"
    (emit_expression ~ctx_prec:postfix_precedence ~indent)
    i.target (emit_expression ~ctx_prec:0 ~indent) i.index

and emit_field ~indent fmt f_node =
  let f = unwrap f_node in
  let sep = if f.arrow then "->" else "." in
  fprintf fmt "%a%s%a"
    (emit_expression ~ctx_prec:postfix_precedence ~indent)
    f.target sep emit_identifier f.field

and emit_match_expr ~indent fmt m =
  let m = unwrap m in
  fprintf fmt "match %a {" (emit_expression ~ctx_prec:0 ~indent) m.expr;
  List.iteri
    (fun idx arm ->
      fprintf fmt "\n%s" (spaces (indent + 1));
      emit_match_arm ~indent:(indent + 1) fmt arm;
      if idx < List.length m.arms - 1 then fprintf fmt ",")
    m.arms;
  fprintf fmt "\n%s}" (spaces indent)

and emit_match_arm ~indent fmt arm =
  let arm = unwrap arm in
  fprintf fmt "%a => %a" emit_pattern arm.pattern
    (emit_expression ~ctx_prec:0 ~indent)
    arm.expr

and emit_pattern_binding fmt (b : pattern_binding) =
  match b.value with
  | BindingIgnored -> fprintf fmt "_"
  | BindingNamed s -> emit_identifier fmt s

and emit_pattern_binding_list fmt (bindings : pattern_binding list) =
  (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt ",@ ")
     emit_pattern_binding)
    fmt bindings

and emit_enum_pattern fmt (pat : pattern_enum) =
  let pat = unwrap pat in
  match pat.enum_name with
  | Some name ->
      fprintf fmt "%a::%a%a" emit_identifier name emit_identifier pat.enum_variant
        emit_pattern_binding_group pat.binding
  | None ->
      fprintf fmt "%a%a" emit_identifier pat.enum_variant emit_pattern_binding_group
        pat.binding

and emit_pattern_binding_group fmt bindings =
  match bindings with
  | [] -> ()
  | bindings ->
      fprintf fmt "(%a)" emit_pattern_binding_list bindings

and emit_pattern fmt pat =
  match pat.value with
  | PatternDefault -> fprintf fmt "_"
  | PatternLiteral lit -> emit_literal fmt lit
  | PatternEnum e -> emit_enum_pattern fmt e

and emit_if_expr ~indent fmt i =
  let i = unwrap i in
  fprintf fmt "if %a %a%a" (emit_expression ~ctx_prec:0 ~indent) i.cond
    (emit_block ~indent) i.then_block (emit_else_block ~indent) i.else_block

and emit_else_block ~indent fmt blk =
  match blk with
  | None -> ()
  | Some (ElseIf e) -> fprintf fmt " else %a" (emit_if_expr ~indent) e
  | Some (Else b) -> fprintf fmt " else %a" (emit_block ~indent) b

and emit_initializer ~indent fmt exprs =
  match exprs with
  | [] -> fprintf fmt "{}"
  | [ single ] ->
      fprintf fmt "{%a}" (emit_expression ~ctx_prec:0 ~indent) single
  | _ ->
      fprintf fmt "{";
      List.iteri
        (fun idx expr ->
          fprintf fmt "\n%s%a" (spaces (indent + 1))
            (emit_expression ~ctx_prec:0 ~indent:(indent + 1))
            expr;
          if idx < List.length exprs - 1 then fprintf fmt ",")
        exprs;
      fprintf fmt "\n%s}" (spaces indent)

and emit_statement ~indent fmt stmt =
  match stmt.value with
  | Expression e ->
      fprintf fmt "%a;" (emit_expression ~ctx_prec:0 ~indent:indent) e
  | Let s ->
      let s = unwrap s in
      fprintf fmt "let ";
      if s.mut then fprintf fmt "mut ";
      (pp_print_option
         (fun fmt ty -> fprintf fmt "%a " emit_type ty))
        fmt s.ty;
      emit_identifier fmt s.name;
      fprintf fmt " = %a;" (emit_expression ~ctx_prec:0 ~indent:indent) s.init_expr
  | Return (Some e) ->
      fprintf fmt "ret %a;" (emit_expression ~ctx_prec:0 ~indent:indent) e
  | Return None -> fprintf fmt "ret;"
  | Defer e ->
      fprintf fmt "defer %a;" (emit_expression ~ctx_prec:0 ~indent:indent) e
  | Iter i_node ->
      let i = unwrap i_node in
      fprintf fmt "iter %a:%a"
        (emit_expression ~ctx_prec:0 ~indent:indent)
        i.range.value.range_start
        (emit_expression ~ctx_prec:0 ~indent:indent)
        i.range.value.range_end;
      (pp_print_option
        (fun fmt incr ->
           fprintf fmt ":%a" (emit_expression ~ctx_prec:0 ~indent:indent) incr))
        fmt i.range.value.range_incr;
      fprintf fmt " %a" (emit_identifier) i.var;
      fprintf fmt " %a;" (emit_block ~indent) i.body
  | While w_node ->
      let w = unwrap w_node in
      fprintf fmt "while %a %a"
        (emit_expression ~ctx_prec:0 ~indent:indent)
        w.cond (emit_block ~indent) w.body;
      fprintf fmt ";"
  | Break -> fprintf fmt "break;"
  | Continue -> fprintf fmt "continue;"
  | Empty -> fprintf fmt ";"

and emit_block_item ~indent fmt item =
  match item.value with
  | BlockStatement s ->
      fprintf fmt "%s%a" (spaces indent) (emit_statement ~indent) s
  | BlockExpression e ->
      fprintf fmt "%s%a" (spaces indent)
        (emit_expression ~ctx_prec:0 ~indent)
        e

and block_item_kind item =
  match item.value with
  | BlockStatement s -> (
      match s.value with Let _ -> `Let | _ -> `Stmt)
  | BlockExpression _ -> `Expr

and emit_block ~indent fmt block =
  let block = unwrap block in
  match block.items with
  | [] -> fprintf fmt "{}"
  | _ ->
      fprintf fmt "{";
      let rec loop prev_kind = function
        | [] -> ()
        | item :: rest ->
            let kind = block_item_kind item in
            fprintf fmt "\n";
            (match prev_kind with
            | None -> ()
            | Some `Let when kind = `Let -> ()
            | Some `Let when kind = `Expr -> ()
            | Some _ -> fprintf fmt "\n");
            emit_block_item ~indent:(indent + 1) fmt item;
            loop (Some kind) rest
      in
      loop None block.items;
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
  (match i.types with
  | [] -> ()
  | types ->
      fprintf fmt " %a"
        (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt ", ") emit_type)
        types)

let emit_fdecl fmt (decl : function_decl) =
  let decl = unwrap decl in
  fprintf fmt "%s%sfn %s(" (if decl.public then "pub " else "")
    (if decl.impure then "impure " else "")
    decl.name.value;
  emit_params fmt decl.params;
  fprintf fmt ")";
  (pp_print_option (fun fmt ty -> fprintf fmt " -> %a" emit_type ty)) fmt
    decl.return_type;
  (pp_print_option (fun fmt i -> fprintf fmt " %a" emit_intrinsic i)) fmt
    decl.intrinsic;
  match decl.definition with
  | Some block ->
      fprintf fmt " ";
      emit_block ~indent:0 fmt block
  | None -> fprintf fmt ";"

let emit_fdecl_list fmt decls =
  (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt "\n") emit_fdecl) fmt decls

let emit_var_decl fmt (decl : var_decl) =
  let decl = unwrap decl in
  fprintf fmt "%s%s %a %a"
    (if decl.public then "pub " else "")
    (if decl.is_mutable then "state" else "data") emit_type decl.ty
    emit_identifier decl.name;
  (pp_print_option
     (fun fmt expr ->
       fprintf fmt " = %a" (emit_expression ~ctx_prec:0 ~indent:0) expr))
    fmt decl.init_expr;
  fprintf fmt ";"

let emit_struct_field fmt (f : struct_field) =
  let f = unwrap f in
  fprintf fmt "%a %a;" emit_type f.ty emit_identifier f.name

let emit_struct_decl fmt (d : struct_decl) =
  let d = unwrap d in
  fprintf fmt "struct {";
  List.iter
    (fun field ->
      fprintf fmt "\n%s%a" (spaces 1) emit_struct_field field)
    d.fields;
  fprintf fmt "\n}"

let emit_enum_variant fmt (v : enum_variant) =
  let v = unwrap v in
  fprintf fmt "%a%a" emit_identifier v.name
    (pp_print_option
       (fun fmt inner -> fprintf fmt "(%a)" emit_type inner))
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

let emit_foreign fmt (f : foreign) =
  let f = unwrap f in
  fprintf fmt "foreign %a {\n" emit_string_lit f.lib;
  List.iteri
    (fun idx decl ->
      emit_fdecl fmt decl;
      if idx < List.length f.decls - 1 then fprintf fmt "\n")
    f.decls;
  fprintf fmt "\n}"

let emit_decl fmt decl =
  match decl.value with
  | FDecl d -> emit_fdecl fmt d
  | TDecl t -> emit_type_decl fmt t
  | VDecl v -> emit_var_decl fmt v
  | Import i -> fprintf fmt "import %a;" emit_string_lit i
  | CImport i -> fprintf fmt "cimport %a;" emit_string_lit i
  | Foreign f -> emit_foreign fmt f

let is_import decl =
  match decl.value with Import _ | CImport _ -> true | _ -> false

let emit_program fmt (program : program) =
  Format.pp_set_margin fmt 120;
  let rec loop = function
    | [] -> ()
    | [ last ] ->
        emit_decl fmt last;
        pp_print_newline fmt ()
    | decl :: rest ->
        emit_decl fmt decl;
        if is_import decl && is_import (List.hd rest) then fprintf fmt "\n"
        else fprintf fmt "\n\n";
        loop rest
  in
  loop program.value.decls
