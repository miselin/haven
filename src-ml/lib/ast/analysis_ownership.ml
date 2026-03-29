open Analysis_types
open Haven_core

module Ownership = struct
  type owned_value = {
    subject : ownership_subject;
    loc : Loc.t;
    resolved_type : resolved_ty;
  }

  type scope_kind =
    | FunctionScope of string
    | LoopScope of string
    | BlockScope of string

  type scope = {
    kind : scope_kind;
    owned : owned_value list;
  }

  type state = {
    typed : typing_result;
    type_env : type_env;
    mutable actions_rev : ownership_action list;
    mutable diagnostics_rev : diagnostic list;
  }

  let add_diagnostic state level loc message =
    state.diagnostics_rev <-
      { category = Ownership; level; loc; message } :: state.diagnostics_rev

  let expr_annotation state expr =
    Hashtbl.find_opt state.typed.annotations.exprs (expr_id expr)

  let expr_resolved_type state expr =
    match expr_annotation state expr with
    | Some { resolved_type = Some ty; _ } -> Some ty
    | _ -> None

  let binding_annotation state binding =
    Hashtbl.find_opt state.typed.annotations.bindings (binding_id binding)

  let push_scope kind scopes = { kind; owned = [] } :: scopes

  let add_owned_value scopes owned =
    match scopes with
    | [] -> []
    | scope :: rest -> { scope with owned = owned :: scope.owned } :: rest

  let emit_action state anchor kind reason subject loc resolved_type =
    state.actions_rev <-
      { anchor; kind; reason; subject; loc; resolved_type } :: state.actions_rev

  let emit_release_subject state anchor reason owned =
    emit_action state anchor Release reason owned.subject owned.loc
      (Some owned.resolved_type)

  let emit_scope_release state anchor reason (scope : scope) =
    List.iter (emit_release_subject state anchor reason) scope.owned

  let resolved_contains_ownership state loc ty =
    resolved_contains_box_ownership state.type_env [] loc ty

  let resolved_type_of_param state (param : Core.param) =
    resolve_core_type state.type_env [] [] param.loc param.value.ty

  let binding_resolved_type state (binding : Core.let_stmt) =
    match binding_annotation state binding with
    | Some { resolved_type = Some ty; _ } -> Some ty
    | Some _ | None -> None

  let emit_retain_expr state reason expected (expr : Core.expression) =
    emit_action state (AfterExpr (expr_id expr)) Retain reason
      (OwnershipExpr (expr_id expr, root_identifier_name expr))
      expr.loc
      (match expr_resolved_type state expr with Some ty -> Some ty | None -> Some expected)

  let rec emit_initializer_retains state reason slots exprs =
    match (slots, exprs) with
    | expected :: slot_rest, expr :: expr_rest ->
        emit_retains_for_transfer state reason expected expr;
        emit_initializer_retains state reason slot_rest expr_rest
    | _ -> ()

  and emit_enum_payload_retains state reason expected (enum_lit : Core.enum_literal) =
    match lookup_enum_variant state.type_env enum_lit.loc expected enum_lit.value.enum_variant.value with
    | Some (_, Some inner_ty) -> (
        match enum_lit.value.wrapped with
        | [ wrapped ] -> emit_retains_for_transfer state reason inner_ty wrapped
        | [] -> ()
        | _ ->
            add_diagnostic state Warning enum_lit.loc
              "ownership analysis only handles one enum payload value today")
    | Some (_, None) | None -> ()

  and emit_retains_for_call state reason (call : Core.call) =
    match expr_annotation state call.value.target with
    | Some { resolved_type = Some (ResolvedFunction (params, _, _)); _ } ->
        let rec loop params args =
          match (params, args) with
          | expected :: params_rest, arg :: args_rest ->
              emit_retains_for_transfer state reason expected arg;
              loop params_rest args_rest
          | _ -> ()
        in
        loop params call.value.params
    | Some { resolved_type = Some enum_ty; _ } -> (
        match call.value.target.value with
        | Core.Literal literal -> (
            match literal.value with
            | Core.Enum enum_lit ->
                emit_enum_payload_retains state reason enum_ty
                  { enum_lit with value = { enum_lit.value with wrapped = call.value.params } }
            | _ -> ())
        | _ -> ())
    | _ -> ()

  and emit_retains_for_expected_enum_call state reason expected (call : Core.call) =
    let emit_variant_retains variant_name =
      match lookup_enum_variant state.type_env call.loc expected variant_name with
      | Some (_, Some inner_ty) -> (
          match call.value.params with
          | [ wrapped ] -> emit_retains_for_transfer state reason inner_ty wrapped
          | [] -> ()
          | _ ->
              add_diagnostic state Warning call.loc
                "ownership analysis only handles one enum payload value today")
      | Some (_, None) | None -> ()
    in
    match call.value.target.value with
    | Core.Identifier id -> emit_variant_retains id.value
    | Core.Literal literal -> (
        match literal.value with
        | Core.Enum enum_lit -> emit_variant_retains enum_lit.value.enum_variant.value
        | _ -> ())
    | _ -> ()

  and emit_retains_for_transfer state reason expected (expr : Core.expression) =
    if not (resolved_contains_ownership state expr.loc expected) then ()
    else
      match expr.value with
      | Core.Nil -> ()
      | Core.BoxExpr _ | Core.BoxType _ -> ()
      | Core.Call call -> emit_retains_for_expected_enum_call state reason expected call
      | Core.As cast -> emit_retains_for_transfer state reason expected cast.value.inner
      | Core.Block block ->
          Option.iter (emit_retains_for_transfer state reason expected) block.value.result
      | Core.Match match_expr ->
          List.iter
            (fun (arm : Core.match_arm) ->
              emit_retains_for_transfer state reason expected arm.value.expr)
            match_expr.value.arms
      | Core.Initializer init -> (
          match expected with
          | ResolvedArray (inner_ty, count) ->
              emit_initializer_retains state reason
                (List.init count (fun _ -> inner_ty))
                init.value.exprs
          | ResolvedNamed _ as struct_ty -> (
              match lookup_struct_fields state.type_env expr.loc struct_ty with
              | Some fields ->
                  emit_initializer_retains state reason
                    (List.map snd fields)
                    init.value.exprs
              | None -> ())
          | _ -> ())
      | Core.Literal literal -> (
          match literal.value with
          | Core.Enum enum_lit -> emit_enum_payload_retains state reason expected enum_lit
          | _ -> emit_retain_expr state reason expected expr)
      | _ -> emit_retain_expr state reason expected expr

  let emit_assignment_release state reason (expr : Core.expression)
      (target : Core.expression) expected =
    if resolved_contains_ownership state expr.loc expected then
      emit_action state (BeforeExpr (expr_id expr)) Release reason
        (OwnershipTarget (expr_id target, root_identifier_name target))
        target.loc (Some expected)

  let rec emit_release_to_function state anchor = function
    | [] -> ()
    | scope :: rest ->
        let reason =
          match scope.kind with
          | BlockScope _ -> ScopeExit
          | LoopScope _ -> LoopExit
          | FunctionScope _ -> FunctionExit
        in
        emit_scope_release state anchor reason scope;
        (match scope.kind with
        | FunctionScope _ -> ()
        | BlockScope _ | LoopScope _ -> emit_release_to_function state anchor rest)

  let rec emit_release_to_loop state anchor ~include_loop = function
    | [] -> ()
    | scope :: rest -> (
        match scope.kind with
        | FunctionScope _ -> ()
        | BlockScope _ ->
            emit_scope_release state anchor ScopeExit scope;
            emit_release_to_loop state anchor ~include_loop rest
        | LoopScope _ ->
            if include_loop then emit_scope_release state anchor LoopExit scope)

  let rec visit_expression state scopes (expr : Core.expression) =
    match expr.value with
    | Core.Identifier _ | Core.Nil | Core.SizeType _ | Core.BoxType _ -> ()
    | Core.Literal literal -> (
        match literal.value with
        | Core.Vector vec ->
            List.iter (visit_expression state scopes) vec.value.elements
        | Core.Matrix mat ->
            List.iter
              (fun (row : Core.vec_literal) ->
                List.iter (visit_expression state scopes) row.value.elements)
              mat.value.rows
        | Core.Enum enum_lit ->
            List.iter (visit_expression state scopes) enum_lit.value.wrapped
        | Core.Integer _ | Core.Bool _ | Core.Float _ | Core.String _ | Core.Char _ ->
            ())
    | Core.ToBool inner
    | Core.SizeExpr inner
    | Core.BoxExpr inner
    | Core.Unbox inner
    | Core.Ref inner
    | Core.Load inner ->
        visit_expression state scopes inner
    | Core.Unary unary ->
        visit_expression state scopes unary.value.inner
    | Core.Binary binary ->
        visit_expression state scopes binary.value.left;
        visit_expression state scopes binary.value.right
    | Core.Block block ->
        ignore (visit_block_impl state scopes block ())
    | Core.Initializer init ->
        List.iter (visit_expression state scopes) init.value.exprs
    | Core.As cast ->
        visit_expression state scopes cast.value.inner
    | Core.Match match_expr ->
        visit_expression state scopes match_expr.value.expr;
        List.iter
          (fun (arm : Core.match_arm) -> visit_expression state scopes arm.value.expr)
          match_expr.value.arms
    | Core.Call call ->
        visit_expression state scopes call.value.target;
        List.iter (visit_expression state scopes) call.value.params;
        emit_retains_for_call state CallArg call
    | Core.Index index ->
        visit_expression state scopes index.value.target;
        visit_expression state scopes index.value.index
    | Core.Field field ->
        visit_expression state scopes field.value.target
    | Core.Assign write ->
        visit_expression state scopes write.value.target;
        visit_expression state scopes write.value.value;
        (match expr_resolved_type state write.value.target with
        | Some expected ->
            emit_retains_for_transfer state AssignValue expected write.value.value;
            emit_assignment_release state AssignOverwrite expr write.value.target expected
        | None -> ())
    | Core.Mutate write ->
        visit_expression state scopes write.value.target;
        visit_expression state scopes write.value.value;
        (match expr_resolved_type state write.value.target with
        | Some (ResolvedPointer expected)
        | Some (ResolvedBox expected)
        | Some (ResolvedCell expected) ->
            emit_retains_for_transfer state MutateValue expected write.value.value;
            emit_assignment_release state MutateOverwrite expr write.value.target expected
        | Some _ | None -> ())

  and visit_statement state scopes ~return_expected (stmt : Core.statement) =
    match stmt.value with
    | Core.Expression expr ->
        visit_expression state scopes expr;
        scopes
    | Core.Return expr ->
        Option.iter (visit_expression state scopes) expr;
        (match (return_expected, expr) with
        | Some expected, Some returned ->
            emit_retains_for_transfer state ReturnValue expected returned
        | _ -> ());
        emit_release_to_function state (BeforeStmt (statement_id stmt)) scopes;
        scopes
    | Core.Defer expr ->
        visit_expression state scopes expr;
        scopes
    | Core.Break ->
        emit_release_to_loop state (BeforeStmt (statement_id stmt)) ~include_loop:true
          scopes;
        scopes
    | Core.Continue ->
        emit_release_to_loop state (BeforeStmt (statement_id stmt)) ~include_loop:false
          scopes;
        scopes
    | Core.Let binding ->
        visit_expression state scopes binding.value.init_expr;
        let scopes =
          match binding_resolved_type state binding with
          | Some resolved ->
              emit_retains_for_transfer state BindingInit resolved
                binding.value.init_expr;
              if resolved_contains_ownership state binding.loc resolved then
                add_owned_value scopes
                  {
                    subject = OwnershipBinding binding.value.name.value;
                    loc = binding.loc;
                    resolved_type = resolved;
                  }
              else scopes
          | None -> scopes
        in
        scopes
    | Core.Loop loop ->
        let loop_scopes = push_scope (LoopScope (statement_id stmt)) scopes in
        let loop_scopes =
          List.fold_left
            (fun scopes stmt -> visit_statement state scopes ~return_expected stmt)
            loop_scopes loop.value.init
        in
        visit_expression state loop_scopes loop.value.cond;
        ignore (visit_block_impl state loop_scopes loop.value.body ~return_expected ());
        ignore
          (List.fold_left
             (fun scopes stmt -> visit_statement state scopes ~return_expected stmt)
             loop_scopes loop.value.step);
        (match loop_scopes with
        | scope :: _ -> emit_scope_release state (OnLoopExit (statement_id stmt)) LoopExit scope
        | [] -> ());
        scopes

  and visit_block_impl state scopes (block : Core.block)
      ?(return_expected : resolved_ty option = None) () =
    let scopes = push_scope (BlockScope (block_id block)) scopes in
    let scopes =
      List.fold_left
        (fun scopes stmt -> visit_statement state scopes ~return_expected stmt)
        scopes block.value.statements
    in
    Option.iter (visit_expression state scopes) block.value.result;
    (match scopes with
    | scope :: _ -> emit_scope_release state (OnBlockExit (block_id block)) ScopeExit scope
    | [] -> ());
    List.tl scopes

  let walk_block state scopes ?(return_expected : resolved_ty option = None) block =
    visit_block_impl state scopes ~return_expected block ()

  let owned_param_of_decl state (param : Core.param) =
    match resolved_type_of_param state param with
    | Some resolved when resolved_contains_ownership state param.loc resolved ->
        Some
          {
            subject = OwnershipParam param.value.name.value;
            loc = param.loc;
            resolved_type = resolved;
          }
    | Some _ | None -> None

  let visit_function state (fn : Core.function_decl) =
    match fn.value.definition with
    | None -> ()
    | Some body ->
        let return_expected =
          let core_ty = Option.value ~default:(void_type fn.loc) fn.value.return_type in
          resolve_core_type state.type_env [] [] fn.loc core_ty
        in
        let params =
          List.filter_map (owned_param_of_decl state) fn.value.params.value.params
        in
        let scopes = [ { kind = FunctionScope (function_id fn); owned = List.rev params } ] in
        ignore (walk_block state scopes body ~return_expected);
        (match (return_expected, body.value.result) with
        | Some expected, Some result ->
            emit_retains_for_transfer state ReturnValue expected result
        | _ -> ());
        emit_scope_release state (OnFunctionExit (function_id fn)) FunctionExit
          { kind = FunctionScope (function_id fn); owned = List.rev params }

  let visit_top_decl state (decl : Core.top_decl) =
    match decl.value with
    | Core.FDecl fn -> visit_function state fn
    | Core.Foreign foreign -> List.iter (visit_function state) foreign.value.decls
    | Core.VDecl binding ->
        Option.iter
          (fun init ->
            visit_expression state [] init;
            match resolve_core_type state.type_env [] [] binding.loc binding.value.ty with
            | Some resolved ->
                emit_retains_for_transfer state BindingInit resolved init
            | None -> ())
          binding.value.init_expr
    | Core.TDecl _ | Core.Import _ | Core.CImport _ -> ()

  let run typed =
    let state =
      {
        typed;
        type_env = type_env_of_program typed.program.program;
        actions_rev = [];
        diagnostics_rev = [];
      }
    in
    List.iter (visit_top_decl state) typed.program.program.value.decls;
    {
      actions = List.rev state.actions_rev;
      diagnostics = List.rev state.diagnostics_rev;
    }
end
