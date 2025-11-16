open Cst

type any_node =
  | Program of program
  | TopDecl of top_decl
  | FunctionDecl of function_decl
  | VarDecl of var_decl
  | TypeDecl of type_decl
  | StructDecl of struct_decl
  | EnumDecl of enum_decl
  | StructField of struct_field
  | EnumVariant of enum_variant
  | ParamList of param_list
  | Param of param
  | Intrinsic of intrinsic
  | Foreign of foreign
  | Block of block
  | BlockItem of block_item
  | Statement of statement
  | LetStmt of let_stmt
  | IterStmt of iter_stmt
  | WhileStmt of while_stmt
  | IterRange of iter_range
  | Expression of expression
  | Binary of binary
  | Unary of unary
  | Call of call
  | Index of index
  | Field of field
  | AsExpr of as_expr
  | IfExpr of if_expr
  | MatchExpr of match_expr
  | MatchArm of match_arm
  | MatchPattern of match_pattern
  | PatternEnum of pattern_enum
  | PatternBinding of pattern_binding
  | InitList of init_list
  | Literal of literal
  | MatLiteral of mat_literal
  | VecLiteral of vec_literal
  | EnumLiteral of enum_literal
  | HavenType of haven_type
  | ArrayType of array_type
  | TemplatedType of templated_type
  | FunctionType of function_type

let location_of = function
  | Program p -> p.loc
  | TopDecl t -> t.loc
  | FunctionDecl f -> f.loc
  | VarDecl v -> v.loc
  | TypeDecl t -> t.loc
  | StructDecl s -> s.loc
  | EnumDecl e -> e.loc
  | StructField f -> f.loc
  | EnumVariant v -> v.loc
  | ParamList p -> p.loc
  | Param p -> p.loc
  | Intrinsic i -> i.loc
  | Foreign f -> f.loc
  | Block b -> b.loc
  | BlockItem b -> b.loc
  | Statement s -> s.loc
  | LetStmt s -> s.loc
  | IterStmt i -> i.loc
  | WhileStmt w -> w.loc
  | IterRange r -> r.loc
  | Expression e -> e.loc
  | Binary b -> b.loc
  | Unary u -> u.loc
  | Call c -> c.loc
  | Index i -> i.loc
  | Field f -> f.loc
  | AsExpr a -> a.loc
  | IfExpr i -> i.loc
  | MatchExpr m -> m.loc
  | MatchArm m -> m.loc
  | MatchPattern p -> p.loc
  | PatternEnum e -> e.loc
  | PatternBinding b -> b.loc
  | InitList l -> l.loc
  | Literal l -> l.loc
  | MatLiteral m -> m.loc
  | VecLiteral v -> v.loc
  | EnumLiteral e -> e.loc
  | HavenType t -> t.loc
  | ArrayType a -> a.loc
  | TemplatedType t -> t.loc
  | FunctionType f -> f.loc

let pos_before (a : Lexing.position) (b : Lexing.position) =
  a.pos_cnum < b.pos_cnum

let contains_position loc pos =
  not (pos_before pos loc.start_pos) && not (pos_before loc.end_pos pos)

let overlaps_range loc range =
  not (pos_before loc.end_pos range.start_pos || pos_before range.end_pos loc.start_pos)

let add_if predicate node acc = if predicate node then node :: acc else acc

let rec walk_haven_type predicate acc (ty : haven_type) =
  let acc = add_if predicate (HavenType ty) acc in
  match ty.value with
  | NumericType _ | VecType _ | MatrixType _ | FloatType | VoidType
  | StringType ->
      acc
  | CustomType _ -> acc
  | CellType inner -> walk_haven_type predicate acc inner
  | FunctionType fn ->
      let acc = add_if predicate (FunctionType fn) acc in
      let acc = List.fold_left (walk_haven_type predicate) acc fn.value.param_types in
      walk_haven_type predicate acc fn.value.return_type
  | PointerType inner -> walk_haven_type predicate acc inner
  | BoxType inner -> walk_haven_type predicate acc inner
  | ArrayType arr ->
      let acc = add_if predicate (ArrayType arr) acc in
      walk_haven_type predicate acc arr.value.element
  | TemplatedType t ->
      let acc = add_if predicate (TemplatedType t) acc in
      List.fold_left (walk_haven_type predicate) acc t.value.inner

let rec walk_literal predicate acc lit =
  let acc = add_if predicate (Literal lit) acc in
  match lit.value with
  | HexInt _ | OctInt _ | BinInt _ | DecInt _ | Float _ | String _ | Char _ ->
      acc
  | Matrix m ->
      let acc = add_if predicate (MatLiteral m) acc in
      List.fold_left (walk_vec_literal predicate) acc m.value.rows
  | Vector v ->
      let acc = add_if predicate (VecLiteral v) acc in
      walk_vec_literal predicate acc v
  | Enum e ->
      let acc = add_if predicate (EnumLiteral e) acc in
      List.fold_left (walk_haven_type predicate) acc e.value.types

and walk_vec_literal predicate acc (vec : vec_literal) =
  let acc = add_if predicate (VecLiteral vec) acc in
  List.fold_left (walk_expression predicate) acc vec.value.elements

and walk_match_pattern predicate acc pat =
  let acc = add_if predicate (MatchPattern pat) acc in
  match pat.value with
  | PatternDefault -> acc
  | PatternLiteral lit -> walk_literal predicate acc lit
  | PatternEnum e ->
      let acc = add_if predicate (PatternEnum e) acc in
      let acc =
        List.fold_left (fun acc b -> add_if predicate (PatternBinding b) acc) acc
          e.value.binding
      in
      acc

and walk_match_arm predicate acc arm =
  let acc = add_if predicate (MatchArm arm) acc in
  let acc = walk_match_pattern predicate acc arm.value.pattern in
  walk_expression predicate acc arm.value.expr

and walk_if_expr predicate acc (ifx : if_expr) =
  let acc = add_if predicate (IfExpr ifx) acc in
  let acc = walk_expression predicate acc ifx.value.cond in
  let acc = walk_block predicate acc ifx.value.then_block in
  match ifx.value.else_block with
  | None -> acc
  | Some (Else b) -> walk_block predicate acc b
  | Some (ElseIf i) -> walk_if_expr predicate acc i

and walk_expression predicate acc expr =
  let acc = add_if predicate (Expression expr) acc in
  match expr.value with
  | Binary b ->
      let acc = add_if predicate (Binary b) acc in
      let acc = walk_expression predicate acc b.value.left in
      walk_expression predicate acc b.value.right
  | Unary u ->
      let acc = add_if predicate (Unary u) acc in
      walk_expression predicate acc u.value.inner
  | Literal lit -> walk_literal predicate acc lit
  | Block b -> walk_block predicate acc b
  | ParenthesizedExpression e -> walk_expression predicate acc e
  | Identifier _ -> acc
  | Initializer i ->
      let acc = add_if predicate (InitList i) acc in
      List.fold_left (walk_expression predicate) acc i.value.exprs
  | As a ->
      let acc = add_if predicate (AsExpr a) acc in
      let acc = walk_haven_type predicate acc a.value.target_type in
      walk_expression predicate acc a.value.inner
  | SizeExpr e -> walk_expression predicate acc e
  | SizeType t -> walk_haven_type predicate acc t
  | Nil -> acc
  | If i -> walk_if_expr predicate acc i
  | Match m ->
      let acc = add_if predicate (MatchExpr m) acc in
      let acc = walk_expression predicate acc m.value.expr in
      List.fold_left (walk_match_arm predicate) acc m.value.arms
  | BoxExpr e -> walk_expression predicate acc e
  | BoxType t -> walk_haven_type predicate acc t
  | Unbox e -> walk_expression predicate acc e
  | Ref e -> walk_expression predicate acc e
  | Load e -> walk_expression predicate acc e
  | Call c ->
      let acc = add_if predicate (Call c) acc in
      let acc = walk_expression predicate acc c.value.target in
      List.fold_left (walk_expression predicate) acc c.value.params
  | Index i ->
      let acc = add_if predicate (Index i) acc in
      let acc = walk_expression predicate acc i.value.target in
      walk_expression predicate acc i.value.index
  | Field f ->
      let acc = add_if predicate (Field f) acc in
      walk_expression predicate acc f.value.target

and walk_statement predicate acc stmt =
  let acc = add_if predicate (Statement stmt) acc in
  match stmt.value with
  | Expression e -> walk_expression predicate acc e
  | Let s ->
      let acc = add_if predicate (LetStmt s) acc in
      let acc =
        match s.value.ty with
        | None -> acc
        | Some t -> walk_haven_type predicate acc t
      in
      walk_expression predicate acc s.value.init_expr
  | Return (Some e) -> walk_expression predicate acc e
  | Return None -> acc
  | Defer e -> walk_expression predicate acc e
  | Iter i ->
      let acc = add_if predicate (IterStmt i) acc in
      let acc = add_if predicate (IterRange i.value.range) acc in
      let acc = walk_expression predicate acc i.value.range.value.range_start in
      let acc = walk_expression predicate acc i.value.range.value.range_end in
      let acc =
        match i.value.range.value.range_incr with
        | None -> acc
        | Some e -> walk_expression predicate acc e
      in
      walk_block predicate acc i.value.body
  | While w ->
      let acc = add_if predicate (WhileStmt w) acc in
      let acc = walk_expression predicate acc w.value.cond in
      walk_block predicate acc w.value.body
  | Break | Continue | Empty -> acc

and walk_block predicate acc block =
  let acc = add_if predicate (Block block) acc in
  List.fold_left (walk_block_item predicate) acc block.value.items

and walk_block_item predicate acc item =
  let acc = add_if predicate (BlockItem item) acc in
  match item.value with
  | BlockStatement s -> walk_statement predicate acc s
  | BlockExpression e -> walk_expression predicate acc e

and walk_struct_decl predicate acc decl =
  let acc = add_if predicate (StructDecl decl) acc in
  List.fold_left
    (fun acc f -> add_if predicate (StructField f) acc)
    acc decl.value.fields

and walk_enum_decl predicate acc decl =
  let acc = add_if predicate (EnumDecl decl) acc in
  List.fold_left
    (fun acc v -> add_if predicate (EnumVariant v) acc)
    acc decl.value.variants

and walk_type_decl predicate acc ty =
  let acc = add_if predicate (TypeDecl ty) acc in
  match ty.value.data with
  | TypeDeclAlias t -> walk_haven_type predicate acc t
  | TypeDeclStruct s -> walk_struct_decl predicate acc s
  | TypeDeclEnum e -> walk_enum_decl predicate acc e
  | TypeDeclForward -> acc

and walk_var_decl predicate acc v =
  let acc = add_if predicate (VarDecl v) acc in
  let acc = walk_haven_type predicate acc v.value.ty in
  match v.value.init_expr with
  | None -> acc
  | Some e -> walk_expression predicate acc e

and walk_function_decl predicate acc fn =
  let acc = add_if predicate (FunctionDecl fn) acc in
  let acc = add_if predicate (ParamList fn.value.params) acc in
  let acc =
    List.fold_left (fun acc p -> add_if predicate (Param p) acc) acc
      fn.value.params.value.params
  in
  let acc =
    match fn.value.return_type with
    | None -> acc
    | Some t -> walk_haven_type predicate acc t
  in
  let acc =
    match fn.value.intrinsic with
    | None -> acc
    | Some i ->
        let acc = add_if predicate (Intrinsic i) acc in
        List.fold_left (walk_haven_type predicate) acc i.value.types
  in
  let acc =
    match fn.value.definition with
    | None -> acc
    | Some block -> walk_block predicate acc block
  in
  acc

and walk_top_decl predicate acc decl =
  let acc = add_if predicate (TopDecl decl) acc in
  match decl.value with
  | FDecl f -> walk_function_decl predicate acc f
  | VDecl v -> walk_var_decl predicate acc v
  | TDecl t -> walk_type_decl predicate acc t
  | Import _ | CImport _ -> acc
  | Foreign f ->
      let acc = add_if predicate (Foreign f) acc in
      List.fold_left (walk_function_decl predicate) acc f.value.decls

let walk_program predicate acc program =
  let acc = add_if predicate (Program program) acc in
  List.fold_left (walk_top_decl predicate) acc program.value.decls

let nodes_matching predicate (program : program) =
  walk_program predicate [] program |> List.rev

let nodes_at_position pos program =
  nodes_matching (fun node -> contains_position (location_of node) pos) program

let nodes_overlapping_range range program =
  nodes_matching (fun node -> overlaps_range (location_of node) range) program
