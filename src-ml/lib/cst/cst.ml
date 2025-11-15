open Haven_token

type unary_operator =
  | Not
  | Negate
  | Complement

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

type custom_type = {
  name: string;
}

type function_type = {
  param_types: haven_type list;
  return_type: haven_type;
  vararg: bool;
}

and array_type = {
  element: haven_type;
  count: literal
}

and templated_type = {
  outer: string;
  inner: haven_type list;
}

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

and program = {
  decls: top_decl list;
}

and top_decl =
  | FDecl of function_decl
  | TDecl
  | VDecl
  | Import
  | CImport
  | Foreign

and function_decl = {
  public: bool;
  impure: bool;
  name: string;
  definition: block option;
  (* TODO: params *)
}

and block = {
  items: block_item list;
}

and block_item =
  | BlockStatement of statement
  | BlockExpression of expression

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
  mut: bool;
  ty: haven_type option;
  name: string;
  init_expr: expression;
}

and iter_range = {
  range_start: expression;
  range_end: expression;
  range_incr: expression option;
}

and iter_stmt = {
  range: iter_range;
  var: string;
  body: block;
}

and while_stmt = {
  cond: expression;
  body: block;
}

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
  | Match
  | Box
  | Unbox
  | Ref
  | Load
  | Call of call
  | Index of index
  | Field of field

and call = {
  target: expression;
  params: expression list;
}

and index = {
  target: expression;
  index: expression;
}

and field = {
  target: expression;
  arrow: bool;
  field: string;
}

and binary = {
  left: expression;
  right: expression;
  op: binary_operator;
}

and unary = {
  inner: expression;
  op: unary_operator;
}

and as_expr = {
  target_type: haven_type;
  inner: expression;
}

and if_else_block =
  | ElseIf of if_expr
  | Else of block

and if_expr = {
  cond: expression;
  then_block: block;
  else_block: if_else_block option;
}

and init_list = {
  exprs: expression list;
}

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
  | Enum

and mat_literal = {
  rows: vec_literal list;
}

and vec_literal = expression list
