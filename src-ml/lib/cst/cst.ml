open Haven_token

type unary_operator = Not | Negate | Complement

type binary_operator =
  | Add
  | Subtract
  | Multiply
  | Divide
  | Modulo
  | LeftShift
  | RightShift
  | IsEqual
  | NotEqual
  | LessThan
  | LessThanOrEqual
  | GreaterThan
  | GreaterThanOrEqual
  | BitwiseAnd
  | BitwiseXor
  | BitwiseOr
  | LogicAnd
  | LogicOr
  | Assign
  | Mutate

type custom_type = { name : string }

type function_type = {
  param_types : haven_type list;
  return_type : haven_type;
  vararg : bool;
}

and array_type = { element : haven_type; count : literal }
and templated_type = { outer : string; inner : haven_type list }

and haven_type =
  | NumericType of numeric_type
  | VecType of vec_type
  | MatrixType of mat_type
  | FloatType
  | VoidType
  | StringType
  | CustomType of custom_type
  | CellType of haven_type
  | FunctionType of function_type
  | PointerType of haven_type
  | BoxType of haven_type
  | ArrayType of array_type
  | TemplatedType of templated_type

and program = { decls : top_decl list }

and top_decl =
  | FDecl of function_decl
  | TDecl of type_decl
  | VDecl of var_decl
  | Import of string
  | CImport of string
  | Foreign of foreign

and function_decl = {
  public : bool;
  impure : bool;
  name : string;
  definition : block option;
  intrinsic : intrinsic option;
  params : param_list;
  return_type : haven_type option;
  vararg : bool;
}

and var_decl = {
  name : string;
  public : bool;
  is_mutable : bool;
  ty : haven_type;
  init_expr : expression option;
}

and type_decl = { name : string; data : type_decl_data }

and type_decl_data =
  | TypeDeclAlias of haven_type
  | TypeDeclStruct of struct_decl
  | TypeDeclEnum of enum_decl
  | TypeDeclForward

and struct_decl = { fields : struct_field list }
and enum_decl = { variants : enum_variant list }
and struct_field = { name : string; ty : haven_type }
and enum_variant = { name : string; inner_ty : haven_type option }
and param_list = { params : param list; vararg : bool }
and param = { name : string; ty : haven_type }
and intrinsic = { name : string; types : haven_type list }
and foreign = { lib : string; decls : function_decl list }
and block = { items : block_item list }
and block_item = BlockStatement of statement | BlockExpression of expression

and statement =
  | Expression of expression
  | Let of let_stmt
  | Return of expression option
  | Defer of expression
  | Iter of iter_stmt
  | While of while_stmt
  | Break
  | Continue
  | Empty

and let_stmt = {
  mut : bool;
  ty : haven_type option;
  name : string;
  init_expr : expression;
}

and iter_range = {
  range_start : expression;
  range_end : expression;
  range_incr : expression option;
}

and iter_stmt = { range : iter_range; var : string; body : block }
and while_stmt = { cond : expression; body : block }

and expression =
  | Binary of binary
  | Unary of unary
  | Literal of literal
  | Block of block
  | ParenthesizedExpression of expression
  | Identifier of string
  | Initializer of init_list
  | As of as_expr
  | SizeExpr of expression
  | SizeType of haven_type
  | Nil
  | If of if_expr
  | Match of match_expr
  | BoxExpr of expression
  | BoxType of haven_type
  | Unbox of expression
  | Ref of expression
  | Load of expression
  | Call of call
  | Index of index
  | Field of field

and call = { target : expression; params : expression list }
and index = { target : expression; index : expression }
and field = { target : expression; arrow : bool; field : string }
and binary = { left : expression; right : expression; op : binary_operator }
and unary = { inner : expression; op : unary_operator }
and as_expr = { target_type : haven_type; inner : expression }
and if_else_block = ElseIf of if_expr | Else of block

and if_expr = {
  cond : expression;
  then_block : block;
  else_block : if_else_block option;
}

and match_expr = { expr : expression; arms : match_arm list }

and match_pattern =
  | PatternDefault
  | PatternLiteral of literal
  | PatternEnum of pattern_enum

and pattern_binding = BindingIgnored | BindingNamed of string

and pattern_enum = {
  enum_name : string option;
  enum_variant : string;
  binding : pattern_binding list;
}

and match_arm = { pattern : match_pattern; expr : expression }
and init_list = { exprs : expression list }

and literal =
  | HexInt of int
  | OctInt of int
  | BinInt of int
  | DecInt of int
  | Float of float
  | String of string
  | Char of char
  | Matrix of mat_literal
  | Vector of vec_literal
  | Enum of enum_literal

and mat_literal = { rows : vec_literal list }
and vec_literal = expression list

and enum_literal = {
  enum_name : string;
  enum_variant : string;
  types : haven_type list;
}
