open Analysis_types

module ConstantFold = struct
  type numeric_pair =
    | IntPair of int * int
    | FloatPair of float * float

  let int_of_int64_opt value =
    try
      let roundtrip = Int64.to_int value in
      if Int64.of_int roundtrip = value then Some roundtrip else None
    with Invalid_argument _ -> None

  let constant_int_of_int64 value = Option.map (fun value -> ConstantInt value) (int_of_int64_opt value)

  let fold_numeric_pair left right (int_op : int64 -> int64 -> int64) (float_op : float -> float -> float) :
      constant_value option =
    match (left, right) with
    | ConstantInt l, ConstantInt r -> constant_int_of_int64 (int_op (Int64.of_int l) (Int64.of_int r))
    | ConstantFloat l, ConstantFloat r -> Some (ConstantFloat (float_op l r))
    | ConstantInt l, ConstantFloat r -> Some (ConstantFloat (float_op (float_of_int l) r))
    | ConstantFloat l, ConstantInt r -> Some (ConstantFloat (float_op l (float_of_int r)))
    | _ -> None

  let fold_int_pair left right f =
    match (left, right) with
    | ConstantInt l, ConstantInt r -> constant_int_of_int64 (f (Int64.of_int l) (Int64.of_int r))
    | _ -> None

  let literal_of_constant loc = function
    | ConstantInt value -> Some (mk_expr loc (Core.Literal (mk_literal loc (Core.Integer value))))
    | ConstantBool value -> Some (mk_expr loc (Core.Literal (mk_literal loc (Core.Bool value))))
    | ConstantFloat value -> Some (mk_expr loc (Core.Literal (mk_literal loc (Core.Float value))))
    | ConstantString value -> Some (mk_expr loc (Core.Literal (mk_literal loc (Core.String value))))
    | ConstantChar value -> Some (mk_expr loc (Core.Literal (mk_literal loc (Core.Char value))))

  let constant_of_literal (literal : Core.literal) =
    match literal.value with
    | Core.Integer value -> Some (ConstantInt value)
    | Core.Bool value -> Some (ConstantBool value)
    | Core.Float value -> Some (ConstantFloat value)
    | Core.String value -> Some (ConstantString value)
    | Core.Char value -> Some (ConstantChar value)
    | Core.Matrix _ | Core.Vector _ | Core.Enum _ -> None

  let truthy_of_constant = function
    | ConstantBool value -> Some value
    | ConstantInt value -> Some (value <> 0)
    | ConstantFloat value -> Some (value <> 0.0)
    | ConstantChar value -> Some (Char.code value <> 0)
    | ConstantString _ -> Some true

  let numeric_pair left right =
    match (left, right) with
    | ConstantInt l, ConstantInt r -> Some (IntPair (l, r))
    | ConstantFloat l, ConstantFloat r -> Some (FloatPair (l, r))
    | ConstantInt l, ConstantFloat r -> Some (FloatPair (float_of_int l, r))
    | ConstantFloat l, ConstantInt r -> Some (FloatPair (l, float_of_int r))
    | _ -> None

  let order_pair left right =
    match numeric_pair left right with
    | Some (IntPair (l, r)) -> Some (IntPair (l, r))
    | Some (FloatPair (l, r)) -> Some (FloatPair (l, r))
    | None -> (
        match (left, right) with
        | ConstantChar l, ConstantChar r -> Some (IntPair (Char.code l, Char.code r))
        | _ -> None)

  let equality_result left right =
    match (left, right) with
    | ConstantInt _, ConstantInt _
    | ConstantBool _, ConstantBool _
    | ConstantFloat _, ConstantFloat _
    | ConstantString _, ConstantString _
    | ConstantChar _, ConstantChar _ ->
        Some (left = right)
    | _ -> None

  let fold_unary op inner =
    match (op, inner) with
    | Core.Not, _ -> Option.map (fun value -> ConstantBool (not value)) (truthy_of_constant inner)
    | Core.Negate, ConstantInt value -> constant_int_of_int64 (Int64.neg (Int64.of_int value))
    | Core.Negate, ConstantFloat value -> Some (ConstantFloat (-.value))
    | Core.Complement, ConstantInt value -> constant_int_of_int64 (Int64.lognot (Int64.of_int value))
    | _ -> None

  let fold_binary op left right =
    match op with
    | Core.Add -> (
        match fold_numeric_pair left right Int64.add ( +. ) with
        | Some value -> Some value
        | None -> None)
    | Core.Subtract -> (
        match fold_numeric_pair left right Int64.sub ( -. ) with
        | Some value -> Some value
        | None -> None)
    | Core.Multiply -> (
        match fold_numeric_pair left right Int64.mul ( *. ) with
        | Some value -> Some value
        | None -> None)
    | Core.Divide -> (
        match (left, right) with
        | ConstantInt _, ConstantInt 0 -> None
        | ConstantInt l, ConstantInt r ->
            constant_int_of_int64 (Int64.div (Int64.of_int l) (Int64.of_int r))
        | ConstantFloat _, ConstantFloat 0.0
        | ConstantInt _, ConstantFloat 0.0
        | ConstantFloat _, ConstantInt 0 ->
            None
        | ConstantFloat l, ConstantFloat r -> Some (ConstantFloat (l /. r))
        | ConstantInt l, ConstantFloat r -> Some (ConstantFloat (float_of_int l /. r))
        | ConstantFloat l, ConstantInt r -> Some (ConstantFloat (l /. float_of_int r))
        | _ -> None)
    | Core.Modulo -> (
        match (left, right) with
        | ConstantInt _, ConstantInt 0 -> None
        | ConstantInt l, ConstantInt r ->
            constant_int_of_int64 (Int64.rem (Int64.of_int l) (Int64.of_int r))
        | _ -> None)
    | Core.LeftShift -> (
        match (left, right) with
        | ConstantInt l, ConstantInt r when r >= 0 && r < 64 ->
            constant_int_of_int64 (Int64.shift_left (Int64.of_int l) r)
        | _ -> None)
    | Core.RightShift -> (
        match (left, right) with
        | ConstantInt l, ConstantInt r when r >= 0 && r < 64 ->
            constant_int_of_int64 (Int64.shift_right (Int64.of_int l) r)
        | _ -> None)
    | Core.BitwiseAnd -> (
        match fold_int_pair left right Int64.logand with
        | Some (ConstantInt value) -> Some (ConstantInt value)
        | _ -> None)
    | Core.BitwiseXor -> (
        match fold_int_pair left right Int64.logxor with
        | Some (ConstantInt value) -> Some (ConstantInt value)
        | _ -> None)
    | Core.BitwiseOr -> (
        match fold_int_pair left right Int64.logor with
        | Some (ConstantInt value) -> Some (ConstantInt value)
        | _ -> None)
    | Core.IsEqual -> (
        match equality_result left right with Some value -> Some (ConstantBool value) | None -> None)
    | Core.NotEqual -> (
        match equality_result left right with Some value -> Some (ConstantBool (not value)) | None -> None)
    | Core.LessThan -> (
        match order_pair left right with
        | Some (IntPair (l, r)) -> Some (ConstantBool (l < r))
        | Some (FloatPair (l, r)) -> Some (ConstantBool (l < r))
        | None -> None)
    | Core.LessThanOrEqual -> (
        match order_pair left right with
        | Some (IntPair (l, r)) -> Some (ConstantBool (l <= r))
        | Some (FloatPair (l, r)) -> Some (ConstantBool (l <= r))
        | None -> None)
    | Core.GreaterThan -> (
        match order_pair left right with
        | Some (IntPair (l, r)) -> Some (ConstantBool (l > r))
        | Some (FloatPair (l, r)) -> Some (ConstantBool (l > r))
        | None -> None)
    | Core.GreaterThanOrEqual -> (
        match order_pair left right with
        | Some (IntPair (l, r)) -> Some (ConstantBool (l >= r))
        | Some (FloatPair (l, r)) -> Some (ConstantBool (l >= r))
        | None -> None)
    | Core.LogicAnd -> (
        match (truthy_of_constant left, truthy_of_constant right) with
        | Some l, Some r -> Some (ConstantBool (l && r))
        | _ -> None)
    | Core.LogicOr -> (
        match (truthy_of_constant left, truthy_of_constant right) with
        | Some l, Some r -> Some (ConstantBool (l || r))
        | _ -> None)

  let rec constant_of_expr (expr : Core.expression) =
    match expr.value with
    | Core.Literal literal -> constant_of_literal literal
    | Core.Unary unary ->
        Option.bind (constant_of_expr unary.value.inner) (fun inner ->
            fold_unary unary.value.op inner)
    | Core.Binary binary ->
        Option.bind (constant_of_expr binary.value.left) (fun left ->
            Option.bind (constant_of_expr binary.value.right) (fun right ->
                fold_binary binary.value.op left right))
    | Core.ToBool inner ->
        Option.bind (constant_of_expr inner) truthy_of_constant |> Option.map (fun value -> ConstantBool value)
    | Core.Block block when block.value.statements = [] ->
        Option.bind block.value.result constant_of_expr
    | _ -> None

  let rec fold_literal (literal : Core.literal) =
    match literal.value with
    | Core.Matrix mat -> mk_literal literal.loc (Core.Matrix (fold_mat_literal mat))
    | Core.Vector vec -> mk_literal literal.loc (Core.Vector (fold_vec_literal vec))
    | Core.Enum enum -> mk_literal literal.loc (Core.Enum (fold_enum_literal enum))
    | (Core.Integer _ | Core.Bool _ | Core.Float _ | Core.String _ | Core.Char _) as value ->
        mk_literal literal.loc value

  and fold_vec_literal (vec : Core.vec_literal) =
    { loc = vec.loc; value = { elements = List.map fold_expression vec.value.elements } }

  and fold_mat_literal (mat : Core.mat_literal) =
    { loc = mat.loc; value = { rows = List.map fold_vec_literal mat.value.rows } }

  and fold_enum_literal (enum : Core.enum_literal) =
    { enum with value = { enum.value with wrapped = List.map fold_expression enum.value.wrapped } }

  and fold_expression (expr : Core.expression) : Core.expression =
    let value =
      match expr.value with
      | Core.Binary binary ->
          Core.Binary
            {
              binary with
              value =
                {
                  binary.value with
                  left = fold_expression binary.value.left;
                  right = fold_expression binary.value.right;
                };
            }
      | Core.Unary unary ->
          Core.Unary
            {
              unary with
              value = { unary.value with inner = fold_expression unary.value.inner };
            }
      | Core.Block block -> Core.Block (fold_block block)
      | Core.ToBool inner -> Core.ToBool (fold_expression inner)
      | Core.Initializer init ->
          Core.Initializer
            { init with value = { Core.exprs = List.map fold_expression init.value.exprs } }
      | Core.As cast ->
          Core.As
            { cast with value = { cast.value with inner = fold_expression cast.value.inner } }
      | Core.SizeExpr inner -> Core.SizeExpr (fold_expression inner)
      | Core.Match match_expr ->
          Core.Match
            {
              match_expr with
              value =
                {
                  Core.expr = fold_expression match_expr.value.expr;
                  arms =
                    List.map
                      (fun (arm : Core.match_arm) ->
                        { arm with value = { arm.value with expr = fold_expression arm.value.expr } })
                      match_expr.value.arms;
                };
            }
      | Core.BoxExpr inner -> Core.BoxExpr (fold_expression inner)
      | Core.Unbox inner -> Core.Unbox (fold_expression inner)
      | Core.Ref inner -> Core.Ref (fold_expression inner)
      | Core.Load inner -> Core.Load (fold_expression inner)
      | Core.Call call ->
          Core.Call
            {
              call with
              value =
                {
                  Core.target = fold_expression call.value.target;
                  params = List.map fold_expression call.value.params;
                };
            }
      | Core.Index index ->
          Core.Index
            {
              index with
              value =
                {
                  Core.target = fold_expression index.value.target;
                  index = fold_expression index.value.index;
                };
            }
      | Core.Field field ->
          Core.Field
            { field with value = { field.value with target = fold_expression field.value.target } }
      | Core.Assign write -> Core.Assign (fold_write write)
      | Core.Mutate write -> Core.Mutate (fold_write write)
      | Core.Literal literal -> Core.Literal (fold_literal literal)
      | (Core.Identifier _ | Core.SizeType _ | Core.Nil | Core.BoxType _) as value -> value
    in
    let expr = { expr with value } in
    match expr.value with
    | Core.Literal _ | Core.Identifier _ | Core.Nil | Core.SizeType _ | Core.BoxType _ -> expr
    | Core.Block block when block.value.statements = [] -> (
        match block.value.result with Some result -> result | None -> expr)
    | Core.ToBool inner -> (
        match constant_of_expr inner with
        | Some constant -> (
            match literal_of_constant expr.loc constant with Some folded -> folded | None -> expr)
        | None -> expr)
    | Core.Unary unary -> (
        match constant_of_expr unary.value.inner with
        | Some constant -> (
            match fold_unary unary.value.op constant with
            | Some folded -> (
                match literal_of_constant expr.loc folded with Some lit -> lit | None -> expr)
            | None -> expr)
        | None -> expr)
    | Core.Binary binary -> (
        match (constant_of_expr binary.value.left, constant_of_expr binary.value.right) with
        | Some left, Some right -> (
            match fold_binary binary.value.op left right with
            | Some folded -> (
                match literal_of_constant expr.loc folded with Some lit -> lit | None -> expr)
            | None -> expr)
        | _ -> expr)
    | _ -> expr

  and fold_write (write : Core.write) =
    {
      write with
      value =
        {
          Core.target = fold_expression write.value.target;
          value = fold_expression write.value.value;
        };
    }

  and fold_statement (stmt : Core.statement) =
    let value =
      match stmt.value with
      | Core.Expression expr -> Core.Expression (fold_expression expr)
      | Core.Return expr -> Core.Return (Option.map fold_expression expr)
      | Core.Defer expr -> Core.Defer (fold_expression expr)
      | Core.Let binding ->
          Core.Let
            {
              binding with
              value = { binding.value with init_expr = fold_expression binding.value.init_expr };
            }
      | Core.Loop loop ->
          Core.Loop
            {
              loop with
              value =
                {
                  loop.value with
                  init = List.map fold_statement loop.value.init;
                  cond = fold_expression loop.value.cond;
                  body = fold_block loop.value.body;
                  step = List.map fold_statement loop.value.step;
                };
            }
      | Core.Break | Core.Continue as value -> value
    in
    { stmt with value }

  and fold_block (block : Core.block) =
    {
      loc = block.loc;
      value =
        {
          statements = List.map fold_statement block.value.statements;
          result = Option.map fold_expression block.value.result;
        };
    }

  let fold_function_decl (fn : Core.function_decl) =
    {
      fn with
      value =
        { fn.value with definition = Option.map fold_block fn.value.definition };
    }

  let fold_top_decl = function
    | ({ loc; value = Core.FDecl fn } : Core.top_decl) ->
        ({ loc; value = Core.FDecl (fold_function_decl fn) } : Core.top_decl)
    | ({ loc; value = Core.Foreign foreign } : Core.top_decl) ->
        ({
          loc;
          value =
            Core.Foreign
              { foreign with value = { foreign.value with decls = List.map fold_function_decl foreign.value.decls } };
        } : Core.top_decl)
    | ({ loc; value = Core.VDecl binding } : Core.top_decl) ->
        ({
          loc;
          value =
            Core.VDecl
              { binding with value = { binding.value with init_expr = Option.map fold_expression binding.value.init_expr } };
        } : Core.top_decl)
    | ({ loc; value = (Core.TDecl _ | Core.Import _ | Core.CImport _) as value } : Core.top_decl) ->
        ({ loc; value } : Core.top_decl)

  let run (typed : typing_result) =
    {
      Core.program =
        {
          loc = typed.program.program.loc;
          value = { Core.decls = List.map fold_top_decl typed.program.program.value.decls };
        };
    }
end
