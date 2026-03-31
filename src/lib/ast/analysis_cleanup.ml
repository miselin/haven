open Analysis_types


module Cleanup = struct
  let expr_annotation typed expr =
    Hashtbl.find_opt typed.annotations.exprs (expr_id expr)

  let rec clean_write_like typed make_ctor (write : Core.write) =
    make_ctor
      {
        write with
        value =
          {
            Core.target = clean_expression typed write.value.target;
            value = clean_expression typed write.value.value;
          };
      }

  and clean_expression typed (expr : Core.expression) : Core.expression =
    let cleaned_value =
      match expr.value with
      | Core.Binary binary ->
          Core.Binary
            {
              binary with
              value =
                {
                  binary.value with
                  left = clean_expression typed binary.value.left;
                  right = clean_expression typed binary.value.right;
                };
            }
      | Core.Unary unary ->
          Core.Unary
            {
              unary with
              value = { unary.value with inner = clean_expression typed unary.value.inner };
            }
      | Core.Block block ->
          let block : Core.block = clean_block typed block in
          if block.value.statements = [] then
            match block.value.result with
            | Some result -> result.value
            | None -> Core.Block block
          else Core.Block block
      | Core.ToBool inner ->
          let inner : Core.expression = clean_expression typed inner in
          let inner_is_bool =
            match expr_annotation typed inner with
            | Some { inferred_type = Some ty; _ } -> is_boolean_type ty
            | Some { inferred_type = None; _ } | None -> false
          in
          if inner_is_bool then inner.value else Core.ToBool inner
      | Core.Initializer init ->
          Core.Initializer
            {
              init with
              value = { Core.exprs = List.map (clean_expression typed) init.value.exprs };
            }
      | Core.As cast ->
          let inner : Core.expression = clean_expression typed cast.value.inner in
          let redundant =
            match expr_annotation typed inner with
            | Some { inferred_type = Some ty; _ } -> equal_type ty cast.value.target_type
            | _ -> false
          in
          if redundant then inner.value
          else
            Core.As { cast with value = { cast.value with inner } }
      | Core.SizeExpr inner -> Core.SizeExpr (clean_expression typed inner)
      | Core.Match match_expr ->
          Core.Match
            {
              match_expr with
              value =
                {
                  Core.expr = clean_expression typed match_expr.value.expr;
                  arms =
                    List.map
                      (fun (arm : Core.match_arm) ->
                        {
                          arm with
                          value =
                            {
                              arm.value with
                              expr = clean_expression typed arm.value.expr;
                            };
                        })
                      match_expr.value.arms;
                };
            }
      | Core.BoxExpr inner -> Core.BoxExpr (clean_expression typed inner)
      | Core.BoxConstruct box ->
          Core.BoxConstruct
            {
              box with
              value = { box.value with args = List.map (clean_expression typed) box.value.args };
            }
      | Core.Unbox inner -> Core.Unbox (clean_expression typed inner)
      | Core.Ref inner -> Core.Ref (clean_expression typed inner)
      | Core.Load inner -> Core.Load (clean_expression typed inner)
      | Core.Call call ->
          Core.Call
            {
              call with
              value =
                {
                  Core.target = clean_expression typed call.value.target;
                  params = List.map (clean_expression typed) call.value.params;
                };
            }
      | Core.Index index ->
          Core.Index
            {
              index with
              value =
                {
                  Core.target = clean_expression typed index.value.target;
                  index = clean_expression typed index.value.index;
                };
            }
      | Core.Field field ->
          Core.Field
            {
              field with
              value =
                { field.value with target = clean_expression typed field.value.target };
            }
      | Core.Assign write -> clean_write_like typed (fun write -> Core.Assign write) write
      | Core.Mutate write -> clean_write_like typed (fun write -> Core.Mutate write) write
      | (Core.Identifier _ | Core.Literal _ | Core.SizeType _ | Core.Nil | Core.BoxType _) as
          value ->
          value
    in
    { expr with value = cleaned_value }

  and clean_statement typed (stmt : Core.statement) : Core.statement =
    let value =
      match stmt.value with
      | Core.Expression expr -> Core.Expression (clean_expression typed expr)
      | Core.CompileAssert compile_assert ->
          Core.CompileAssert
            {
              compile_assert with
              value =
                {
                  compile_assert.value with
                  cond = clean_expression typed compile_assert.value.cond;
                };
            }
      | Core.Return expr -> Core.Return (Option.map (clean_expression typed) expr)
      | Core.Defer expr -> Core.Defer (clean_expression typed expr)
      | Core.Let binding ->
          Core.Let
            {
              binding with
              value =
                {
                  binding.value with
                  init_expr = clean_expression typed binding.value.init_expr;
                };
            }
      | Core.Loop loop ->
          Core.Loop
            {
              loop with
              value =
                {
                  loop.value with
                  init = List.map (clean_statement typed) loop.value.init;
                  cond = clean_expression typed loop.value.cond;
                  body = clean_block typed loop.value.body;
                  step = List.map (clean_statement typed) loop.value.step;
                };
            }
      | Core.Break | Core.Continue as value -> value
    in
    { stmt with value }

  and clean_block typed (block : Core.block) : Core.block =
    {
      block with
      value =
        {
          Core.statements = List.map (clean_statement typed) block.value.statements;
          result = Option.map (clean_expression typed) block.value.result;
        };
    }

  let clean_decl typed decl =
    let value =
      match (decl : Core.top_decl).value with
      | Core.FDecl fn ->
          Core.FDecl
            {
              fn with
              value =
                {
                  fn.value with
                  definition = Option.map (clean_block typed) fn.value.definition;
                };
            }
      | Core.VDecl binding ->
          Core.VDecl
            {
              binding with
              value =
                {
                  binding.value with
                  init_expr = Option.map (clean_expression typed) binding.value.init_expr;
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
                              definition =
                                Option.map (clean_block typed) fn.value.definition;
                            };
                        })
                      foreign.value.decls;
                };
            }
      | (Core.TDecl _ | Core.Import _ | Core.CImport _) as value -> value
    in
    { decl with value }

  let run ?program typed =
    let program = Option.value ~default:typed.program program in
    {
      Core.program =
        {
          program.program with
          value =
            { Core.decls = List.map (clean_decl typed) program.program.value.decls };
        };
    }
end
