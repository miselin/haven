module Cst = Haven_cst.Cst

let ast_from_cst (_cst : Haven_cst.Cst.parsed_program) = ()

(** [block_to_expr block] desugars a single-expression block into an expression,
    or None if it is not single-item **)
let cst_block_to_expr ({ value; _ } : Cst.block) =
  match value.items with
  | [ item ] -> (
      match item.value with Cst.BlockExpression expr -> Some expr | _ -> None)
  | _ -> None

(** [expr_to_expr block] desugars certain expressions into simpler expressions,
    simplifying constructs for the AST **)
let cst_expr_desugar (expr : Cst.expression) =
  match expr.value with ParenthesizedExpression e -> e | _ -> expr

(* TODO: more desugars *)

let rec cst_literal_to_ast ({ value; loc } : Cst.literal) : Ast.literal =
  let value =
    match value with
    | HexInt i | OctInt i | BinInt i | DecInt i -> Ast.Integer i
    | Float f -> Ast.Float f
    | String s -> Ast.String s
    | Char c -> Ast.Char c
    | Matrix m -> Ast.Matrix (cst_mat_literal_to_ast m)
    | Vector v -> Ast.Vector (cst_vec_literal_to_ast v)
    | Enum e -> Ast.Enum (cst_enum_literal_to_ast e)
  in
  { value; loc }

and cst_expr_to_ast ({ value; loc } : Cst.expression) : Ast.expression =
  let value =
    match value with
    | Binary _b -> Ast.Todo
    | Unary _u -> Ast.Todo
    | Literal l -> Ast.Literal (cst_literal_to_ast l)
    | Block b -> (
        match cst_block_to_expr b with Some _e -> Ast.Todo | None -> Ast.Todo)
    | ParenthesizedExpression _e -> Ast.Todo
    | Identifier _i -> Ast.Todo
    | Initializer _i -> Ast.Todo
    | As _a -> Ast.Todo
    | SizeExpr _s -> Ast.Todo
    | SizeType _s -> Ast.Todo
    | Nil -> Ast.Nil
    | If _i -> Ast.Todo
    | Match _m -> Ast.Todo
    | BoxExpr _b -> Ast.Todo
    | BoxType _b -> Ast.Todo
    | Unbox _b -> Ast.Todo
    | Ref _r -> Ast.Todo
    | Load _l -> Ast.Todo
    | Call _c -> Ast.Todo
    | Index _i -> Ast.Todo
    | Field _f -> Ast.Todo
  in
  { value; loc }

and cst_vec_literal_to_ast ({ value; loc } : Cst.vec_literal) : Ast.vec_literal
    =
  { value = { elements = List.map cst_expr_to_ast value.elements }; loc }

and cst_mat_literal_to_ast ({ value; loc } : Cst.mat_literal) : Ast.mat_literal
    =
  { value = { rows = List.map cst_vec_literal_to_ast value.rows }; loc }

and cst_enum_literal_to_ast ({ value; loc } : Cst.enum_literal) :
    Ast.enum_literal =
  {
    value =
      {
        enum_name = cst_identifier_to_ast value.enum_name;
        enum_variant = cst_identifier_to_ast value.enum_variant;
        types = List.map cst_haven_type_to_ast value.types;
        wrapped = [];
      };
    loc;
  }

and cst_identifier_to_ast (i : Cst.identifier) : Ast.identifier =
  { value = i.value; loc = i.loc }

and cst_haven_type_to_ast (ty : Cst.haven_type) : Ast.haven_type =
  { value = Ast.VoidType; loc = ty.loc }
