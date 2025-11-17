open Haven_token.Token
module Lexer = Haven_lexer.Lexer
module Raw = Lexer.Raw

type trivia_entry = Lexer.token_with_trivia
type trivia_table = (string * int, Raw.tok list) Hashtbl.t
type trivia_tables = { leading : trivia_table; trailing : trivia_table }

type comment = {
  tok : Raw.tok;
  startp : Lexing.position;
  endp : Lexing.position;
}

type location = { start_pos : Lexing.position; end_pos : Lexing.position }
type 'a node = { value : 'a; loc : location }

let location_between ~start_pos ~end_pos = { start_pos; end_pos }

let location_spanning (a : _ node) (b : _ node) =
  location_between ~start_pos:a.loc.start_pos ~end_pos:b.loc.end_pos

let with_location ~start_pos ~end_pos value =
  { value; loc = location_between ~start_pos ~end_pos }

let pos_key (pos : Lexing.position) = (pos.pos_fname, pos.pos_cnum)
let trivia_key_of_location (loc : location) = pos_key loc.start_pos

let build_trivia_tables (tokens : trivia_entry list) : trivia_tables =
  let leading = Hashtbl.create 64 in
  let trailing = Hashtbl.create 64 in
  let comments_from_trivia trivia =
    List.filter
      (fun (tok : Raw.tok) ->
        match tok.tok with Raw.Trivia (Comment _) -> true | _ -> false)
      trivia
  in
  List.iter
    (fun (tok : trivia_entry) ->
      let l = comments_from_trivia tok.leading_trivia in
      if l <> [] then Hashtbl.replace leading (pos_key tok.startp) l;
      let r = comments_from_trivia tok.trailing_trivia in
      if r <> [] then Hashtbl.replace trailing (pos_key tok.endp) r)
    tokens;
  { leading; trailing }

let comments_from_raw_tokens tokens =
  tokens
  |> List.filter_map (fun (tok : Raw.tok) ->
      match tok.tok with
      | Raw.Trivia (Lexer.Comment _) ->
          Some { tok; startp = tok.startp; endp = tok.endp }
      | _ -> None)
  |> List.sort (fun a b -> compare a.startp.pos_cnum b.startp.pos_cnum)

let trivia_for_location (tables : trivia_tables) ~kind (loc : location) =
  let table =
    match kind with `Leading -> tables.leading | `Trailing -> tables.trailing
  in
  Hashtbl.find_opt table (trivia_key_of_location loc)

type identifier = string node
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

type custom_type = { name : identifier }

type function_type_desc = {
  param_types : haven_type list;
  return_type : haven_type;
  vararg : bool;
}

and function_type = function_type_desc node
and array_type_desc = { element : haven_type; count : literal }
and array_type = array_type_desc node
and templated_type_desc = { outer : identifier; inner : haven_type list }
and templated_type = templated_type_desc node

and haven_type_desc =
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

and haven_type = haven_type_desc node
and program_desc = { decls : top_decl list }
and program = program_desc node
and top_level_item = Decl of top_decl | Comments of comment list

and top_decl_desc =
  | FDecl of function_decl
  | TDecl of type_decl
  | VDecl of var_decl
  | Import of string node
  | CImport of string node
  | Foreign of foreign

and top_decl = top_decl_desc node

and function_decl_desc = {
  public : bool;
  impure : bool;
  name : identifier;
  definition : block option;
  intrinsic : intrinsic option;
  params : param_list;
  return_type : haven_type option;
  vararg : bool;
}

and function_decl = function_decl_desc node

and var_decl_desc = {
  name : identifier;
  public : bool;
  is_mutable : bool;
  ty : haven_type;
  init_expr : expression option;
}

and var_decl = var_decl_desc node
and type_decl_desc = { name : identifier; data : type_decl_data }
and type_decl = type_decl_desc node

and type_decl_data =
  | TypeDeclAlias of haven_type
  | TypeDeclStruct of struct_decl
  | TypeDeclEnum of enum_decl
  | TypeDeclForward

and struct_decl_desc = { fields : struct_field list }
and enum_decl_desc = { variants : enum_variant list }
and struct_field_desc = { name : identifier; ty : haven_type }
and enum_variant_desc = { name : identifier; inner_ty : haven_type option }
and struct_decl = struct_decl_desc node
and enum_decl = enum_decl_desc node
and struct_field = struct_field_desc node
and enum_variant = enum_variant_desc node
and param_list_desc = { params : param list; vararg : bool }
and param_list = param_list_desc node
and param_desc = { name : identifier; ty : haven_type }
and param = param_desc node
and intrinsic_desc = { name : string node; types : haven_type list }
and intrinsic = intrinsic_desc node
and foreign_desc = { lib : string node; decls : function_decl list }
and foreign = foreign_desc node
and block_desc = { items : block_item list }
and block = block_desc node

and block_item_desc =
  | BlockStatement of statement
  | BlockExpression of expression

and block_item = block_item_desc node

and statement_desc =
  | Expression of expression
  | Let of let_stmt
  | Return of expression option
  | Defer of expression
  | Iter of iter_stmt
  | While of while_stmt
  | Break
  | Continue
  | Empty

and statement = statement_desc node

and let_stmt_desc = {
  mut : bool;
  ty : haven_type option;
  name : identifier;
  init_expr : expression;
}

and let_stmt = let_stmt_desc node

and iter_range_desc = {
  range_start : expression;
  range_end : expression;
  range_incr : expression option;
}

and iter_range = iter_range_desc node
and iter_stmt_desc = { range : iter_range; var : identifier; body : block }
and iter_stmt = iter_stmt_desc node
and while_stmt_desc = { cond : expression; body : block }
and while_stmt = while_stmt_desc node

and expression_desc =
  | Binary of binary
  | Unary of unary
  | Literal of literal
  | Block of block
  | ParenthesizedExpression of expression
  | Identifier of identifier
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

and expression = expression_desc node
and call_desc = { target : expression; params : expression list }
and call = call_desc node
and index_desc = { target : expression; index : expression }
and index = index_desc node
and field_desc = { target : expression; arrow : bool; field : identifier }
and field = field_desc node

and binary_desc = {
  left : expression;
  right : expression;
  op : binary_operator;
}

and binary = binary_desc node
and unary_desc = { inner : expression; op : unary_operator }
and unary = unary_desc node
and as_expr_desc = { target_type : haven_type; inner : expression }
and as_expr = as_expr_desc node
and if_else_block = ElseIf of if_expr | Else of block

and if_expr_desc = {
  cond : expression;
  then_block : block;
  else_block : if_else_block option;
}

and if_expr = if_expr_desc node
and match_expr_desc = { expr : expression; arms : match_arm list }
and match_expr = match_expr_desc node

and match_pattern_desc =
  | PatternDefault
  | PatternLiteral of literal
  | PatternEnum of pattern_enum

and match_pattern = match_pattern_desc node
and pattern_binding_desc = BindingIgnored | BindingNamed of identifier
and pattern_binding = pattern_binding_desc node

and pattern_enum_desc = {
  enum_name : identifier option;
  enum_variant : identifier;
  binding : pattern_binding list;
}

and pattern_enum = pattern_enum_desc node
and match_arm_desc = { pattern : match_pattern; expr : expression }
and match_arm = match_arm_desc node
and init_list_desc = { exprs : expression list }
and init_list = init_list_desc node

and literal_desc =
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

and literal = literal_desc node
and mat_literal_desc = { rows : vec_literal list }
and vec_literal_desc = { elements : expression list }
and mat_literal = mat_literal_desc node
and vec_literal = vec_literal_desc node

and enum_literal_desc = {
  enum_name : identifier;
  enum_variant : identifier;
  types : haven_type list;
}

and enum_literal = enum_literal_desc node

type parsed_program = {
  program : program;
  trivia : trivia_entry list;
  trivia_table : trivia_tables;
  comments : comment list;
  items : top_level_item list;
}

let merge_comments_with_decls (program : program) (comments : comment list) :
    top_level_item list =
  let decls = program.value.decls in
  let rec merge acc remaining_comments remaining_decls =
    match (remaining_comments, remaining_decls) with
    | [], [] -> List.rev acc
    | [], d :: ds -> merge (Decl d :: acc) [] ds
    | cs, [] -> List.rev (Comments cs :: acc)
    | c :: cs_tail, d :: ds ->
        if c.startp.pos_cnum < d.loc.start_pos.pos_cnum then
          let comments_for_decl, rest =
            List.partition
              (fun cmt -> cmt.startp.pos_cnum < d.loc.start_pos.pos_cnum)
              (c :: cs_tail)
          in
          merge (Comments comments_for_decl :: acc) rest (d :: ds)
        else merge (Decl d :: acc) remaining_comments ds
  in
  merge [] comments decls
