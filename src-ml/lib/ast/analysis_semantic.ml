open Analysis_types

module Typing = Analysis_typing.Typing

module Semantic = struct
  type env = binding_annotation String_map.t list

  type state = {
    typed : typing_result;
    mutable diagnostics_rev : diagnostic list;
    type_env : type_env;
  }

  let add_diagnostic_with_category state category level loc message =
    state.diagnostics_rev <- { category; level; loc; message } :: state.diagnostics_rev

  let add_diagnostic state = add_diagnostic_with_category state Semantic

  let push_scope env = String_map.empty :: env

  let bind_current env name binding =
    match env with
    | [] -> [ String_map.singleton name binding ]
    | scope :: rest -> String_map.add name binding scope :: rest

  let rec lookup env name =
    match env with
    | [] -> None
    | scope :: rest -> (
        match String_map.find_opt name scope with
        | Some binding -> Some binding
        | None -> lookup rest name)

  let expr_annotation state expr =
    Hashtbl.find_opt state.typed.annotations.exprs (expr_id expr)

  let initial_scope typed =
    let add_decl scope (decl : Core.top_decl) =
      match decl.value with
      | Core.FDecl fn ->
          String_map.add fn.value.name.value
            {
              inferred_type = Some (Typing.function_type_of_decl fn);
              resolved_type = None;
              metavar = metavar_of_type (Typing.function_type_of_decl fn);
              is_mutable = false;
            }
            scope
      | Core.VDecl binding ->
          String_map.add binding.value.name.value
            {
              inferred_type = Some binding.value.ty;
              resolved_type = None;
              metavar = metavar_of_type binding.value.ty;
              is_mutable = binding.value.is_mutable;
            }
            scope
      | Core.Foreign foreign ->
          List.fold_left
            (fun scope (fn : Core.function_decl) ->
              String_map.add fn.value.name.value
                {
                  inferred_type = Some (Typing.function_type_of_decl fn);
                  resolved_type = None;
                  metavar = metavar_of_type (Typing.function_type_of_decl fn);
                  is_mutable = false;
                }
                scope)
            scope foreign.value.decls
      | Core.TDecl _ | Core.Import _ | Core.CImport _ -> scope
    in
    List.fold_left add_decl String_map.empty typed.program.program.value.decls

  let duplicate_binding env name =
    match env with
    | [] -> false
    | scope :: _ -> String_map.mem name scope

  let bool_match_is_exhaustive (match_expr : Core.match_expr) =
    let has_true =
      List.exists
        (fun (arm : Core.match_arm) ->
          match arm.value.pattern.value with
          | Core.PatternLiteral lit -> lit.value = Core.Bool true
          | _ -> false)
        match_expr.value.arms
    in
    let has_false =
      List.exists
        (fun (arm : Core.match_arm) ->
          match arm.value.pattern.value with
          | Core.PatternLiteral lit -> lit.value = Core.Bool false
          | _ -> false)
        match_expr.value.arms
    in
    has_true && has_false

  let return_value_matches state expected (expr : Core.expression) =
    if expr.value = Core.Nil then resolved_is_pointerish expected
    else
      match expr_annotation state expr with
      | Some { resolved_type = Some actual; _ } -> resolved_compatible actual expected
      | _ -> false

  let expr_resolved_type state (expr : Core.expression) =
    match expr_annotation state expr with
    | Some { resolved_type = Some ty; _ } -> Some ty
    | _ -> None

  let check_expr_matches_expected state (expr : Core.expression) expected mismatch_message
      nil_message =
    if expr.value = Core.Nil then (
      if not (resolved_is_pointerish expected) then add_diagnostic state Error expr.loc nil_message)
    else
      match expr_resolved_type state expr with
      | Some actual when not (resolved_compatible actual expected) ->
          add_diagnostic state Error expr.loc mismatch_message
      | _ -> ()

  let check_initializer_shape state loc (init : Core.init_list) expected =
    let check_slots slots too_many_message too_few_message mismatch_for_index =
      let actual_count = List.length init.value.exprs in
      let expected_count = List.length slots in
      if actual_count > expected_count then add_diagnostic state Error loc too_many_message;
      if actual_count < expected_count then add_diagnostic state Error loc too_few_message;
      List.iteri
        (fun index expected_slot ->
          if index < actual_count then
            let expr = List.nth init.value.exprs index in
            check_expr_matches_expected state expr expected_slot
              (mismatch_for_index index) "nil is only valid for pointer-like initializer elements")
        slots
    in
    match expected with
    | ResolvedArray (element_ty, count) ->
        check_slots (List.init count (fun _ -> element_ty))
          "array initializer has more elements than the target type"
          "array initializer has fewer elements than the target type"
          (fun _ -> "array initializer element type does not match the target type")
    | ResolvedNamed _ as struct_ty -> (
        match lookup_struct_fields state.type_env loc struct_ty with
        | Some fields ->
            let field_names, field_types = List.split fields in
            check_slots field_types
              "struct initializer has more elements than the target type"
              "struct initializer has fewer elements than the target type"
              (fun index ->
                let field_name =
                  if index < List.length field_names then List.nth field_names index
                  else "<unknown>"
                in
                Printf.sprintf
                  "struct initializer field %s does not match the declared field type"
                  field_name)
        | None -> ())
    | ResolvedVec vec ->
        check_slots (List.init vec.dimension (fun _ -> ResolvedFloat))
          "vector initializer has more elements than the target type"
          "vector initializer has fewer elements than the target type"
          (fun _ -> "vector initializer element type does not match the target type")
    | ResolvedMatrix mat ->
        check_slots
          (List.init mat.rows (fun _ -> ResolvedVec { kind = FloatVec; dimension = mat.columns }))
          "matrix initializer has more rows than the target type"
          "matrix initializer has fewer rows than the target type"
          (fun _ -> "matrix initializer row type does not match the target type")
    | _ -> ()

  let statement_guarantees_return (stmt : Core.statement) =
    match stmt.value with Core.Return _ -> true | _ -> false

  let block_guarantees_return (block : Core.block) =
    Option.is_some block.value.result || List.exists statement_guarantees_return block.value.statements

  let rec check_block state env loop_depth ~return_expected (block : Core.block) =
    let env = push_scope env in
    let env =
      List.fold_left
        (fun env (stmt : Core.statement) ->
          check_statement state env loop_depth ~return_expected stmt)
        env
        block.value.statements
    in
    Option.iter
      (fun (expr : Core.expression) ->
        check_expression state env loop_depth expr;
        match return_expected with
        | Some ResolvedVoid ->
            add_diagnostic state Error expr.loc "void function cannot return a value"
        | Some expected when not (return_value_matches state expected expr) ->
            add_diagnostic state Error expr.loc
              "returned value does not match the function return type"
        | _ -> ())
      block.value.result;
    env

  and check_statement state env loop_depth ~return_expected (stmt : Core.statement) =
    match stmt.value with
    | Core.Expression expr ->
        check_expression_in_context state env loop_depth true expr;
        env
    | Core.Return expr ->
        Option.iter (check_expression state env loop_depth) expr;
        (match (return_expected, expr) with
        | Some ResolvedVoid, Some returned ->
            add_diagnostic state Error returned.loc
              "void function cannot return a value"
        | Some ResolvedVoid, None -> ()
        | Some _, None ->
            add_diagnostic state Error stmt.loc
              "non-void function must return a value"
        | Some expected, Some returned ->
            if not (return_value_matches state expected returned) then
              add_diagnostic state Error returned.loc
                "returned value does not match the function return type"
        | None, _ -> ());
        env
    | Core.Defer expr ->
        check_expression state env loop_depth expr;
        env
    | Core.Break ->
        if loop_depth = 0 then
          add_diagnostic state Error stmt.loc "break used outside of a loop";
        env
    | Core.Continue ->
        if loop_depth = 0 then
          add_diagnostic state Error stmt.loc "continue used outside of a loop";
        env
    | Core.Let binding ->
        check_expression state env loop_depth binding.value.init_expr;
        (match (binding.value.ty, binding.value.init_expr.value) with
        | Some ty, Core.Nil -> (
            match resolve_core_type state.type_env [] [] binding.loc ty with
            | Some resolved when resolved_is_pointerish resolved -> ()
            | _ ->
                add_diagnostic state Error binding.value.init_expr.loc
                  "nil is only valid for pointer-like let bindings")
        | _ -> ());
        (match binding.value.ty with
        | Some ty -> (
            match
              ( resolve_core_type state.type_env [] [] binding.loc ty,
                expr_annotation state binding.value.init_expr )
            with
            | Some expected, Some { resolved_type = Some actual; _ }
              when not (resolved_compatible actual expected) ->
                add_diagnostic state Error binding.value.init_expr.loc
                  "let initializer type does not match the declared binding type"
            | Some expected, _ -> (
                match binding.value.init_expr.value with
                | Core.Initializer init ->
                    check_initializer_shape state binding.value.init_expr.loc init expected
                | _ -> ())
            | _ -> ())
        | None -> ());
        if duplicate_binding env binding.value.name.value then
          add_diagnostic state Error binding.loc
            (Printf.sprintf "duplicate binding %s in the same scope"
               binding.value.name.value);
        let binding_ann =
          Hashtbl.find_opt state.typed.annotations.bindings (binding_id binding)
          |> Option.value
               ~default:
                 {
                   inferred_type = binding.value.ty;
                   resolved_type = None;
                   metavar = unknown_metavar;
                   is_mutable = binding.value.mut;
                 }
        in
        bind_current env binding.value.name.value binding_ann
    | Core.Loop loop ->
        let env = push_scope env in
        let env =
          List.fold_left
            (fun env (stmt : Core.statement) ->
              check_statement state env (loop_depth + 1) ~return_expected stmt)
            env loop.value.init
        in
        check_expression state env (loop_depth + 1) loop.value.cond;
        ignore (check_block state env (loop_depth + 1) ~return_expected loop.value.body);
        ignore
          (List.fold_left
             (fun env (stmt : Core.statement) ->
               check_statement state env (loop_depth + 1) ~return_expected stmt)
             env loop.value.step);
        env

  and check_expression state env loop_depth expr =
    check_expression_in_context state env loop_depth false expr

  and check_expression_in_context state env loop_depth statement_context
      (expr : Core.expression) =
    let check_scalar_truthy inner =
      match expr_annotation state inner with
      | Some { inferred_type = Some ty; resolved_type; _ } ->
          let allowed =
            match resolved_type with
            | Some resolved ->
                resolved_is_bool resolved || resolved_is_numeric resolved
                || resolved_is_pointerish resolved
            | None -> is_scalar_truthy_type ty || is_boolean_type ty
          in
          if not allowed then
            add_diagnostic state Error inner.loc
              "expression cannot be converted to bool in this context"
      | _ -> ()
    in
    match expr.value with
    | Core.Identifier _ | Core.Literal _ | Core.Nil | Core.SizeType _ -> ()
    | Core.ToBool inner ->
        check_expression state env loop_depth inner;
        check_scalar_truthy inner
    | Core.Unary unary -> check_expression state env loop_depth unary.value.inner
    | Core.Binary binary ->
        check_expression state env loop_depth binary.value.left;
        check_expression state env loop_depth binary.value.right;
        let left_resolved = expr_resolved_type state binary.value.left in
        let right_resolved = expr_resolved_type state binary.value.right in
        let numeric_pair =
          match (left_resolved, right_resolved) with
          | Some left, Some right -> resolved_is_numeric left && resolved_is_numeric right
          | _ -> false
        in
        let pointer_numeric_pair =
          match (left_resolved, right_resolved) with
          | Some left, Some right ->
              (resolved_is_pointerish left && resolved_is_numeric right)
              || (resolved_is_numeric left && resolved_is_pointerish right)
          | _ -> false
        in
        let vector_or_matrix_result =
          match (left_resolved, right_resolved) with
          | Some left, Some right ->
              resolved_arithmetic_binary_result binary.value.op left right
          | _ -> None
        in
        let compatible_pair =
          match (left_resolved, right_resolved) with
          | Some left, Some right -> resolved_compatible left right
          | _ -> false
        in
        (match binary.value.op with
        | Core.Add | Core.Subtract ->
            if not (numeric_pair || pointer_numeric_pair || Option.is_some vector_or_matrix_result)
            then
              add_diagnostic state Error expr.loc
                "binary arithmetic requires numeric operands or pointer arithmetic"
        | Core.Multiply | Core.Divide | Core.Modulo ->
            if not (numeric_pair || Option.is_some vector_or_matrix_result) then
              add_diagnostic state Error expr.loc
                "binary arithmetic requires numeric operands"
        | Core.LeftShift | Core.RightShift | Core.BitwiseAnd | Core.BitwiseOr | Core.BitwiseXor
          ->
            if not numeric_pair then
              add_diagnostic state Error expr.loc
                "bitwise operations require numeric operands"
        | Core.IsEqual | Core.NotEqual ->
            if not compatible_pair then
              add_diagnostic state Error expr.loc
                "comparison operands must have compatible types"
        | Core.LessThan | Core.LessThanOrEqual | Core.GreaterThan | Core.GreaterThanOrEqual ->
            if not numeric_pair then
              add_diagnostic state Error expr.loc
                "ordered comparisons require numeric operands"
        | Core.LogicAnd | Core.LogicOr ->
            check_scalar_truthy binary.value.left;
            check_scalar_truthy binary.value.right)
    | Core.Block block ->
        ignore (check_block state env loop_depth ~return_expected:None block)
    | Core.Initializer init ->
        List.iter (check_expression state env loop_depth) init.value.exprs
    | Core.As cast ->
        check_expression state env loop_depth cast.value.inner;
        (match
           ( expr_resolved_type state cast.value.inner,
             resolve_core_type state.type_env [] [] cast.loc cast.value.target_type )
         with
        | Some source, Some target when not (resolved_can_cast source target) ->
            add_diagnostic state Error cast.loc "incompatible cast"
        | _ -> ())
    | Core.SizeExpr inner -> check_expression state env loop_depth inner
    | Core.Match match_expr ->
        check_expression state env loop_depth match_expr.value.expr;
        let is_bool_scrutinee =
          match expr_annotation state match_expr.value.expr with
          | Some { inferred_type = Some ty; _ } -> is_boolean_type ty
          | _ -> false
        in
        let has_default =
          List.exists
            (fun (arm : Core.match_arm) -> arm.value.pattern.value = Core.PatternDefault)
            match_expr.value.arms
        in
        if
          (not statement_context) && (not has_default)
          && (not (is_bool_scrutinee && bool_match_is_exhaustive match_expr))
        then
          add_diagnostic state Error expr.loc "match expression is not exhaustive";
        List.iter
          (fun (arm : Core.match_arm) ->
            let env = push_scope env in
            let env =
              match arm.value.pattern.value with
              | Core.PatternEnum enum ->
                  List.fold_left
                    (fun env (binding : Core.pattern_binding) ->
                      match binding.value with
                      | Core.BindingIgnored -> env
                      | Core.BindingNamed id ->
                          bind_current env id.value
                            {
                              inferred_type = None;
                              resolved_type = None;
                              metavar = unknown_metavar;
                              is_mutable = false;
                            })
                    env enum.value.binding
              | Core.PatternDefault | Core.PatternLiteral _ -> env
            in
            check_expression state env loop_depth arm.value.expr)
          match_expr.value.arms;
        let arm_exprs = List.map (fun (arm : Core.match_arm) -> arm.value.expr) match_expr.value.arms in
        let expected_arm_type = expr_resolved_type state expr in
        (match expected_arm_type with
        | Some expected ->
            List.iter
              (fun arm_expr ->
                check_expr_matches_expected state arm_expr expected
                  "match arm type does not match the rest of the match expression"
                  "nil is only valid for pointer-like match arm types")
              arm_exprs
        | None -> ());
        (match expr_annotation state match_expr.value.expr with
        | Some { resolved_type = Some scrutinee_ty; _ } ->
            List.iter
              (fun (arm : Core.match_arm) ->
                match arm.value.pattern.value with
                | Core.PatternEnum enum -> (
                    match
                      lookup_enum_variant state.type_env arm.loc scrutinee_ty
                        enum.value.enum_variant.value
                    with
                    | None ->
                        add_diagnostic state Error arm.loc
                          "enum pattern variant does not exist on the scrutinee type"
                    | Some (_, inner_ty) ->
                        Option.iter
                          (fun (enum_name : Core.identifier) ->
                            match scrutinee_ty with
                            | ResolvedNamed (name, _)
                              when not (String.equal name enum_name.value) ->
                                add_diagnostic state Error arm.loc
                                  "pattern enum name does not match the scrutinee type"
                            | _ -> ())
                          enum.value.enum_name;
                        (match (inner_ty, enum.value.binding) with
                        | Some _, [] ->
                            add_diagnostic state Error arm.loc
                              "enum pattern requires a binding or explicit (_) payload"
                        | None, _ :: _ ->
                            add_diagnostic state Error arm.loc
                              "enum pattern does not take payload bindings"
                        | Some _, _ :: _ :: _ ->
                            add_diagnostic state Error arm.loc
                              "enum pattern currently supports only one payload binding"
                        | _ -> ()))
                | Core.PatternDefault | Core.PatternLiteral _ -> ())
              match_expr.value.arms
        | _ -> ())
    | Core.BoxExpr inner ->
        check_expression state env loop_depth inner
    | Core.Unbox inner ->
        check_expression state env loop_depth inner;
        (match expr_resolved_type state inner with
        | Some (ResolvedBox _) -> ()
        | Some _ ->
            add_diagnostic state Error expr.loc
              "unbox can only be used with boxed types"
        | None -> ())
    | Core.Ref inner ->
        check_expression state env loop_depth inner;
        if not (is_lvalue inner) then
          add_diagnostic state Error expr.loc
            "ref expression must resolve to an assignable target"
    | Core.Load inner ->
        check_expression state env loop_depth inner;
        (match expr_resolved_type state inner with
        | Some (ResolvedPointer _ | ResolvedBox _ | ResolvedCell _) -> ()
        | Some _ ->
            add_diagnostic state Error expr.loc
              "load expression must resolve to a pointer-like reference"
        | None -> ())
    | Core.BoxType _ -> ()
    | Core.Call call ->
        check_expression state env loop_depth call.value.target;
        List.iter (check_expression state env loop_depth) call.value.params;
        (match expr_annotation state call.value.target with
        | Some { inferred_type = Some ty; _ } -> (
            match ty.value with
            | Core.FunctionType fn ->
                let actual_arity = List.length call.value.params in
                let required_arity = List.length fn.value.param_types in
                if
                  actual_arity < required_arity
                  || ((not fn.value.vararg) && actual_arity > required_arity)
                then
                  add_diagnostic state Error call.loc
                    "call argument count does not match the function signature";
                List.iter2
                  (fun (arg : Core.expression) expected_ty ->
                    if arg.value = Core.Nil then
                      match resolve_core_type state.type_env [] [] arg.loc expected_ty with
                      | Some resolved when resolved_is_pointerish resolved -> ()
                      | _ ->
                          add_diagnostic state Error arg.loc
                            "nil is only valid for pointer-like parameter types")
                  (List.filteri
                     (fun index _ -> index < List.length fn.value.param_types)
                     call.value.params)
                  (List.filteri
                     (fun index _ -> index < List.length call.value.params)
                     fn.value.param_types);
                List.iter2
                  (fun (arg : Core.expression) expected_ty ->
                    match
                      ( expr_annotation state arg,
                        resolve_core_type state.type_env [] [] arg.loc expected_ty )
                    with
                    | Some { resolved_type = Some actual; _ }, Some expected
                      when not (resolved_compatible actual expected)
                           && arg.value <> Core.Nil ->
                        add_diagnostic state Error arg.loc
                          "call argument type does not match the function signature"
                    | _ -> ())
                  (List.filteri
                     (fun index _ -> index < List.length fn.value.param_types)
                     call.value.params)
                  (List.filteri
                     (fun index _ -> index < List.length call.value.params)
                     fn.value.param_types)
            | _ -> ())
        | Some { resolved_type = Some enum_ty; _ } -> (
            match call.value.target.value with
            | Core.Literal literal -> (
                match literal.value with
                | Core.Enum enum_lit -> (
                    match
                      lookup_enum_variant state.type_env call.loc enum_ty
                        enum_lit.value.enum_variant.value
                    with
                    | Some (_, Some expected) -> (
                        match call.value.params with
                        | [ arg ] -> (
                            match expr_annotation state arg with
                            | Some { resolved_type = Some actual; _ }
                              when not (resolved_compatible actual expected)
                                   && arg.value <> Core.Nil ->
                                add_diagnostic state Error arg.loc
                                  "enum payload type does not match the variant"
                            | Some _ when arg.value = Core.Nil ->
                                if not (resolved_is_pointerish expected) then
                                  add_diagnostic state Error arg.loc
                                    "nil is only valid for pointer-like enum payloads"
                            | _ -> ())
                        | _ -> ())
                    | Some (_, None) | None -> ())
                | _ -> ())
            | _ -> ())
        | _ -> ())
    | Core.Index index ->
        check_expression state env loop_depth index.value.target;
        check_expression state env loop_depth index.value.index
    | Core.Field field ->
        check_expression state env loop_depth field.value.target;
        if field.value.arrow then (
          match expr_annotation state field.value.target with
          | Some { inferred_type = Some ty; _ } -> (
              match ty.value with
              | Core.PointerType _ | Core.BoxType _ | Core.CellType _ -> ()
              | _ ->
                  add_diagnostic state Error field.loc
                    "arrow field access requires a pointer-like target")
          | _ -> ())
    | Core.Assign write ->
        check_expression state env loop_depth write.value.target;
        check_expression state env loop_depth write.value.value;
        if not (is_lvalue write.value.target) then
          add_diagnostic state Error expr.loc
            "assignment target must be assignable";
        Option.iter
          (fun name ->
            match lookup env name with
            | Some binding when not binding.is_mutable ->
                add_diagnostic state Error expr.loc
                  (Printf.sprintf "assignment to immutable binding %s" name)
            | _ -> ())
          (root_identifier_name write.value.target);
        (match
           ( expr_annotation state write.value.target,
             expr_annotation state write.value.value )
         with
        | Some { resolved_type = Some expected; _ }, Some { resolved_type = Some actual; _ }
          when not (resolved_compatible actual expected) ->
            add_diagnostic state Error write.value.value.loc
              "assignment value type does not match the target"
        | Some { resolved_type = Some expected; _ }, _ -> (
            match write.value.value.value with
            | Core.Initializer init ->
                check_initializer_shape state write.value.value.loc init expected
            | _ -> ())
        | _ -> ());
        if write.value.value.value = Core.Nil then (
          match expr_annotation state write.value.target with
          | Some { resolved_type = Some resolved; _ } when resolved_is_pointerish resolved -> ()
          | _ ->
              add_diagnostic state Error write.value.value.loc
                "nil is only valid for pointer-like assignment targets")
    | Core.Mutate write ->
        check_expression state env loop_depth write.value.target;
        check_expression state env loop_depth write.value.value;
        if not statement_context then
          add_diagnostic state Error expr.loc
            "mutation is statement-only and cannot be used as a value";
        if not (is_lvalue write.value.target) then
          add_diagnostic state Error expr.loc
            "mutation target must be assignable";
        (match expr_annotation state write.value.target with
        | Some { resolved_type = Some resolved; _ } ->
            if not (resolved_is_pointerish resolved) then
              add_diagnostic state Error expr.loc
                "mutation requires a pointer-like target"
        | Some { inferred_type = Some ty; _ } -> (
            match ty.value with
            | Core.PointerType _ | Core.BoxType _ | Core.CellType _ -> ()
            | _ ->
                add_diagnostic state Error expr.loc
                  "mutation requires a pointer-like target")
        | _ -> ());
        (match
           ( expr_annotation state write.value.target,
             expr_annotation state write.value.value )
         with
        | Some { resolved_type = Some (ResolvedPointer expected); _ },
          Some { resolved_type = Some actual; _ }
        | Some { resolved_type = Some (ResolvedBox expected); _ },
          Some { resolved_type = Some actual; _ }
        | Some { resolved_type = Some (ResolvedCell expected); _ },
          Some { resolved_type = Some actual; _ }
          when not (resolved_compatible actual expected) ->
            add_diagnostic state Error write.value.value.loc
              "mutation value type does not match the pointed-to type"
        | Some { resolved_type = Some (ResolvedPointer expected); _ }, _
        | Some { resolved_type = Some (ResolvedBox expected); _ }, _
        | Some { resolved_type = Some (ResolvedCell expected); _ }, _ -> (
            match write.value.value.value with
            | Core.Initializer init ->
                check_initializer_shape state write.value.value.loc init expected
            | _ -> ())
        | _ -> ());
        if write.value.value.value = Core.Nil then (
          match expr_annotation state write.value.target with
          | Some { resolved_type = Some (ResolvedPointer inner); _ }
          | Some { resolved_type = Some (ResolvedBox inner); _ }
          | Some { resolved_type = Some (ResolvedCell inner); _ }
            when resolved_is_pointerish inner -> ()
          | _ ->
              add_diagnostic state Error write.value.value.loc
                "nil is only valid for pointer-like mutation targets")

  let run typed =
    let state =
      { typed; diagnostics_rev = []; type_env = type_env_of_program typed.program.program }
    in
    let env = [ initial_scope typed ] in
    List.iter
      (fun (decl : Core.top_decl) ->
        match decl.value with
        | Core.FDecl fn -> (
            match fn.value.definition with
            | None -> ()
            | Some body ->
                let return_expected =
                  let core_ty =
                    Option.value ~default:(void_type fn.loc) fn.value.return_type
                  in
                  resolve_core_type state.type_env [] [] fn.loc core_ty
                in
                let env = push_scope env in
                let env =
                  List.fold_left
                    (fun env (param : Core.param) ->
                      bind_current env param.value.name.value
                        {
                          inferred_type = Some param.value.ty;
                          resolved_type = None;
                          metavar = metavar_of_type param.value.ty;
                              is_mutable = false;
                            })
                    env fn.value.params.value.params
                in
                ignore (check_block state env 0 ~return_expected body);
                (match return_expected with
                | Some ResolvedVoid -> ()
                | Some _ when block_guarantees_return body -> ()
                | Some _ ->
                    add_diagnostic state Error body.loc
                      "non-void function must return a value"
                | None -> ()))
        | Core.Foreign foreign ->
            List.iter
              (fun (fn : Core.function_decl) ->
                match fn.value.definition with
                | None -> ()
                | Some body ->
                    let return_expected =
                      let core_ty =
                        Option.value ~default:(void_type fn.loc) fn.value.return_type
                      in
                      resolve_core_type state.type_env [] [] fn.loc core_ty
                    in
                    let env = push_scope env in
                    let env =
                      List.fold_left
                        (fun env (param : Core.param) ->
                          bind_current env param.value.name.value
                            {
                              inferred_type = Some param.value.ty;
                              resolved_type = None;
                              metavar = metavar_of_type param.value.ty;
                              is_mutable = false;
                            })
                        env fn.value.params.value.params
                    in
                    ignore (check_block state env 0 ~return_expected body);
                    (match return_expected with
                    | Some ResolvedVoid -> ()
                    | Some _ when block_guarantees_return body -> ()
                    | Some _ ->
                        add_diagnostic state Error body.loc
                          "non-void function must return a value"
                    | None -> ()))
              foreign.value.decls
        | Core.VDecl binding ->
            Option.iter
              (fun init ->
                check_expression state env 0 init;
                match
                  ( resolve_core_type state.type_env [] [] binding.loc binding.value.ty,
                    init.value )
                with
                | Some expected, Core.Initializer init ->
                    check_initializer_shape state init.loc init expected
                | _ -> ())
              binding.value.init_expr
        | Core.TDecl _ | Core.Import _ | Core.CImport _ -> ())
      typed.program.program.value.decls;
    { diagnostics = List.rev state.diagnostics_rev }
end
