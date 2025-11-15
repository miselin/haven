open Format
open Cst
open Haven_token

let pp_unary_op fmt op =
  match op with
  | Not -> fprintf fmt "!"
  | Negate -> fprintf fmt "-"
  | Complement -> fprintf fmt "~"

let pp_binary_op fmt op =
  match op with
  | Add -> fprintf fmt "+"
  | Subtract -> fprintf fmt "-"
  | Multiply -> fprintf fmt "*"
  | Divide -> fprintf fmt "/"
  | Modulo -> fprintf fmt "%%"
  | LeftShift -> fprintf fmt "<<"
  | RightShift -> fprintf fmt ">>"
  | IsEqual -> fprintf fmt "=="
  | NotEqual -> fprintf fmt "!="
  | LessThan -> fprintf fmt "<"
  | LessThanOrEqual -> fprintf fmt "<="
  | GreaterThan -> fprintf fmt ">"
  | GreaterThanOrEqual -> fprintf fmt ">="
  | BitwiseAnd -> fprintf fmt "&"
  | BitwiseXor -> fprintf fmt "^"
  | BitwiseOr -> fprintf fmt "|"
  | LogicAnd -> fprintf fmt "&&"
  | LogicOr -> fprintf fmt "||"
  | Assign -> fprintf fmt "="
  | Mutate -> fprintf fmt ":="

let rec pp_vec_literal fmt (vec : vec_literal) =
  fprintf fmt "@[<hv 2>Vec(@,%a@,)@]"
    (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt ",@ ") pp_expression)
    vec

and pp_mat_literal fmt (mat : mat_literal) =
  fprintf fmt "@[<hv 2>Mat(@,%a@,)@]"
    (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt ",@ ") pp_vec_literal)
    mat.rows

and pp_literal fmt lit =
  match lit with
  | HexInt i -> fprintf fmt "0x%x" i
  | OctInt i -> fprintf fmt "0o%o" i
  | BinInt i -> fprintf fmt "0b%d <<TODO>>" i
  | DecInt i -> fprintf fmt "%d" i
  | Float f -> fprintf fmt "%f" f
  | String s -> fprintf fmt "%S" s
  | Char c -> fprintf fmt "%c" c
  | Matrix m -> pp_mat_literal fmt m
  | Vector v -> pp_vec_literal fmt v
  | Enum e ->
      fprintf fmt "Enum(%s, %s, types=%a)" e.enum_name e.enum_variant
        pp_type_list e.types

and pp_type fmt ty =
  match ty with
  | NumericType n -> fprintf fmt "%s" (numeric_type_to_string n)
  | VecType v -> fprintf fmt "%s" (vec_type_to_string v)
  | MatrixType m -> fprintf fmt "%s" (mat_type_to_string m)
  | FloatType -> fprintf fmt "float"
  | VoidType -> fprintf fmt "void"
  | StringType -> fprintf fmt "str"
  | CustomType c -> fprintf fmt "%s" c.name
  | CellType t -> fprintf fmt "Cell<%a>" pp_type t
  | FunctionType f ->
      fprintf fmt "%sFunction<(%a) -> %a>"
        (if f.vararg then "VA" else "")
        pp_type_list f.param_types pp_type f.return_type
  | PointerType p -> fprintf fmt "Pointer<%a>" pp_type p
  | BoxType b -> fprintf fmt "Box<%a>" pp_type b
  | ArrayType a ->
      fprintf fmt "Array<%a, %a>" pp_literal a.count pp_type a.element
  | TemplatedType t -> fprintf fmt "%s<todo>" t.outer

and pp_type_list fmt (l : haven_type list) =
  (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt ",@ ") pp_type) fmt l

and pp_expression fmt expr =
  match expr with
  | Binary b ->
      fprintf fmt "@[<hv 2>Binary(@,%a@,%a@,%a@,)@]" pp_expression b.left
        pp_binary_op b.op pp_expression b.right
  | Unary u ->
      fprintf fmt "Unary(%a, %a)" pp_unary_op u.op pp_expression u.inner
  | Literal lit -> fprintf fmt "Literal(%a)" pp_literal lit
  | Block block -> pp_block fmt block
  | ParenthesizedExpression e ->
      fprintf fmt "@[<hv 2>Parenthesized(@,%a@,)@]" pp_expression e
  | Identifier s -> fprintf fmt "Ident(%s)" s
  | Initializer i -> pp_expr_named_list fmt "Initializer" i.exprs
  | As a ->
      fprintf fmt "@[<hv 2>As<%a>(@,%a@,)@]" pp_type a.target_type pp_expression
        a.inner
  | SizeExpr e -> fprintf fmt "SizeExpr(%a)" pp_expression e
  | SizeType t -> fprintf fmt "SizeType(%a)" pp_type t
  | Nil -> fprintf fmt "Nil"
  | If i -> pp_if_expr fmt i
  | Match m -> pp_match_expr fmt m
  | BoxExpr e -> fprintf fmt "Box(expr=%a)" pp_expression e
  | BoxType t -> fprintf fmt "Box(type=%a)" pp_type t
  | Unbox e -> fprintf fmt "Unbox(%a)" pp_expression e
  | Ref e -> fprintf fmt "Ref(%a)" pp_expression e
  | Load e -> fprintf fmt "Load(%a)" pp_expression e
  | Call c ->
      fprintf fmt "Call(%a, params=%a)" pp_expression c.target
        (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt "@,") pp_expression)
        c.params
  | Index i ->
      fprintf fmt "Index(%a, %a)" pp_expression i.target pp_expression i.index
  | Field f ->
      fprintf fmt "Field(%a, arrow=%a, field=%s)" pp_expression f.target
        pp_print_bool f.arrow f.field

and pp_match_expr fmt m =
  fprintf fmt "@[<v 2>Match(@,expr=%a,@ arms=%a@,)@]" pp_expression m.expr
    pp_match_arms m.arms

and pp_match_arms fmt arms =
  fprintf fmt "@[<v 2>Arms(@,%a@,)@]"
    (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt "@,") pp_match_arm)
    arms

and pp_match_arm fmt arm =
  fprintf fmt "@[<hv 2>Arm(@,pattern=%a,@ expr=%a@,)@]" pp_pattern arm.pattern
    pp_expression arm.expr

and pp_pattern_binding fmt (b : pattern_binding) =
  match b with
  | BindingIgnored -> fprintf fmt "_"
  | BindingNamed s -> fprintf fmt "%s" s

and pp_pattern_binding_list fmt (bindings : pattern_binding list) =
  (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt "@,") pp_pattern_binding)
    fmt bindings

and pp_enum_pattern fmt (pat : pattern_enum) =
  fprintf fmt "Enum(%a, %s, bindings=%a)"
    (pp_print_option pp_print_string)
    pat.enum_name pat.enum_variant pp_pattern_binding_list pat.binding

and pp_pattern fmt pat =
  match pat with
  | PatternDefault -> fprintf fmt "_"
  | PatternLiteral lit -> pp_literal fmt lit
  | PatternEnum e -> pp_enum_pattern fmt e

and pp_if_expr fmt i =
  fprintf fmt "@[<hv 2>If(@,cond=%a,@ then=%a,@ else=%a@,)@]" pp_expression
    i.cond pp_block i.then_block pp_else_block i.else_block

and pp_else_block fmt blk =
  match blk with
  | None -> fprintf fmt "None"
  | Some (ElseIf e) -> pp_if_expr fmt e
  | Some (Else b) -> pp_block fmt b

and pp_expr_named_list fmt name exprs =
  fprintf fmt "@[<hv 2>%s(@,%a@,)@]" name
    (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt "@,") pp_expression)
    exprs

and pp_statement fmt stmt =
  match stmt with
  | Expression e -> fprintf fmt "@[<hv 2>Expr(@,%a@,)@]" pp_expression e
  | Let s ->
      fprintf fmt "@[<hv 2>Let(@,mut=%a,@ name=%s,@ init_expr=%a@,)@]"
        pp_print_bool s.mut s.name pp_expression s.init_expr
  | Return (Some e) -> fprintf fmt "@[<hv 2>Return(@,%a@,)@]" pp_expression e
  | Return None -> fprintf fmt "Return"
  | Defer e -> fprintf fmt "@[<hv 2>Defer(@,%a@,)@]" pp_expression e
  | Iter i ->
      fprintf fmt
        "@[<hv 2>Iter(@,var=%s,@ start=%a,@ end=%a,@ incr=%a,@ body=%a@,)@]"
        i.var pp_expression i.range.range_start pp_expression i.range.range_end
        (pp_print_option pp_expression)
        i.range.range_incr pp_block i.body
  | While w ->
      fprintf fmt "@[<hv 2>While(@,cond=%a,@ body=%a@,)@]" pp_expression w.cond
        pp_block w.body
  | Break -> fprintf fmt "Break"
  | Continue -> fprintf fmt "Continue"
  | Empty -> fprintf fmt "Empty"

and pp_block_item fmt item =
  match item with
  | BlockStatement s -> pp_statement fmt s
  | BlockExpression e -> pp_expression fmt e

and pp_block fmt block =
  fprintf fmt "@[<v 2>Block(@,%a@,)@]"
    (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt "@,") pp_block_item)
    block.items

let pp_param fmt (p : param) = fprintf fmt "Param(%s, %a)" p.name pp_type p.ty

let pp_param_list fmt (l : param_list) =
  match l.params with
  | [] -> fprintf fmt "[]"
  | _ ->
      fprintf fmt "@[<hv 2>[@,%a@,]@]"
        (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt ",@ ") pp_param)
        l.params

let pp_intrinsic fmt (i : intrinsic) =
  fprintf fmt "@[<hv 2>Intrinsic(%s, types=[%a])@]" i.name pp_type_list i.types

let pp_fdecl fmt (decl : function_decl) =
  fprintf fmt
    "@[<hv 2>Function(@,\
     pub=%a@,\
     impure=%a@,\
     name=%s,@ params=%a,@ vararg=%a,@ intrinsic=%a,@ body=%a@,\
     )@]"
    pp_print_bool decl.public pp_print_bool decl.impure decl.name pp_param_list
    decl.params pp_print_bool decl.vararg
    (pp_print_option pp_intrinsic)
    decl.intrinsic (pp_print_option pp_block) decl.definition

let pp_fdecl_list fmt decls =
  let printer decls =
    (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt "@,") pp_fdecl) decls
  in
  printer fmt decls

let pp_var_decl fmt (decl : var_decl) =
  fprintf fmt
    "@[<hv 2>Variable(@,name=%s,@ pub=%a,@ mutable=%a,@ ty=%a,@ init=%a@,)@]"
    decl.name pp_print_bool decl.public pp_print_bool decl.is_mutable pp_type
    decl.ty
    (pp_print_option pp_expression)
    decl.init_expr

let pp_struct_field fmt (f : struct_field) =
  fprintf fmt "Field(%a, %s)" pp_type f.ty f.name

let pp_struct_decl fmt (d : struct_decl) =
  (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt ",@ ") pp_struct_field)
    fmt d.fields

let pp_enum_variant fmt (v : enum_variant) =
  fprintf fmt "Variant(%s, inner=%a)" v.name (pp_print_option pp_type)
    v.inner_ty

let pp_enum_decl fmt (d : enum_decl) =
  (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt ",@ ") pp_enum_variant)
    fmt d.variants

let pp_type_decl_data fmt tyd =
  match tyd with
  | TypeDeclAlias t -> fprintf fmt "Alias(%a)" pp_type t
  | TypeDeclStruct s -> fprintf fmt "@[<hv 2>Struct(@,%a@,)@]" pp_struct_decl s
  | TypeDeclEnum e -> fprintf fmt "@[<hv 2>Enum(@,%a@,)@]" pp_enum_decl e
  | TypeDeclForward -> fprintf fmt "Forward"

let pp_type_decl fmt (ty : type_decl) =
  fprintf fmt "@[<hv 2>TypeDecl(@,%s,@ %a@,)@]" ty.name pp_type_decl_data
    ty.data

let pp_decl fmt decl =
  match decl with
  | FDecl d -> fprintf fmt "@[<hv 2>FDecl(@,%a@,)@]" pp_fdecl d
  | TDecl t -> pp_type_decl fmt t
  | VDecl v -> pp_var_decl fmt v
  | Import i -> fprintf fmt "Import(%s)" i
  | CImport i -> fprintf fmt "CImport(%s)" i
  | Foreign f ->
      fprintf fmt "@[<v 2>Foreign(@,%s,@ decls=%a@,)@]" f.lib pp_fdecl_list
        f.decls

let pp_program fmt (program : program) =
  Format.pp_set_margin fmt 100;
  Format.pp_set_max_indent fmt 80;
  fprintf fmt "@[<v 2>Program(@,%a@,)@]"
    (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt "@,") pp_decl)
    program.decls;
  Format.pp_print_newline fmt ()
