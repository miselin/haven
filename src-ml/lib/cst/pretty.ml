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

let pp_literal fmt lit =
  match lit with
  | HexInt i -> fprintf fmt "0x%x" i
  | OctInt i -> fprintf fmt "0o%o" i
  | BinInt i -> fprintf fmt "0b%d <<TODO>>" i
  | DecInt i -> fprintf fmt "%d" i
  | Float f -> fprintf fmt "%f" f
  | String s -> fprintf fmt "%s" s
  | Char c -> fprintf fmt "%c" c
  | Matrix _ -> fprintf fmt "mat todo"
  | Vector _ -> fprintf fmt "mat todo"
  | Enum -> fprintf fmt "enum todo"

let rec pp_type fmt ty =
  match ty with
  | NumericType n -> fprintf fmt "%s" (numeric_type_to_string n)
  | VecType v -> fprintf fmt "%s" (vec_type_to_string v)
  | MatrixType m -> fprintf fmt "%s" (mat_type_to_string m)
  | FloatType -> fprintf fmt "float"
  | VoidType -> fprintf fmt "void"
  | StringType -> fprintf fmt "str"
  | CustomType c -> fprintf fmt "%s" c.name
  | CellType t -> fprintf fmt "Cell<%a>" pp_type t
  | FunctionType _f -> fprintf fmt "Function<todo>"
  | PointerType p -> fprintf fmt "Pointer<%a>" pp_type p
  | BoxType b -> fprintf fmt "Box<%a>" pp_type b
  | ArrayType a -> fprintf fmt "Array<%a, %a>" pp_literal a.count pp_type a.element
  | TemplatedType t -> fprintf fmt "%s<todo>" t.outer

let rec pp_expression fmt expr =
  match expr with
  | Binary b ->
    fprintf fmt "@[<v 2>Binary(@,%a@,%a@,%a@,)@]" pp_expression b.left pp_binary_op b.op pp_expression b.right
  | Unary u ->
    fprintf fmt "Unary(%a, %a)" pp_unary_op u.op pp_expression u.inner
  | Literal lit -> fprintf fmt "Literal(%a)" pp_literal lit
  | Block block -> fprintf fmt "Block(%a)" pp_block block
  | ParenthesizedExpression e ->
    fprintf fmt "@[Parenthesized(@,%a@,)@]" pp_expression e
  | Identifier s -> fprintf fmt "Ident(%s)" s
  | Initializer i -> pp_expr_named_list fmt "Initializer" i.exprs
  | As a -> fprintf fmt "@[<v 2>As<%a>(@,%a@,)@]" pp_type a.target_type pp_expression a.inner
  | SizeExpr e -> fprintf fmt "SizeExpr(%a)" pp_expression e
  | SizeType t -> fprintf fmt "SizeType(%a)" pp_type t
  | Nil -> fprintf fmt "Nil"
  | If i -> pp_if_expr fmt i
  | Match -> fprintf fmt "Match(todo)"
  | Box -> fprintf fmt "Box(todo)"
  | Unbox -> fprintf fmt "Unbox(todo)"
  | Ref -> fprintf fmt "Ref(todo)"
  | Load -> fprintf fmt "Load(todo)"
  | Call c -> fprintf fmt "Call(%a, params=%a)" pp_expression c.target (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt "@,") pp_expression) c.params
  | Index i -> fprintf fmt "Index(%a, %a)" pp_expression i.target pp_expression i.index
  | Field f -> fprintf fmt "Field(%a, arrow=%a, field=%s)" pp_expression f.target pp_print_bool f.arrow f.field

and pp_if_expr fmt i =
  fprintf fmt "@[<v 2>If(@,cond=%a,@,then=%a,@,else=%a@,)@]" pp_expression i.cond pp_block i.then_block pp_else_block i.else_block

and pp_else_block fmt blk =
  match blk with
  | None -> fprintf fmt "None"
  | Some (ElseIf e) -> pp_if_expr fmt e
  | Some (Else b) -> pp_block fmt b

and pp_expr_named_list fmt name exprs =
  fprintf fmt "@[<v 2>%s(@,%a@,)@]" name
    (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt "@,")
    pp_expression)
    exprs

and pp_maybe_expression fmt expr =
  match expr with
  | Some e -> pp_expression fmt e
  | None -> ()

and pp_statement fmt stmt =
  match stmt with
  | Expression e -> fprintf fmt "@[<v 2>Expr(@,%a@,)@]" pp_expression e
  | Let s -> fprintf fmt "@[<v 2>Let(@,mut=%a,@,name=%s,@,init_expr=%a@,)@]" pp_print_bool s.mut s.name pp_expression s.init_expr
  | Return (Some e) -> fprintf fmt "@[<v 2>Return(@,%a@,)@]" pp_expression e
  | Return (None) -> fprintf fmt "Return"
  | Defer e -> fprintf fmt "@[<v 2>Defer(@,%a@,)@]" pp_expression e
  | Iter i -> fprintf fmt "@[<v 2>Iter(@,var=%s,@,start=%a,@,end=%a,@,incr=%a,@,body=%a@,)@]" i.var pp_expression i.range.range_start pp_expression i.range.range_end pp_maybe_expression i.range.range_incr pp_block i.body
  | While _w -> fprintf fmt "While(todo)"
  | Break -> fprintf fmt "Break"
  | Continue -> fprintf fmt "Continue"
  | Empty -> fprintf fmt "Empty"

and pp_block_item fmt item =
  match item with
  | BlockStatement s -> pp_statement fmt s
  | BlockExpression e -> pp_expression fmt e

and pp_block fmt block =
  fprintf fmt "@[<v 2>Block(@,%a@,)@]"
    (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt "@,")
    pp_block_item)
    block.items

and pp_maybe_block fmt block =
  match block with
  | Some b -> pp_block fmt b
  | None -> ()

let pp_fdecl fmt decl =
  fprintf fmt "@[<v 2>Function(@,pub=%a@,impure=%a@,name=%s@,body=%a@,)@]" pp_print_bool decl.public pp_print_bool decl.impure decl.name pp_maybe_block decl.definition

let pp_decl fmt decl =
  match decl with
  | FDecl d -> fprintf fmt "@[<v 2>FDecl(@,%a@,)@]" pp_fdecl d
  | TDecl -> fprintf fmt "TDecl"
  | VDecl -> fprintf fmt "VDecl"
  | Import -> fprintf fmt "Import"
  | CImport -> fprintf fmt "CImport"
  | Foreign -> fprintf fmt "Foreign"

let pp_program fmt program =
  Format.pp_set_margin fmt 100;
  Format.pp_set_max_indent fmt 80;
  fprintf fmt "@[<v 2>Program(@,%a@,)@]"
    (pp_print_list ~pp_sep:(fun fmt () -> fprintf fmt "@,")
    pp_decl)
    program.decls;
  Format.pp_print_newline fmt ()
