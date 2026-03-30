open Analysis_types

module Purity = struct
  type function_info = {
    fn : Core.function_decl;
    is_foreign : bool;
    mutable direct_effectful : bool;
    mutable callees : string list;
    mutable effectful : bool;
  }

  type state = {
    typed : typing_result;
    functions : function_info String_map.t;
    mutable diagnostics_rev : diagnostic list;
  }

  type env = string list list

  let add_diagnostic state loc message =
    state.diagnostics_rev <- { category = Purity; level = Error; loc; message } :: state.diagnostics_rev

  let expr_annotation state expr =
    Hashtbl.find_opt state.typed.annotations.exprs (expr_id expr)

  let expr_resolved_type state expr =
    match expr_annotation state expr with
    | Some { resolved_type = Some ty; _ } -> Some ty
    | _ -> None

  let push_scope env = [] :: env

  let bind_current env name =
    match env with [] -> [ [ name ] ] | scope :: rest -> (name :: scope) :: rest

  let rec lookup env name =
    match env with
    | [] -> false
    | scope :: rest -> if List.mem name scope then true else lookup rest name

  let function_info_of_decl ~is_foreign (fn : Core.function_decl) =
    {
      fn;
      is_foreign;
      direct_effectful = is_foreign;
      callees = [];
      effectful = is_foreign || fn.value.impure;
    }

  let collect_functions (program : Core.program) =
    List.fold_left
      (fun functions (decl : Core.top_decl) ->
        match decl.value with
        | Core.FDecl fn ->
            String_map.add fn.value.name.value
              (function_info_of_decl ~is_foreign:false fn)
              functions
        | Core.Foreign foreign ->
            List.fold_left
              (fun functions (fn : Core.function_decl) ->
                String_map.add fn.value.name.value
                  (function_info_of_decl ~is_foreign:true fn)
                  functions)
              functions foreign.value.decls
        | Core.TDecl _ | Core.VDecl _ | Core.Import _ | Core.CImport _ -> functions)
      String_map.empty program.value.decls

  let add_callee current name =
    match current with
    | None -> ()
    | Some info ->
        if not (List.mem name info.callees) then info.callees <- name :: info.callees

  let mark_direct_effect current =
    match current with
    | None -> ()
    | Some info -> info.direct_effectful <- true

  let visit_pattern_bindings (pattern : Core.match_pattern) =
    match pattern.value with
    | Core.PatternDefault | Core.PatternLiteral _ -> []
    | Core.PatternEnum enum ->
        List.filter_map
          (fun (binding : Core.pattern_binding) ->
            match binding.value with
            | Core.BindingIgnored -> None
            | Core.BindingNamed id -> Some id.value)
          enum.value.binding

  let rec visit_expression state current env (expr : Core.expression) =
    let visit = visit_expression state current in
    match expr.value with
    | Core.Identifier _ | Core.Literal _ | Core.SizeType _ | Core.BoxType _ | Core.Nil -> ()
    | Core.ToBool inner
    | Core.SizeExpr inner
    | Core.BoxExpr inner
    | Core.Unbox inner
    | Core.Ref inner ->
        visit env inner
    | Core.Load inner ->
        visit env inner
        ;
        mark_direct_effect current
    | Core.Unary unary -> visit env unary.value.inner
    | Core.Binary binary ->
        visit env binary.value.left;
        visit env binary.value.right
    | Core.Block block ->
        ignore (visit_block state current env block)
    | Core.Initializer init ->
        List.iter (visit env) init.value.exprs
    | Core.As cast -> visit env cast.value.inner
    | Core.Match match_expr ->
        visit env match_expr.value.expr;
        List.iter
          (fun (arm : Core.match_arm) ->
            let arm_env =
              List.fold_left
                (fun env name -> bind_current env name)
                (push_scope env)
                (visit_pattern_bindings arm.value.pattern)
            in
            visit arm_env arm.value.expr)
          match_expr.value.arms
    | Core.Call call ->
        visit env call.value.target;
        List.iter (visit env) call.value.params;
        classify_call state current env call
    | Core.Index index ->
        visit env index.value.target;
        visit env index.value.index;
        classify_index state current index
    | Core.Field field ->
        visit env field.value.target;
        classify_field state current field
    | Core.Assign write ->
        visit env write.value.target;
        visit env write.value.value
    | Core.Mutate write ->
        visit env write.value.target;
        visit env write.value.value;
        mark_direct_effect current

  and classify_call state current env (call : Core.call) =
    let target_type = expr_resolved_type state call.value.target in
    match call.value.target.value with
    | Core.Literal literal -> (
        match literal.value with
        | Core.Enum _ -> ()
        | _ -> (
            match target_type with
            | Some (ResolvedFunction _) | None -> mark_direct_effect current
            | _ -> ()))
    | Core.Identifier id ->
        if lookup env id.value then
          (match target_type with
        | Some (ResolvedFunction _) -> mark_direct_effect current
          | _ -> ())
        else if String_map.mem id.value state.functions then add_callee current id.value
        else
          (match target_type with
          | Some (ResolvedFunction _) | None -> mark_direct_effect current
          | _ -> ())
    | _ -> (
        match target_type with
        | Some (ResolvedFunction _) | None -> mark_direct_effect current
        | _ -> ())

  and classify_index state current (index : Core.index) =
    match expr_resolved_type state index.value.target with
    | Some (ResolvedPointer _ | ResolvedBox _ | ResolvedCell _) ->
        mark_direct_effect current
    | _ -> ()

  and classify_field state current (field : Core.field) =
    if field.value.arrow then (
      match expr_resolved_type state field.value.target with
      | Some (ResolvedPointer _ | ResolvedBox _ | ResolvedCell _) ->
          mark_direct_effect current
      | _ -> ())

  and visit_statement state current env (stmt : Core.statement) =
    match stmt.value with
    | Core.Expression expr ->
        visit_expression state current env expr;
        env
    | Core.Return expr ->
        Option.iter (visit_expression state current env) expr;
        env
    | Core.Defer expr ->
        visit_expression state current env expr;
        env
    | Core.Let binding ->
        visit_expression state current env binding.value.init_expr;
        bind_current env binding.value.name.value
    | Core.Loop loop ->
        let loop_env =
          List.fold_left (visit_statement state current) (push_scope env) loop.value.init
        in
        visit_expression state current loop_env loop.value.cond;
        ignore (visit_block state current loop_env loop.value.body);
        ignore (List.fold_left (visit_statement state current) loop_env loop.value.step);
        env
    | Core.Break | Core.Continue -> env

  and visit_block state current env (block : Core.block) =
    let block_env = push_scope env in
    let block_env = List.fold_left (visit_statement state current) block_env block.value.statements in
    Option.iter (visit_expression state current block_env) block.value.result;
    block_env

  let visit_function_body state (info : function_info) =
    match info.fn.value.definition with
    | None -> ()
    | Some body ->
        let env =
          let env = push_scope [] in
          let env =
            List.fold_left
              (fun env (param : Core.param) -> bind_current env param.value.name.value)
              env info.fn.value.params.value.params
          in
          env
        in
        ignore (visit_block state (Some info) env body)

  let rec propagate_effectfulness state =
    let changed = ref false in
    String_map.iter
      (fun _ info ->
        let next_effectful =
          info.direct_effectful
          || info.is_foreign
          || info.fn.value.impure
          || List.exists
               (fun callee ->
                 match String_map.find_opt callee state.functions with
                 | Some callee_info -> callee_info.effectful
                 | None -> false)
               info.callees
        in
        if next_effectful <> info.effectful then (
          info.effectful <- next_effectful;
          changed := true))
      state.functions;
    if !changed then propagate_effectfulness state

  let emit_diagnostics state =
    String_map.iter
      (fun _ info ->
        if info.effectful && not info.fn.value.impure && not info.is_foreign then
          add_diagnostic state info.fn.loc
            (Printf.sprintf "function %s performs side effects and must be marked impure"
               info.fn.value.name.value))
      state.functions

  let run typed : purity_result =
    let functions = collect_functions typed.program.program in
    let state = { typed; functions; diagnostics_rev = [] } in
    String_map.iter
      (fun _ info -> visit_function_body state info)
      state.functions;
    propagate_effectfulness state;
    emit_diagnostics state;
    { diagnostics = List.rev state.diagnostics_rev }
end
