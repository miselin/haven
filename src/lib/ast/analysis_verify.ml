open Analysis_types

module Verify = struct
  type state = {
    typed : typing_result;
    type_env : type_env;
    mutable diagnostics_rev : diagnostic list;
  }

  let add_diagnostic state level loc message =
    state.diagnostics_rev <-
      { category = TypeVerify; level; loc; message } :: state.diagnostics_rev

  let expr_annotation state expr =
    Hashtbl.find_opt state.typed.annotations.exprs (expr_id expr)

  let binding_annotation state binding =
    Hashtbl.find_opt state.typed.annotations.bindings (binding_id binding)

  let verify_resolved_type state loc context = function
    | Some _ -> ()
    | None ->
        add_diagnostic state Error loc
          (Printf.sprintf "%s still has an unresolved type after typing" context)

  let verify_declared_type state loc context ty =
    verify_resolved_type state loc context
      (resolve_core_type state.type_env [] [] loc ty)

  let verify_declared_type_with_subst state loc context subst ty =
    verify_resolved_type state loc context
      (resolve_core_type state.type_env [] subst loc ty)

  let verify_expr_annotation state context (expr : Core.expression) =
    match expr.value with
    | Core.Nil -> ()
    | _ -> (
        match expr_annotation state expr with
        | Some { resolved_type = Some _; _ } -> ()
        | Some _ | None ->
            add_diagnostic state Error expr.loc
              (Printf.sprintf "%s still has an unresolved expression type after typing"
                 context))

  let verify_binding_annotation state (binding : Core.let_stmt) =
    match binding_annotation state binding with
    | Some { resolved_type = Some _; _ } -> ()
    | Some _ | None ->
        add_diagnostic state Error binding.loc
          (Printf.sprintf "binding %s still has an unresolved type after typing"
             binding.value.name.value)

  let rec verify_block state (block : Core.block) =
    List.iter (verify_statement state) block.value.statements;
    Option.iter (verify_expression state) block.value.result

  and verify_statement state (stmt : Core.statement) =
    match stmt.value with
    | Core.Expression expr -> verify_expression state expr
    | Core.Return expr -> Option.iter (verify_expression state) expr
    | Core.Defer expr -> verify_expression state expr
    | Core.Let binding ->
        verify_binding_annotation state binding;
        Option.iter
          (verify_declared_type state binding.loc
             (Printf.sprintf "binding %s declaration" binding.value.name.value))
          binding.value.ty;
        verify_expression state binding.value.init_expr
    | Core.Loop loop ->
        List.iter (verify_statement state) loop.value.init;
        verify_expression state loop.value.cond;
        verify_block state loop.value.body;
        List.iter (verify_statement state) loop.value.step
    | Core.Break | Core.Continue -> ()

  and verify_expression state (expr : Core.expression) =
    let verify_subexprs () =
      match expr.value with
      | Core.Identifier _ | Core.Literal _ | Core.Nil -> ()
      | Core.ToBool inner
      | Core.SizeExpr inner
      | Core.BoxExpr inner
      | Core.Unbox inner
      | Core.Ref inner
      | Core.Load inner ->
          verify_expression state inner
      | Core.Unary unary ->
          verify_expression state unary.value.inner
      | Core.Binary binary ->
          verify_expression state binary.value.left;
          verify_expression state binary.value.right
      | Core.Block block ->
          verify_block state block
      | Core.Initializer init ->
          List.iter (verify_expression state) init.value.exprs
      | Core.As cast ->
          verify_declared_type state cast.loc "cast target" cast.value.target_type;
          verify_expression state cast.value.inner
      | Core.SizeType ty | Core.BoxType ty ->
          verify_declared_type state expr.loc "embedded type expression" ty
      | Core.Match match_expr ->
          verify_expression state match_expr.value.expr;
          List.iter
            (fun (arm : Core.match_arm) -> verify_expression state arm.value.expr)
            match_expr.value.arms
      | Core.Call call ->
          (match call.value.target.value with
          | Core.Identifier _ -> ()
          | Core.Literal literal -> (
              match literal.value with
              | Core.Enum _ -> ()
              | _ -> verify_expression state call.value.target)
          | _ -> verify_expression state call.value.target);
          List.iter (verify_expression state) call.value.params
      | Core.Index index ->
          verify_expression state index.value.target;
          verify_expression state index.value.index
      | Core.Field field ->
          verify_expression state field.value.target
      | Core.Assign write | Core.Mutate write ->
          verify_expression state write.value.target;
          verify_expression state write.value.value
    in
    verify_subexprs ();
    verify_expr_annotation state "expression" expr

  let verify_function state (fn : Core.function_decl) =
    Option.iter
      (fun declared_return ->
        verify_declared_type state fn.loc
          (Printf.sprintf "function %s return type" fn.value.name.value)
          declared_return)
      fn.value.return_type;
    List.iter
      (fun (param : Core.param) ->
        verify_declared_type state param.loc
          (Printf.sprintf "function %s parameter %s" fn.value.name.value
             param.value.name.value)
          param.value.ty)
      fn.value.params.value.params;
    Option.iter (verify_block state) fn.value.definition

  let verify_type_decl state (decl : Core.type_decl) =
    match decl.value.data with
    | Core.TypeDeclAlias ty ->
        verify_declared_type state decl.loc
          (Printf.sprintf "type alias %s" decl.value.name.value)
          ty
    | Core.TypeDeclStruct struct_decl ->
        List.iter
          (fun (field : Core.struct_field) ->
            verify_declared_type state field.loc
              (Printf.sprintf "struct field %s.%s" decl.value.name.value
                 field.value.name.value)
              field.value.ty)
          struct_decl.value.fields
    | Core.TypeDeclEnum enum_decl ->
        let subst =
          List.map
            (fun (generic : Core.identifier) ->
              (generic.value, ResolvedGenericParam generic.value))
            enum_decl.value.generics
        in
        List.iter
          (fun (variant : Core.enum_variant) ->
            List.iter
              (verify_declared_type_with_subst state variant.loc
                 (Printf.sprintf "enum variant %s::%s" decl.value.name.value
                    variant.value.name.value)
                 subst)
              variant.value.inner_tys)
          enum_decl.value.variants
    | Core.TypeDeclForward -> ()

  let verify_top_decl state (decl : Core.top_decl) =
    match decl.value with
    | Core.FDecl fn -> verify_function state fn
    | Core.Foreign foreign -> List.iter (verify_function state) foreign.value.decls
    | Core.VDecl binding ->
        verify_declared_type state binding.loc
          (Printf.sprintf "global %s" binding.value.name.value)
          binding.value.ty;
        Option.iter (verify_expression state) binding.value.init_expr
    | Core.TDecl decl -> verify_type_decl state decl
    | Core.Import _ | Core.CImport _ -> ()

  let run typed : verify_result =
    let state =
      { typed; type_env = type_env_of_program typed.program.program; diagnostics_rev = [] }
    in
    List.iter (verify_top_decl state) typed.program.program.value.decls;
    { diagnostics = List.rev state.diagnostics_rev }
end
