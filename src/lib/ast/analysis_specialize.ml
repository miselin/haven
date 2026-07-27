open Analysis_types

module Typing = Analysis_typing.Typing

module Specialize = struct
  type instance = {
    key : string;
    name : string;
    template : Core.function_decl;
    param_types : resolved_ty list;
    return_type : resolved_ty;
  }

  type result = {
    program : Core.parsed_program;
    diagnostics : diagnostic list;
  }

  type state = {
    typed : typing_result;
    functions : Core.function_decl String_map.t;
    instances : (string, instance) Hashtbl.t;
    mutable queue_rev : instance list;
    mutable diagnostics_rev : diagnostic list;
  }

  let add_diagnostic state level loc message =
    state.diagnostics_rev <-
      { category = TypeCheck; level; loc; message } :: state.diagnostics_rev

  let expr_annotation annotations (expr : Core.expression) =
    Hashtbl.find_opt annotations.exprs (expr_id expr)

  let resolved_expr_type annotations expr =
    Option.bind (expr_annotation annotations expr) (fun ann -> ann.resolved_type)

  let exact_integer annotations expr =
    Option.bind (expr_annotation annotations expr) (fun ann ->
        Option.bind ann.metavar.integer (fun integer -> integer.exact_value))

  let is_shape_property name =
    String.equal name "dim" || String.equal name "rows" || String.equal name "cols"

  let sanitize_name raw =
    let buf = Buffer.create (String.length raw) in
    String.iter
      (fun ch ->
        match ch with
        | 'a' .. 'z' | 'A' .. 'Z' | '0' .. '9' -> Buffer.add_char buf ch
        | _ -> Buffer.add_char buf '_')
      raw;
    Buffer.contents buf

  let rec resolved_name = function
    | ResolvedInt (Signed, bits) -> Printf.sprintf "i%d" bits
    | ResolvedInt (Unsigned, bits) -> Printf.sprintf "u%d" bits
    | ResolvedFloat -> "float"
    | ResolvedString -> "str"
    | ResolvedVoid -> "void"
    | ResolvedPointer inner -> "ptr_" ^ resolved_name inner
    | ResolvedBox inner -> "box_" ^ resolved_name inner
    | ResolvedCell inner -> "cell_" ^ resolved_name inner
    | ResolvedArray (inner, count) ->
        Printf.sprintf "arr%d_%s" count (resolved_name inner)
    | ResolvedVec vec -> Printf.sprintf "fvec%d" vec.dimension
    | ResolvedMatrix mat -> Printf.sprintf "mat%dx%d" mat.rows mat.columns
    | ResolvedFunction (params, ret, _vararg) ->
        "fn_" ^ String.concat "_" (List.map resolved_name params) ^ "_to_"
        ^ resolved_name ret
    | ResolvedNamed (name, []) -> sanitize_name name
    | ResolvedNamed (name, args) ->
        sanitize_name name ^ "_" ^ String.concat "_" (List.map resolved_name args)
    | ResolvedGenericParam name -> sanitize_name name
    | ResolvedVecHole -> "fvec_hole"
    | ResolvedMatrixHole -> "mat_hole"

  let instance_key (fn : Core.function_decl) param_types =
    function_id fn ^ "::" ^ String.concat "::" (List.map resolved_name param_types)

  let instance_name (fn : Core.function_decl) param_types =
    fn.value.name.value ^ "__spec__" ^ String.concat "__" (List.map resolved_name param_types)

  let collect_functions (program : Core.program) =
    let add_fn map (fn : Core.function_decl) =
      String_map.add fn.value.name.value fn map
    in
    List.fold_left
      (fun map (decl : Core.top_decl) ->
        match decl.value with
        | Core.FDecl fn -> add_fn map fn
        | Core.Foreign foreign -> List.fold_left add_fn map foreign.value.decls
        | Core.VDecl _ | Core.TDecl _ | Core.Import _ | Core.CImport _ -> map)
      String_map.empty program.value.decls

  let make_state typed =
    {
      typed;
      functions = collect_functions typed.program.program;
      instances = Hashtbl.create 32;
      queue_rev = [];
      diagnostics_rev = [];
    }

  let canonical_param_type type_env (param : Core.param) arg_type =
    if type_has_specialization_hole param.value.ty then arg_type
    else
      match resolve_core_type type_env [] [] param.loc param.value.ty with
      | Some resolved -> resolved
      | None -> arg_type

  let canonical_param_types type_env (fn : Core.function_decl) arg_types =
    List.map2 (canonical_param_type type_env) fn.value.params.value.params arg_types

  let enqueue_instance state (fn : Core.function_decl) param_types return_type =
    let key = instance_key fn param_types in
    match Hashtbl.find_opt state.instances key with
    | Some existing ->
        if not (equal_resolved_type existing.return_type return_type) then
          add_diagnostic state Error fn.loc
            (Printf.sprintf
               "specialization %s was inferred with incompatible return types"
               fn.value.name.value);
        existing.name
    | None ->
        let instance =
          {
            key;
            name = instance_name fn param_types;
            template = fn;
            param_types;
            return_type;
          }
        in
        Hashtbl.add state.instances key instance;
        state.queue_rev <- instance :: state.queue_rev;
        instance.name

  let clone_identifier (id : Core.identifier) value = { id with value }

  let literal_int loc value =
    mk_expr loc (Core.Literal (mk_literal loc (Core.Integer value)))

  let rec rewrite_expression state annotations (expr : Core.expression) :
      Core.expression =
    match expr.value with
    | Core.Binary binary ->
        {
          expr with
          value =
            Core.Binary
              {
                binary with
                value =
                  {
                    binary.value with
                    left = rewrite_expression state annotations binary.value.left;
                    right = rewrite_expression state annotations binary.value.right;
                  };
              };
        }
    | Core.Unary unary ->
        {
          expr with
          value =
            Core.Unary
              {
                unary with
                value =
                  {
                    unary.value with
                    inner = rewrite_expression state annotations unary.value.inner;
                  };
              };
        }
    | Core.Block block ->
        { expr with value = Core.Block (rewrite_block state annotations block) }
    | Core.ToBool inner ->
        { expr with value = Core.ToBool (rewrite_expression state annotations inner) }
    | Core.Initializer init ->
        {
          expr with
          value =
            Core.Initializer
              {
                init with
                value =
                  {
                    Core.exprs =
                      List.map (rewrite_expression state annotations) init.value.exprs;
                  };
              };
        }
    | Core.As cast ->
        {
          expr with
          value =
            Core.As
              {
                cast with
                value =
                  {
                    cast.value with
                    inner = rewrite_expression state annotations cast.value.inner;
                  };
              };
        }
    | Core.SizeExpr inner ->
        { expr with value = Core.SizeExpr (rewrite_expression state annotations inner) }
    | Core.Match match_expr ->
        {
          expr with
          value =
            Core.Match
              {
                match_expr with
                value =
                  {
                    Core.expr =
                      rewrite_expression state annotations match_expr.value.expr;
                    arms =
                      List.map
                        (fun (arm : Core.match_arm) ->
                          {
                            arm with
                            value =
                              {
                                arm.value with
                                expr =
                                  rewrite_expression state annotations arm.value.expr;
                              };
                          })
                        match_expr.value.arms;
                  };
              };
        }
    | Core.BoxExpr inner ->
        { expr with value = Core.BoxExpr (rewrite_expression state annotations inner) }
    | Core.BoxConstruct box ->
        {
          expr with
          value =
            Core.BoxConstruct
              {
                box with
                value =
                  {
                    box.value with
                    args = List.map (rewrite_expression state annotations) box.value.args;
                  };
              };
        }
    | Core.Unbox inner ->
        { expr with value = Core.Unbox (rewrite_expression state annotations inner) }
    | Core.Ref inner ->
        { expr with value = Core.Ref (rewrite_expression state annotations inner) }
    | Core.Load inner ->
        { expr with value = Core.Load (rewrite_expression state annotations inner) }
    | Core.Call call ->
        rewrite_call state annotations expr call
    | Core.Index index ->
        {
          expr with
          value =
            Core.Index
              {
                index with
                value =
                  {
                    Core.target =
                      rewrite_expression state annotations index.value.target;
                    index = rewrite_expression state annotations index.value.index;
                  };
              };
        }
    | Core.Field field ->
        let target = rewrite_expression state annotations field.value.target in
        if is_shape_property field.value.field.value then
          match exact_integer annotations expr with
          | Some value -> literal_int expr.loc value
          | None ->
              add_diagnostic state Error expr.loc
                (Printf.sprintf "could not resolve %s to a concrete compile-time value"
                   field.value.field.value);
              {
                expr with
                value = Core.Field { field with value = { field.value with target } };
              }
        else
          {
            expr with
            value = Core.Field { field with value = { field.value with target } };
          }
    | Core.Assign write ->
        {
          expr with
          value =
            Core.Assign
              {
                write with
                value =
                  {
                    Core.target =
                      rewrite_expression state annotations write.value.target;
                    value = rewrite_expression state annotations write.value.value;
                  };
              };
        }
    | Core.Mutate write ->
        {
          expr with
          value =
            Core.Mutate
              {
                write with
                value =
                  {
                    Core.target =
                      rewrite_expression state annotations write.value.target;
                    value = rewrite_expression state annotations write.value.value;
                  };
              };
        }
    | (Core.Identifier _ | Core.Literal _ | Core.SizeType _ | Core.Nil | Core.Zero
      | Core.BoxType _) ->
        expr

  and rewrite_call state annotations (expr : Core.expression) (call : Core.call) =
    let target = rewrite_expression state annotations call.value.target in
    let params = List.map (rewrite_expression state annotations) call.value.params in
    match call.value.target.value with
    | Core.Identifier id -> (
        match String_map.find_opt id.value state.functions with
        | Some fn when function_has_specialization_param fn -> (
            let arg_types = List.map (resolved_expr_type annotations) call.value.params in
            let return_type = resolved_expr_type annotations expr in
            match
              ( List.for_all Option.is_some arg_types,
                return_type,
                List.length arg_types = List.length fn.value.params.value.params )
            with
            | true, Some return_type, true ->
                let arg_types = List.map Option.get arg_types in
                let param_types =
                  canonical_param_types
                    (type_env_of_program state.typed.program.program)
                    fn arg_types
                in
                let specialized_name =
                  enqueue_instance state fn param_types return_type
                in
                {
                  expr with
                  value =
                    Core.Call
                      {
                        call with
                        value =
                          {
                            Core.target =
                              mk_expr call.value.target.loc
                                (Core.Identifier
                                   (clone_identifier id specialized_name));
                            params;
                          };
                      };
                }
            | _ ->
                add_diagnostic state Error expr.loc
                  (Printf.sprintf
                     "could not concretize specialization call to %s before lowering"
                     id.value);
                { expr with value = Core.Call { call with value = { Core.target = target; params } } })
        | _ ->
            { expr with value = Core.Call { call with value = { Core.target = target; params } } })
    | _ ->
        { expr with value = Core.Call { call with value = { Core.target = target; params } } }

  and rewrite_statement state annotations (stmt : Core.statement) =
    let value =
      match stmt.value with
      | Core.Expression expr ->
          Core.Expression (rewrite_expression state annotations expr)
      | Core.CompileAssert compile_assert ->
          Core.CompileAssert
            compile_assert
      | Core.Return expr ->
          Core.Return (Option.map (rewrite_expression state annotations) expr)
      | Core.Defer expr ->
          Core.Defer (rewrite_expression state annotations expr)
      | Core.Let binding ->
          Core.Let
            {
              binding with
              value =
                {
                  binding.value with
                  init_expr =
                    rewrite_expression state annotations binding.value.init_expr;
                };
            }
      | Core.Loop loop ->
          Core.Loop
            {
              loop with
              value =
                {
                  loop.value with
                  init = List.map (rewrite_statement state annotations) loop.value.init;
                  cond = rewrite_expression state annotations loop.value.cond;
                  body = rewrite_block state annotations loop.value.body;
                  step = List.map (rewrite_statement state annotations) loop.value.step;
                };
            }
      | Core.Break | Core.Continue as value -> value
    in
    { stmt with value }

  and rewrite_block state annotations (block : Core.block) =
    {
      block with
      value =
        {
          Core.statements =
            List.map (rewrite_statement state annotations) block.value.statements;
          result = Option.map (rewrite_expression state annotations) block.value.result;
        };
    }

  let binding_of_arg_type loc arg_type =
    let ty = core_type_of_resolved_ty loc arg_type in
    {
      inferred_type = Some ty;
      resolved_type = Some arg_type;
      metavar = metavar_of_type ty;
      is_mutable = false;
    }

  let specialize_param (param : Core.param) arg_type =
    if type_has_specialization_hole param.value.ty then
      {
        param with
        value =
          {
            param.value with
            ty = core_type_of_resolved_ty param.value.ty.loc arg_type;
          };
      }
    else param

  let rewrite_function_with_annotations state annotations (fn : Core.function_decl) =
    {
      fn with
      value =
        {
          fn.value with
          definition = Option.map (rewrite_block state annotations) fn.value.definition;
        };
    }

  let specialize_instance_decl state (inst : instance) =
    let param_bindings =
      List.map2
        (fun (param : Core.param) arg_type ->
          binding_of_arg_type param.loc arg_type)
        inst.template.value.params.value.params inst.param_types
    in
    let temp_typed, _body_result =
      Typing.analyze_function_body state.typed.program
        ~active_specializations:[ function_id inst.template ]
        ~param_bindings inst.template
    in
    List.iter
      (fun diagnostic -> state.diagnostics_rev <- diagnostic :: state.diagnostics_rev)
      (List.rev temp_typed.diagnostics);
    let params =
      List.map2 specialize_param inst.template.value.params.value.params inst.param_types
    in
    {
      inst.template with
      value =
        {
          inst.template.value with
          name = clone_identifier inst.template.value.name inst.name;
          params =
            { inst.template.value.params with value = { inst.template.value.params.value with params } };
          return_type =
            Some (core_type_of_resolved_ty inst.template.loc inst.return_type);
          definition =
            Option.map
              (rewrite_block state temp_typed.annotations)
              inst.template.value.definition;
        };
    }

  let rec drain_instances state acc =
    match state.queue_rev with
    | [] -> List.rev acc
    | inst :: rest ->
        state.queue_rev <- rest;
        let fn = specialize_instance_decl state inst in
        let decl : Core.top_decl = { Core.value = Core.FDecl fn; loc = fn.loc } in
        drain_instances state (decl :: acc)

  let rewrite_decl state annotations (decl : Core.top_decl) =
    match decl.value with
    | Core.FDecl fn ->
        if function_has_specialization_param fn then None
        else
          Some
            {
              decl with
              value = Core.FDecl (rewrite_function_with_annotations state annotations fn);
            }
    | Core.Foreign foreign ->
        let decls =
          List.filter_map
            (fun (fn : Core.function_decl) ->
              if function_has_specialization_param fn then None
              else Some (rewrite_function_with_annotations state annotations fn))
            foreign.value.decls
        in
        Some
          {
            decl with
            value = Core.Foreign { foreign with value = { foreign.value with decls } };
          }
    | Core.VDecl binding ->
        Some
          {
            decl with
            value =
              Core.VDecl
                {
                  binding with
                  value =
                    {
                      binding.value with
                      init_expr =
                        Option.map
                          (rewrite_expression state annotations)
                          binding.value.init_expr;
                    };
                };
          }
    | Core.TDecl _ | Core.Import _ | Core.CImport _ -> Some decl

  let run (typed : typing_result) : result =
    let state = make_state typed in
    let base_decls =
      List.filter_map
        (rewrite_decl state typed.annotations)
        typed.program.program.value.decls
    in
    let specialized_decls = drain_instances state [] in
    {
      program =
        {
          Core.program =
            {
              typed.program.program with
              value = { Core.decls = base_decls @ specialized_decls };
            };
        };
      diagnostics = List.rev state.diagnostics_rev;
    }
end
