open Analysis_types

module ConstantFold = Analysis_cfold.ConstantFold

module Assert = struct
  type result = {
    program : Core.parsed_program;
    diagnostics : diagnostic list;
  }

  type state = {
    typed : typing_result;
    mutable diagnostics_rev : diagnostic list;
  }

  let expr_annotation state (expr : Core.expression) =
    Hashtbl.find_opt state.typed.annotations.exprs (expr_id expr)

  let add_diagnostic state loc message =
    state.diagnostics_rev <-
      { category = Semantic; level = Error; loc; message } :: state.diagnostics_rev

  let exact_integer_of_annotation (ann : expr_annotation) =
    Option.bind ann.metavar.integer (fun integer -> integer.exact_value)

  let exact_integer state (expr : Core.expression) =
    Option.bind (expr_annotation state expr) exact_integer_of_annotation

  let binary_op_string = function
    | Core.Add -> "+"
    | Core.Subtract -> "-"
    | Core.Multiply -> "*"
    | Core.Divide -> "/"
    | Core.Modulo -> "%"
    | Core.LeftShift -> "<<"
    | Core.RightShift -> ">>"
    | Core.IsEqual -> "=="
    | Core.NotEqual -> "!="
    | Core.LessThan -> "<"
    | Core.LessThanOrEqual -> "<="
    | Core.GreaterThan -> ">"
    | Core.GreaterThanOrEqual -> ">="
    | Core.BitwiseAnd -> "&"
    | Core.BitwiseXor -> "^"
    | Core.BitwiseOr -> "|"
    | Core.LogicAnd -> "&&"
    | Core.LogicOr -> "||"

  let binary_precedence = function
    | Core.LogicOr -> 1
    | Core.LogicAnd -> 2
    | Core.BitwiseOr -> 3
    | Core.BitwiseXor -> 4
    | Core.BitwiseAnd -> 5
    | Core.IsEqual | Core.NotEqual -> 6
    | Core.LessThan
    | Core.LessThanOrEqual
    | Core.GreaterThan
    | Core.GreaterThanOrEqual ->
        7
    | Core.LeftShift | Core.RightShift -> 8
    | Core.Add | Core.Subtract -> 9
    | Core.Multiply | Core.Divide | Core.Modulo -> 10

  let render_literal (literal : Core.literal) =
    match literal.value with
    | Core.Integer value -> string_of_int value
    | Core.Bool value -> if value then "true" else "false"
    | Core.Float value ->
        let rendered = string_of_float value in
        if String.contains rendered '.' then rendered else rendered ^ ".0"
    | Core.String value -> Printf.sprintf "%S" value
    | Core.Char value -> Printf.sprintf "%C" value
    | Core.Vector _ -> "Vec<...>"
    | Core.Matrix _ -> "Mat<...>"
    | Core.Enum enum -> enum.value.enum_variant.value

  let rec render_expression ?(ctx_prec = 0) (expr : Core.expression) =
    let self_prec =
      match expr.value with
      | Core.Binary binary -> binary_precedence binary.value.op
      | Core.Unary _ -> 11
      | Core.Call _ | Core.Index _ | Core.Field _ -> 12
      | _ -> 13
    in
    let rendered =
      match expr.value with
      | Core.Literal literal -> render_literal literal
      | Core.Identifier id -> id.value
      | Core.Binary binary ->
          let prec = binary_precedence binary.value.op in
          Printf.sprintf "%s %s %s"
            (render_expression ~ctx_prec:prec binary.value.left)
            (binary_op_string binary.value.op)
            (render_expression ~ctx_prec:(prec + 1) binary.value.right)
      | Core.Unary unary ->
          let op =
            match unary.value.op with
            | Core.Not -> "!"
            | Core.Negate -> "-"
            | Core.Complement -> "~"
          in
          op ^ render_expression ~ctx_prec:11 unary.value.inner
      | Core.ToBool inner ->
          Printf.sprintf "bool(%s)" (render_expression inner)
      | Core.Call call ->
          Printf.sprintf "%s(%s)"
            (render_expression ~ctx_prec:12 call.value.target)
            (String.concat ", " (List.map render_expression call.value.params))
      | Core.Index index ->
          Printf.sprintf "%s[%s]"
            (render_expression ~ctx_prec:12 index.value.target)
            (render_expression index.value.index)
      | Core.Field field ->
          Printf.sprintf "%s%s%s"
            (render_expression ~ctx_prec:12 field.value.target)
            (if field.value.arrow then "->" else ".")
            field.value.field.value
      | Core.As cast ->
          Printf.sprintf "as<...>(%s)" (render_expression cast.value.inner)
      | Core.SizeExpr inner ->
          Printf.sprintf "size(%s)" (render_expression inner)
      | Core.Block _ -> "{ ... }"
      | Core.Initializer _ -> "{ ... }"
      | Core.Match _ -> "match { ... }"
      | Core.BoxExpr inner -> "box " ^ render_expression ~ctx_prec:11 inner
      | Core.BoxType _ -> "box <type>"
      | Core.BoxConstruct box ->
          Printf.sprintf "box <type>(%s)"
            (String.concat ", " (List.map render_expression box.value.args))
      | Core.Unbox inner -> "unbox " ^ render_expression ~ctx_prec:11 inner
      | Core.Ref inner -> "ref " ^ render_expression ~ctx_prec:11 inner
      | Core.Load inner -> "load " ^ render_expression ~ctx_prec:11 inner
      | Core.Assign write ->
          Printf.sprintf "%s = %s" (render_expression write.value.target)
            (render_expression write.value.value)
      | Core.Mutate write ->
          Printf.sprintf "%s := %s" (render_expression write.value.target)
            (render_expression write.value.value)
      | Core.SizeType _ -> "size<type>"
      | Core.Nil -> "nil"
    in
    if self_prec < ctx_prec then "(" ^ rendered ^ ")" else rendered

  let rec render_specialized_expression ?(ctx_prec = 0) state (expr : Core.expression) =
    match expr.value with
    | Core.Field _ -> (
        match exact_integer state expr with
        | Some value -> string_of_int value
        | None -> render_expression ~ctx_prec expr)
    | Core.Binary binary ->
        let prec = binary_precedence binary.value.op in
        let rendered =
          Printf.sprintf "%s %s %s"
            (render_specialized_expression ~ctx_prec:prec state binary.value.left)
            (binary_op_string binary.value.op)
            (render_specialized_expression ~ctx_prec:(prec + 1) state binary.value.right)
        in
        if prec < ctx_prec then "(" ^ rendered ^ ")" else rendered
    | Core.Unary unary ->
        let op =
          match unary.value.op with
          | Core.Not -> "!"
          | Core.Negate -> "-"
          | Core.Complement -> "~"
        in
        let rendered = op ^ render_specialized_expression ~ctx_prec:11 state unary.value.inner in
        if 11 < ctx_prec then "(" ^ rendered ^ ")" else rendered
    | Core.Call call ->
        Printf.sprintf "%s(%s)"
          (render_specialized_expression ~ctx_prec:12 state call.value.target)
          (String.concat ", "
             (List.map
                (fun expr -> render_specialized_expression state expr)
                call.value.params))
    | Core.Index index ->
        Printf.sprintf "%s[%s]"
          (render_specialized_expression ~ctx_prec:12 state index.value.target)
          (render_specialized_expression state index.value.index)
    | Core.ToBool inner ->
        Printf.sprintf "bool(%s)" (render_specialized_expression state inner)
    | Core.As cast ->
        Printf.sprintf "as<...>(%s)"
          (render_specialized_expression state cast.value.inner)
    | Core.SizeExpr inner ->
        Printf.sprintf "size(%s)" (render_specialized_expression state inner)
    | Core.BoxExpr inner ->
        "box " ^ render_specialized_expression ~ctx_prec:11 state inner
    | Core.Unbox inner ->
        "unbox " ^ render_specialized_expression ~ctx_prec:11 state inner
    | Core.Ref inner ->
        "ref " ^ render_specialized_expression ~ctx_prec:11 state inner
    | Core.Load inner ->
        "load " ^ render_specialized_expression ~ctx_prec:11 state inner
    | Core.Assign write ->
        Printf.sprintf "%s = %s"
          (render_specialized_expression state write.value.target)
          (render_specialized_expression state write.value.value)
    | Core.Mutate write ->
        Printf.sprintf "%s := %s"
          (render_specialized_expression state write.value.target)
          (render_specialized_expression state write.value.value)
    | Core.BoxConstruct box ->
        Printf.sprintf "box <type>(%s)"
          (String.concat ", "
             (List.map
                (fun expr -> render_specialized_expression state expr)
                box.value.args))
    | (Core.Literal _ | Core.Identifier _ | Core.Block _ | Core.Initializer _ | Core.Match _
      | Core.BoxType _ | Core.SizeType _ | Core.Nil) ->
        render_expression ~ctx_prec expr

  let assert_context (compile_assert : Core.compile_assert) suffix =
    Printf.sprintf "compile-time assertion '%s' %s"
      (render_expression compile_assert.value.cond)
      suffix

  let assert_failure_message state (compile_assert : Core.compile_assert) message =
    let message =
      if String.equal message "" then "compile-time assertion failed" else message
    in
    let source = render_expression compile_assert.value.cond in
    let specialized = render_specialized_expression state compile_assert.value.cond in
    if String.equal source specialized then
      Printf.sprintf "%s\n  compile-time assertion failed: %s" message source
    else
      Printf.sprintf
        "%s\n  compile-time assertion failed: %s\n  specialized as: %s"
        message source specialized

  let constant_of_annotation (ann : expr_annotation) =
    match ann.metavar.constant with
    | Some constant -> Some constant
    | None -> (
        match (ann.resolved_type, ann.metavar.integer) with
        | Some (ResolvedInt _), Some { exact_value = Some value; _ } ->
            Some (ConstantInt value)
        | _ -> None)

  let rec constant_of_expr state (expr : Core.expression) =
    match expr_annotation state expr with
    | Some ann -> (
        match constant_of_annotation ann with
        | Some constant -> Some constant
        | None -> constant_of_expr_desc state expr)
    | None -> constant_of_expr_desc state expr

  and constant_of_expr_desc state (expr : Core.expression) =
    match expr.value with
    | Core.Literal literal -> ConstantFold.constant_of_literal literal
    | Core.Unary unary ->
        Option.bind (constant_of_expr state unary.value.inner) (fun inner ->
            ConstantFold.fold_unary unary.value.op inner)
    | Core.Binary binary ->
        Option.bind (constant_of_expr state binary.value.left) (fun left ->
            Option.bind (constant_of_expr state binary.value.right) (fun right ->
                ConstantFold.fold_binary binary.value.op left right))
    | Core.ToBool inner ->
        Option.bind (constant_of_expr state inner) ConstantFold.truthy_of_constant
        |> Option.map (fun value -> ConstantBool value)
    | Core.Block block when block.value.statements = [] ->
        Option.bind block.value.result (constant_of_expr state)
    | _ -> None

  let assert_message (compile_assert : Core.compile_assert) fallback =
    let message = compile_assert.value.message.value in
    if String.equal message "" then assert_context compile_assert fallback
    else Printf.sprintf "%s (%s)" message (assert_context compile_assert fallback)

  let eval_assert_condition state (compile_assert : Core.compile_assert) =
    match constant_of_expr state compile_assert.value.cond with
    | Some constant -> (
        match ConstantFold.truthy_of_constant constant with
        | Some true -> true
        | Some false ->
            add_diagnostic state compile_assert.loc
              (assert_failure_message state compile_assert
                 compile_assert.value.message.value);
            false
        | None ->
            add_diagnostic state compile_assert.loc
              (assert_message compile_assert
                 "must be a scalar constant");
            false)
    | None ->
        add_diagnostic state compile_assert.loc
          (assert_message compile_assert "must be constant");
        false

  let rec rewrite_statement state (stmt : Core.statement) =
    match stmt.value with
    | Core.Expression _
    | Core.Return _
    | Core.Defer _
    | Core.Let _
    | Core.Break
    | Core.Continue ->
        [ stmt ]
    | Core.CompileAssert compile_assert ->
        ignore (eval_assert_condition state compile_assert);
        []
    | Core.Loop loop ->
        [
          {
            stmt with
            value =
              Core.Loop
                {
                  loop with
                  value =
                    {
                      loop.value with
                      init = List.concat_map (rewrite_statement state) loop.value.init;
                      body = rewrite_block state loop.value.body;
                      step = List.concat_map (rewrite_statement state) loop.value.step;
                    };
                };
          };
        ]

  and rewrite_statements state (statements : Core.statement list) =
    match statements with
    | [] -> []
    | stmt :: rest -> (
        match stmt.value with
        | Core.CompileAssert compile_assert ->
            if eval_assert_condition state compile_assert then rewrite_statements state rest
            else []
        | _ ->
            let stmt' = rewrite_statement state stmt in
            stmt' @ rewrite_statements state rest)

  and rewrite_block state (block : Core.block) =
    {
      block with
      value =
        {
          block.value with
          statements = rewrite_statements state block.value.statements;
        };
    }

  let rewrite_decl state (decl : Core.top_decl) =
    let value =
      match decl.value with
      | Core.FDecl fn ->
          Core.FDecl
            {
              fn with
              value =
                {
                  fn.value with
                  definition = Option.map (rewrite_block state) fn.value.definition;
                };
            }
      | Core.Foreign foreign ->
          Core.Foreign
            {
              foreign with
              value =
                {
                  foreign.value with
                  decls =
                    List.map
                      (fun (fn : Core.function_decl) ->
                        {
                          fn with
                          value =
                            {
                              fn.value with
                              definition = Option.map (rewrite_block state) fn.value.definition;
                            };
                        })
                      foreign.value.decls;
                };
            }
      | Core.VDecl binding ->
          Core.VDecl
            {
              binding with
              value =
                {
                  binding.value with
                  init_expr = Option.map (fun expr -> expr) binding.value.init_expr;
                };
            }
      | (Core.TDecl _ | Core.Import _ | Core.CImport _) as value -> value
    in
    { decl with value }

  let run (typed : typing_result) =
    let state = { typed; diagnostics_rev = [] } in
    let program =
      {
        Core.program =
          {
            typed.program.program with
            value =
              {
                Core.decls =
                  List.map (rewrite_decl state) typed.program.program.value.decls;
              };
          };
      }
    in
    {
      program;
      diagnostics = List.rev state.diagnostics_rev;
    }
end
