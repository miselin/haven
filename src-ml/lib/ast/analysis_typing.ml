open Analysis_types

module Typing = struct
  type env = binding_annotation String_map.t list

  type state = {
    annotations : annotations;
    mutable diagnostics_rev : diagnostic list;
    mutable globals : binding_annotation String_map.t;
    type_env : type_env;
  }

  let add_diagnostic_with_category state category level loc message =
    state.diagnostics_rev <- { category; level; loc; message } :: state.diagnostics_rev

  let add_diagnostic state = add_diagnostic_with_category state TypeCheck

  let push_scope env = String_map.empty :: env

  let pop_scope = function [] -> [] | _ :: rest -> rest

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

  let record_expr state (expr : Core.expression) (annotation : expr_annotation) :
      expr_annotation =
    Hashtbl.replace state.annotations.exprs (expr_id expr) annotation;
    annotation

  let record_binding state (binding : Core.let_stmt)
      (annotation : binding_annotation) : binding_annotation =
    Hashtbl.replace state.annotations.bindings (binding_id binding) annotation;
    annotation

  let nth_or_none items index =
    if index < List.length items then List.nth items index else None

  let binding_from_type ?(is_mutable = false) type_env (ty : Core.haven_type) =
    let resolved_type = resolve_core_type type_env [] [] ty.loc ty in
    {
      inferred_type = Some ty;
      resolved_type;
      metavar = metavar_of_type ty;
      is_mutable;
    }

  let annotation_of_resolved loc resolved_type =
    match resolved_type with
    | Some resolved ->
        let inferred_type = Some (core_type_of_resolved_ty loc resolved) in
        {
          inferred_type;
          resolved_type = Some resolved;
          metavar = metavar_of_type (core_type_of_resolved_ty loc resolved);
        }
    | None -> unknown_expr_annotation

  let maybe_coerce_annotation loc expected_type annotation =
    match expected_type with
    | Some expected -> coerce_annotation_to_expected loc expected annotation
    | None -> annotation

  let expected_enum_variant state loc expected_type variant_name =
    match expected_type with
    | Some enum_ty -> lookup_enum_variant state.type_env loc enum_ty variant_name
    | None -> None

  let function_type_of_decl (fn : Core.function_decl) =
    let return_type = Option.value ~default:(void_type fn.loc) fn.value.return_type in
    mk_type fn.loc
      (Core.FunctionType
         {
           Core.value =
             {
               param_types =
                 List.map (fun (param : Core.param) -> param.value.ty)
                   fn.value.params.value.params;
               return_type;
               vararg = fn.value.vararg;
             };
           loc = fn.loc;
         })

  let collect_globals state (program : Core.program) =
    let add_global name binding =
      state.globals <- String_map.add name binding state.globals
    in
    let add_function (fn : Core.function_decl) =
      let ty = function_type_of_decl fn in
      add_global fn.value.name.value
        {
          inferred_type = Some ty;
          resolved_type = resolve_core_type state.type_env [] [] fn.loc ty;
          metavar = metavar_of_type ty;
          is_mutable = false;
        }
    in
    List.iter
      (fun (decl : Core.top_decl) ->
        match decl.value with
        | Core.FDecl fn -> add_function fn
        | Core.Foreign foreign -> List.iter add_function foreign.value.decls
        | Core.VDecl binding ->
            add_global binding.value.name.value
              {
                inferred_type = Some binding.value.ty;
                resolved_type =
                  resolve_core_type state.type_env [] [] binding.loc binding.value.ty;
                metavar = metavar_of_type binding.value.ty;
                is_mutable = binding.value.is_mutable;
              }
        | Core.TDecl _ | Core.Import _ | Core.CImport _ -> ())
      program.value.decls

  let rec infer_block state env ?(result_expected : resolved_ty option = None)
      ~return_expected (block : Core.block) :
      expr_annotation =
    let env = push_scope env in
    let env =
      List.fold_left (infer_statement state ~return_expected) env block.value.statements
    in
    match block.value.result with
    | Some expr ->
        let expr_ann = infer_expression state env ~expected_type:result_expected expr in
        {
          inferred_type = expr_ann.inferred_type;
          resolved_type = expr_ann.resolved_type;
          metavar = expr_ann.metavar;
        }
    | None ->
        let ty = void_type block.loc in
        {
          inferred_type = Some ty;
          resolved_type = Some ResolvedVoid;
          metavar = metavar_of_type ty;
        }

  and infer_statement state ~return_expected env (stmt : Core.statement) : env =
    match stmt.value with
    | Core.Expression expr ->
        ignore (infer_expression state env expr);
        env
    | Core.Return expr ->
        Option.iter
          (fun (expr : Core.expression) ->
            ignore (infer_expression state env ~expected_type:return_expected expr))
          expr;
        env
    | Core.Defer expr ->
        ignore (infer_expression state env expr);
        env
    | Core.Break | Core.Continue -> env
    | Core.Let binding ->
        let init_expected =
          Option.bind binding.value.ty (resolve_core_type state.type_env [] [] binding.loc)
        in
        let init_ann =
          infer_expression state env ~expected_type:init_expected binding.value.init_expr
        in
        let inferred_type =
          match binding.value.ty with
          | Some ty -> Some ty
          | None -> init_ann.inferred_type
        in
        let binding_metavar =
          match (binding.value.ty, inferred_type) with
          | Some ty, _ -> metavar_of_type ty
          | None, Some ty -> { init_ann.metavar with classes = type_class_of_type ty }
          | None, None -> init_ann.metavar
        in
        let binding_ann =
          {
            inferred_type;
            resolved_type =
              (match binding.value.ty with
              | Some ty -> resolve_core_type state.type_env [] [] binding.loc ty
              | None -> init_ann.resolved_type);
            metavar = binding_metavar;
            is_mutable = binding.value.mut;
          }
        in
        ignore (record_binding state binding binding_ann);
        bind_current env binding.value.name.value binding_ann
    | Core.Loop loop ->
        let loop_env = push_scope env in
        let loop_env =
          List.fold_left (infer_statement state ~return_expected) loop_env loop.value.init
        in
        ignore (infer_expression state loop_env loop.value.cond);
        ignore (infer_block state loop_env ~return_expected loop.value.body);
        ignore
          (List.fold_left (infer_statement state ~return_expected) loop_env loop.value.step);
        env

  and infer_expression state env ?(expected_type : resolved_ty option = None)
      (expr : Core.expression) : expr_annotation =
    let annotation =
      match expr.value with
      | Core.Identifier id -> (
          match lookup env id.value with
          | Some binding ->
              {
                inferred_type = binding.inferred_type;
                resolved_type = binding.resolved_type;
                metavar = binding.metavar;
              }
          | None -> (
              match String_map.find_opt id.value state.globals with
              | Some binding ->
                  {
                    inferred_type = binding.inferred_type;
                    resolved_type = binding.resolved_type;
                    metavar = binding.metavar;
                  }
              | None -> (
                  match expected_enum_variant state expr.loc expected_type id.value with
                  | Some (_, None) -> annotation_of_resolved expr.loc expected_type
                  | Some (_, Some _) -> unknown_expr_annotation
                  | None ->
                      add_diagnostic state Error expr.loc
                        (Printf.sprintf "unknown identifier %s" id.value);
                      unknown_expr_annotation)))
      | Core.Literal literal -> infer_literal state env ~expected_type expr.loc literal
      | Core.ToBool inner ->
          let inner_ann = infer_expression state env inner in
          let ty = bool_type expr.loc in
          {
            inferred_type = Some ty;
            resolved_type = Some (ResolvedInt (Unsigned, 1));
            metavar =
              {
                classes = [ TypeClassBool; TypeClassNumeric ];
                constant =
                  (match inner_ann.metavar.constant with
                  | Some (ConstantInt 0) -> Some (ConstantBool false)
                  | Some (ConstantBool value) -> Some (ConstantBool value)
                  | _ -> None);
                integer = None;
              };
          }
      | Core.Unary unary -> infer_unary state env expr.loc unary
      | Core.Binary binary -> infer_binary state env expr.loc binary
      | Core.Block block ->
          infer_block state env ~result_expected:expected_type ~return_expected:None block
      | Core.Initializer init ->
          infer_initializer state env ~expected_type expr.loc init
      | Core.As cast ->
          ignore (infer_expression state env cast.value.inner);
          {
            inferred_type = Some cast.value.target_type;
            resolved_type =
              resolve_core_type state.type_env [] [] cast.loc cast.value.target_type;
            metavar = metavar_of_type cast.value.target_type;
          }
      | Core.SizeExpr inner ->
          ignore (infer_expression state env inner);
          let ty = numeric_type expr.loc Unsigned 64 in
          {
            inferred_type = Some ty;
            resolved_type = Some (ResolvedInt (Unsigned, 64));
            metavar = metavar_of_type ty;
          }
      | Core.SizeType _ ->
          let ty = numeric_type expr.loc Unsigned 64 in
          {
            inferred_type = Some ty;
            resolved_type = Some (ResolvedInt (Unsigned, 64));
            metavar = metavar_of_type ty;
          }
      | Core.Nil ->
          {
            inferred_type = None;
            resolved_type = None;
            metavar =
              {
                classes = [ TypeClassNil ];
                constant = None;
                integer = None;
              };
          }
      | Core.Match match_expr -> infer_match state env ~expected_type expr.loc match_expr
      | Core.BoxExpr inner -> (
          let inner_ann = infer_expression state env inner in
          match inner_ann.resolved_type with
          | Some inner_ty ->
              let ty = box_type expr.loc (core_type_of_resolved_ty expr.loc inner_ty) in
              {
                inferred_type = Some ty;
                resolved_type = Some (ResolvedBox inner_ty);
                metavar = metavar_of_type ty;
              }
          | None ->
              {
                inferred_type = None;
                resolved_type = None;
                metavar = { unknown_metavar with classes = [ TypeClassBox ] };
              })
      | Core.BoxType ty ->
          let core_ty = box_type expr.loc ty in
          {
            inferred_type = Some core_ty;
            resolved_type =
              Option.map (fun ty -> ResolvedBox ty)
                (resolve_core_type state.type_env [] [] expr.loc ty);
            metavar = metavar_of_type core_ty;
          }
      | Core.Unbox inner -> (
          let inner_ann = infer_expression state env inner in
          match inner_ann.resolved_type with
          | Some ty -> (
              match ty with
              | ResolvedBox inner_ty ->
                  let core_ty = core_type_of_resolved_ty expr.loc inner_ty in
                  {
                    inferred_type = Some core_ty;
                    resolved_type = Some inner_ty;
                    metavar = metavar_of_type core_ty;
                  }
              | _ -> unknown_expr_annotation)
          | None -> unknown_expr_annotation)
      | Core.Ref inner -> (
          let inner_ann = infer_expression state env inner in
          match inner_ann.resolved_type with
          | Some inner_ty ->
              let ty = pointer_type expr.loc (core_type_of_resolved_ty expr.loc inner_ty) in
              {
                inferred_type = Some ty;
                resolved_type = Some (ResolvedPointer inner_ty);
                metavar = metavar_of_type ty;
              }
          | None ->
              {
                inferred_type = None;
                resolved_type = None;
                metavar = { unknown_metavar with classes = [ TypeClassPointer ] };
              })
      | Core.Load inner -> (
          let inner_ann = infer_expression state env inner in
          match inner_ann.resolved_type with
          | Some ty -> (
              match ty with
              | ResolvedPointer inner_ty | ResolvedCell inner_ty ->
                  let core_ty = core_type_of_resolved_ty expr.loc inner_ty in
                  {
                    inferred_type = Some core_ty;
                    resolved_type = Some inner_ty;
                    metavar = metavar_of_type core_ty;
                  }
              | _ -> unknown_expr_annotation)
          | None -> unknown_expr_annotation)
      | Core.Call call -> infer_call state env ~expected_type expr.loc call
      | Core.Index index -> infer_index state env expr.loc index
      | Core.Field field -> (
          let target_ann = infer_expression state env field.value.target in
          let target_ty =
            match (field.value.arrow, target_ann.resolved_type) with
            | true, Some ty -> resolved_deref_once ty
            | false, Some ty -> Some ty
            | _, None -> None
          in
          match target_ty with
          | Some (ResolvedNamed _ as target_ty) -> (
              match lookup_struct_fields state.type_env expr.loc target_ty with
              | Some fields -> (
                  match List.assoc_opt field.value.field.value fields with
                  | Some field_ty ->
                      let core_ty = core_type_of_resolved_ty expr.loc field_ty in
                      {
                        inferred_type = Some core_ty;
                        resolved_type = Some field_ty;
                        metavar = metavar_of_type core_ty;
                      }
                  | None -> unknown_expr_annotation)
              | None -> unknown_expr_annotation)
          | Some (ResolvedVec vec) -> (
              match vector_field_index field.value.field.value with
              | Some idx when idx < vec.dimension ->
                  let core_ty = float_type expr.loc in
                  {
                    inferred_type = Some core_ty;
                    resolved_type = Some ResolvedFloat;
                    metavar = metavar_of_type core_ty;
                  }
              | _ -> unknown_expr_annotation)
          | Some (ResolvedMatrix mat) -> (
              match vector_field_index field.value.field.value with
              | Some idx when idx < mat.rows ->
                  let vec_ty =
                    mk_type expr.loc
                      (Core.VecType { kind = FloatVec; dimension = mat.columns })
                  in
                  {
                    inferred_type = Some vec_ty;
                    resolved_type =
                      Some (ResolvedVec { kind = FloatVec; dimension = mat.columns });
                    metavar = metavar_of_type vec_ty;
                  }
              | _ -> unknown_expr_annotation)
          | _ -> unknown_expr_annotation)
      | Core.Assign write ->
          infer_write_like state env ~pointee_target:false write
      | Core.Mutate write ->
          infer_write_like state env ~pointee_target:true write
    in
    let annotation = maybe_coerce_annotation expr.loc expected_type annotation in
    record_expr state expr annotation

  and infer_literal state env ~(expected_type : resolved_ty option) loc
      (literal : Core.literal) : expr_annotation =
    match literal.value with
    | Core.Integer value ->
        let ty = smallest_integer_type loc value in
        {
          inferred_type = Some ty;
          resolved_type = Some (ResolvedInt ((if value < 0 then Signed else Unsigned), exact_integer_bits value));
          metavar =
            {
              classes = [ TypeClassNumeric ];
              constant = Some (ConstantInt value);
              integer =
                Some
                  {
                    exact_value = Some value;
                    minimum_bits = Some (exact_integer_bits value);
                    signedness = Some (if value < 0 then Signed else Unsigned);
                  };
            };
        }
    | Core.Bool value ->
        let ty = bool_type loc in
        {
          inferred_type = Some ty;
          resolved_type = Some (ResolvedInt (Unsigned, 1));
          metavar =
            {
              classes = [ TypeClassBool; TypeClassNumeric ];
              constant = Some (ConstantBool value);
              integer =
                Some
                  {
                    exact_value = Some (if value then 1 else 0);
                    minimum_bits = Some 1;
                    signedness = Some Unsigned;
                  };
            };
        }
    | Core.Float value ->
        let ty = float_type loc in
        {
          inferred_type = Some ty;
          resolved_type = Some ResolvedFloat;
          metavar =
            {
              classes = [ TypeClassFloat ];
              constant = Some (ConstantFloat value);
              integer = None;
            };
        }
    | Core.String value ->
        let ty = string_type loc in
        {
          inferred_type = Some ty;
          resolved_type = Some ResolvedString;
          metavar =
            {
              classes = [ TypeClassString ];
              constant = Some (ConstantString value);
              integer = None;
            };
        }
    | Core.Char value ->
        let ty = numeric_type loc Unsigned 8 in
        {
          inferred_type = Some ty;
          resolved_type = Some (ResolvedInt (Unsigned, 8));
          metavar =
            {
              classes = [ TypeClassNumeric ];
              constant = Some (ConstantChar value);
              integer =
                Some
                  {
                    exact_value = Some (Char.code value);
                    minimum_bits = Some 8;
                    signedness = Some Unsigned;
                  };
            };
        }
    | Core.Vector vec ->
        let element_expected =
          match expected_type with Some ResolvedVec _ -> Some ResolvedFloat | _ -> None
        in
        let element_anns =
          List.map
            (fun (expr : Core.expression) ->
              infer_expression state env ~expected_type:element_expected expr)
            vec.value.elements
        in
        let has_float =
          List.exists
            (fun (ann : expr_annotation) ->
              match ann.inferred_type with
              | Some ty -> ty.value = Core.FloatType
              | None -> false)
            element_anns
        in
        let ty =
          if has_float || List.length vec.value.elements > 0 then
            Some
              (mk_type loc
                 (Core.VecType { kind = FloatVec; dimension = List.length vec.value.elements }))
          else None
        in
        {
          inferred_type = ty;
          resolved_type =
            Option.map (fun _ -> ResolvedVec { kind = FloatVec; dimension = List.length vec.value.elements }) ty;
          metavar =
            {
              classes = [ TypeClassVector ];
              constant = None;
              integer = None;
            };
        }
    | Core.Matrix mat ->
        let rows =
          List.map
            (fun (row : Core.vec_literal) ->
              let row_expected =
                match expected_type with
                | Some (ResolvedMatrix matrix) ->
                    Some (ResolvedVec { kind = FloatVec; dimension = matrix.columns })
                | _ -> None
              in
              infer_literal state env ~expected_type:row_expected row.loc
                (mk_literal row.loc (Core.Vector row)))
            mat.value.rows
        in
        let columns =
          match mat.value.rows with
          | row :: _ -> List.length row.value.elements
          | [] -> 0
        in
        ignore rows;
        {
          inferred_type =
            Some
              (mk_type loc
                 (Core.MatrixType { kind = GenericMat; rows = List.length mat.value.rows; columns }));
          resolved_type =
            Some
              (ResolvedMatrix
                 { kind = GenericMat; rows = List.length mat.value.rows; columns });
          metavar =
            {
              classes = [ TypeClassMatrix ];
              constant = None;
              integer = None;
            };
        }
    | Core.Enum enum ->
        let resolved_type =
          if enum.value.types = [] then
            match expected_type with
            | Some (ResolvedNamed (name, _) as expected)
              when String.equal name enum.value.enum_name.value ->
                Some expected
            | Some expected -> (
                match expected with
                | ResolvedNamed (name, _) when String.equal name enum.value.enum_name.value ->
                    Some expected
                | _ -> None)
            | None -> None
          else
            None
        in
        let ty =
          match resolved_type with
          | Some resolved -> core_type_of_resolved_ty loc resolved
          | None when enum.value.types = [] ->
              mk_type loc
                (Core.CustomType
                   {
                     name =
                       mk_identifier enum.value.enum_name.loc enum.value.enum_name.value;
                   })
          | None ->
            mk_type loc
              (Core.TemplatedType
                 {
                   Core.value =
                     {
                       outer = enum.value.enum_name;
                       inner = enum.value.types;
                     };
                   loc;
                 })
        in
        List.iter
          (fun (expr : Core.expression) -> ignore (infer_expression state env expr))
          enum.value.wrapped;
        {
          inferred_type = Some ty;
          resolved_type =
            (match resolved_type with
            | Some resolved -> Some resolved
            | None -> resolve_core_type state.type_env [] [] loc ty);
          metavar = metavar_of_type ty;
        }

  and infer_initializer state env ~(expected_type : resolved_ty option) loc
      (init : Core.init_list) : expr_annotation =
    let infer_all expected_types =
      List.iteri
        (fun index (expr : Core.expression) ->
          let expected = nth_or_none expected_types index in
          ignore (infer_expression state env ~expected_type:expected expr))
        init.value.exprs
    in
    (match expected_type with
    | Some (ResolvedArray (element_ty, count)) ->
        let expected_types =
          List.init (min count (List.length init.value.exprs)) (fun _ -> Some element_ty)
        in
        infer_all expected_types
    | Some (ResolvedNamed _ as struct_ty) -> (
        match lookup_struct_fields state.type_env loc struct_ty with
        | Some fields ->
            let expected_types =
              List.map (fun (_, field_ty) -> Some field_ty) fields
              |> List.filteri (fun index _ -> index < List.length init.value.exprs)
            in
            infer_all expected_types
        | None ->
            List.iter
              (fun (expr : Core.expression) -> ignore (infer_expression state env expr))
              init.value.exprs)
    | Some (ResolvedVec _) ->
        List.iter
          (fun (expr : Core.expression) ->
            ignore (infer_expression state env ~expected_type:(Some ResolvedFloat) expr))
          init.value.exprs
    | _ ->
        List.iter
          (fun (expr : Core.expression) -> ignore (infer_expression state env expr))
          init.value.exprs);
    match expected_type with
    | Some expected -> annotation_of_resolved loc (Some expected)
    | None -> unknown_expr_annotation

  and infer_unary state env loc (unary : Core.unary) : expr_annotation =
    let inner_ann = infer_expression state env unary.value.inner in
    match unary.value.op with
    | Core.Not ->
        let ty = bool_type loc in
        {
          inferred_type = Some ty;
          resolved_type = Some (ResolvedInt (Unsigned, 1));
          metavar = metavar_of_type ty;
        }
    | Core.Negate | Core.Complement -> (
        match (inner_ann.inferred_type, inner_ann.resolved_type) with
        | Some ty, resolved_type ->
            { inferred_type = Some ty; resolved_type; metavar = metavar_of_type ty }
        | None, _ ->
            {
              inferred_type = None;
              resolved_type = None;
              metavar = { unknown_metavar with classes = [ TypeClassNumeric ] };
            })

  and infer_binary state env loc (binary : Core.binary) : expr_annotation =
    let left_ann = infer_expression state env binary.value.left in
    let right_ann = infer_expression state env binary.value.right in
    match binary.value.op with
    | Core.IsEqual
    | Core.NotEqual
    | Core.LessThan
    | Core.LessThanOrEqual
    | Core.GreaterThan
    | Core.GreaterThanOrEqual
    | Core.LogicAnd
    | Core.LogicOr ->
        let ty = bool_type loc in
        {
          inferred_type = Some ty;
          resolved_type = Some (ResolvedInt (Unsigned, 1));
          metavar = metavar_of_type ty;
        }
    | Core.Add
    | Core.Subtract
    | Core.Multiply
    | Core.Divide
    | Core.Modulo
    | Core.LeftShift
    | Core.RightShift
    | Core.BitwiseAnd
    | Core.BitwiseOr
    | Core.BitwiseXor -> (
        match (left_ann.inferred_type, right_ann.inferred_type) with
        | Some left_ty, Some right_ty
          when is_numeric_type left_ty || left_ty.value = Core.FloatType ->
            let ty =
              if is_numeric_type right_ty || right_ty.value = Core.FloatType then
                wider_numeric_type loc left_ty right_ty
              else left_ty
            in
            {
              inferred_type = Some ty;
              resolved_type = resolve_core_type state.type_env [] [] loc ty;
              metavar = metavar_of_type ty;
            }
        | Some left_ty, Some _ ->
            {
              inferred_type = Some left_ty;
              resolved_type = resolve_core_type state.type_env [] [] loc left_ty;
              metavar = metavar_of_type left_ty;
            }
        | Some ty, None | None, Some ty ->
            {
              inferred_type = Some ty;
              resolved_type = resolve_core_type state.type_env [] [] loc ty;
              metavar = metavar_of_type ty;
            }
        | None, None -> (
            match
              ( Option.bind left_ann.metavar.integer (fun i -> i.exact_value),
                Option.bind right_ann.metavar.integer (fun i -> i.exact_value) )
            with
            | Some left_value, Some right_value ->
                let ty = smallest_integer_type loc (max left_value right_value) in
                {
                  inferred_type = Some ty;
                  resolved_type = resolve_core_type state.type_env [] [] loc ty;
                  metavar = metavar_of_type ty;
                }
            | _ ->
                {
                  inferred_type = None;
                  resolved_type = None;
                  metavar = { unknown_metavar with classes = [ TypeClassNumeric ] };
                }))

  and infer_match state env ~(expected_type : resolved_ty option) loc
      (match_expr : Core.match_expr) :
      expr_annotation =
    ignore (infer_expression state env match_expr.value.expr);
    let arm_types =
      List.map
        (fun (arm : Core.match_arm) ->
          let env = infer_pattern_bindings state env match_expr.value.expr arm.value.pattern in
          let ann = infer_expression state env ~expected_type arm.value.expr in
          ann.inferred_type)
        match_expr.value.arms
    in
    let inferred_type =
      match arm_types with
      | [] -> Some (void_type loc)
      | first :: rest ->
          List.fold_left
            (fun acc next ->
              match (acc, next) with
              | Some left, Some right when equal_type left right -> Some left
              | Some left, Some right
                when is_numeric_type left || left.value = Core.FloatType ->
                  Some (wider_numeric_type loc left right)
              | Some left, Some _ -> Some left
              | Some ty, None | None, Some ty -> Some ty
              | None, None -> None)
            first rest
    in
    {
      inferred_type;
      resolved_type =
        Option.bind inferred_type (resolve_core_type state.type_env [] [] loc);
      metavar =
        (match inferred_type with
        | Some ty -> metavar_of_type ty
        | None -> unknown_metavar);
    }

  and infer_pattern_bindings state env (scrutinee : Core.expression)
      (pattern : Core.match_pattern) : env =
    match pattern.value with
    | Core.PatternDefault | Core.PatternLiteral _ -> push_scope env
    | Core.PatternEnum enum ->
        let initial = push_scope env in
        List.fold_left
          (fun env (binding : Core.pattern_binding) ->
            match binding.value with
            | Core.BindingIgnored -> env
            | Core.BindingNamed id ->
                let binding_ann =
                  match Hashtbl.find_opt state.annotations.exprs (expr_id scrutinee) with
                  | Some { resolved_type = Some scrutinee_ty; _ } -> (
                      match
                        lookup_enum_variant state.type_env pattern.loc scrutinee_ty
                          enum.value.enum_variant.value
                      with
                      | Some (_, Some inner_ty) ->
                          let ty = core_type_of_resolved_ty binding.loc inner_ty in
                          {
                            inferred_type = Some ty;
                            resolved_type = Some inner_ty;
                            metavar = metavar_of_type ty;
                            is_mutable = false;
                          }
                      | Some (_, None) | None ->
                          {
                            inferred_type = None;
                            resolved_type = None;
                            metavar = unknown_metavar;
                            is_mutable = false;
                          })
                  | _ ->
                      {
                        inferred_type = None;
                        resolved_type = None;
                        metavar = unknown_metavar;
                        is_mutable = false;
                      }
                in
                bind_current env id.value binding_ann)
          initial enum.value.binding

  and infer_call state env ~(expected_type : resolved_ty option) _loc
      (call : Core.call) : expr_annotation =
    let infer_args_with_expected expected_args =
      List.iteri
        (fun index (expr : Core.expression) ->
          let expected = nth_or_none expected_args index in
          ignore (infer_expression state env ~expected_type:expected expr))
        call.value.params
    in
    let infer_enum_constructor enum_ty inner_ty =
      infer_args_with_expected
        (match (inner_ty, call.value.params) with
        | Some expected_inner, [ _ ] -> [ Some expected_inner ]
        | _ -> List.init (List.length call.value.params) (fun _ -> None));
      match (inner_ty, call.value.params) with
      | None, [] | Some _, [ _ ] ->
          let ty = core_type_of_resolved_ty call.loc enum_ty in
          {
            inferred_type = Some ty;
            resolved_type = Some enum_ty;
            metavar = metavar_of_type ty;
          }
      | _ -> unknown_expr_annotation
    in
    match call.value.target.value with
    | Core.Identifier id -> (
        match expected_enum_variant state call.loc expected_type id.value with
        | Some (_, inner_ty) -> (
            match expected_type with
            | Some enum_ty -> infer_enum_constructor enum_ty inner_ty
            | None -> unknown_expr_annotation)
        | None ->
            let target_ann = infer_expression state env call.value.target in
            match target_ann.inferred_type with
            | Some ty -> (
                match ty.value with
                | Core.FunctionType fn ->
                    infer_args_with_expected
                      (List.map
                         (fun expected_ty ->
                           resolve_core_type state.type_env [] [] call.loc expected_ty)
                         fn.value.param_types);
                    {
                      inferred_type = Some fn.value.return_type;
                      resolved_type =
                        resolve_core_type state.type_env [] [] call.loc fn.value.return_type;
                      metavar = metavar_of_type fn.value.return_type;
                    }
                | _ -> unknown_expr_annotation)
            | None -> unknown_expr_annotation)
    | _ ->
        let target_ann = infer_expression state env call.value.target in
        match (call.value.target.value, target_ann.resolved_type) with
        | Core.Literal literal, Some enum_ty -> (
            match literal.value with
            | Core.Enum enum_lit -> (
                match
                  lookup_enum_variant state.type_env call.loc enum_ty
                    enum_lit.value.enum_variant.value
                with
                | Some (_, inner_ty) -> infer_enum_constructor enum_ty inner_ty
                | None -> unknown_expr_annotation)
            | _ -> (
                match target_ann.inferred_type with
                | Some ty -> (
                    match ty.value with
                    | Core.FunctionType fn ->
                        infer_args_with_expected
                          (List.map
                             (fun expected_ty ->
                               resolve_core_type state.type_env [] [] call.loc expected_ty)
                             fn.value.param_types);
                        {
                          inferred_type = Some fn.value.return_type;
                          resolved_type =
                            resolve_core_type state.type_env [] [] call.loc fn.value.return_type;
                          metavar = metavar_of_type fn.value.return_type;
                        }
                    | _ -> unknown_expr_annotation)
                | None -> unknown_expr_annotation))
        | _ -> (
            match target_ann.inferred_type with
            | Some ty -> (
                match ty.value with
                | Core.FunctionType fn ->
                    infer_args_with_expected
                      (List.map
                         (fun expected_ty ->
                           resolve_core_type state.type_env [] [] call.loc expected_ty)
                         fn.value.param_types);
                    {
                      inferred_type = Some fn.value.return_type;
                      resolved_type =
                        resolve_core_type state.type_env [] [] call.loc fn.value.return_type;
                      metavar = metavar_of_type fn.value.return_type;
                    }
                | _ -> unknown_expr_annotation)
            | None -> unknown_expr_annotation)

  and infer_index state env _loc (index : Core.index) : expr_annotation =
    let target_ann = infer_expression state env index.value.target in
    ignore (infer_expression state env index.value.index);
    match (target_ann.inferred_type, target_ann.resolved_type) with
    | Some ty, _ -> (
        match ty.value with
        | Core.ArrayType arr ->
            {
              inferred_type = Some arr.value.element;
              resolved_type =
                resolve_core_type state.type_env [] [] index.loc arr.value.element;
              metavar = metavar_of_type arr.value.element;
            }
        | Core.PointerType inner | Core.BoxType inner | Core.CellType inner ->
            {
              inferred_type = Some inner;
              resolved_type = resolve_core_type state.type_env [] [] index.loc inner;
              metavar = metavar_of_type inner;
            }
        | Core.VecType _ ->
            let ty = float_type index.loc in
            {
              inferred_type = Some ty;
              resolved_type = Some ResolvedFloat;
              metavar = metavar_of_type ty;
            }
        | _ -> unknown_expr_annotation)
    | None, Some (ResolvedArray (inner, _) | ResolvedPointer inner | ResolvedBox inner | ResolvedCell inner) ->
        let ty = core_type_of_resolved_ty index.loc inner in
        { inferred_type = Some ty; resolved_type = Some inner; metavar = metavar_of_type ty }
    | None, Some (ResolvedVec _) ->
        let ty = float_type index.loc in
        { inferred_type = Some ty; resolved_type = Some ResolvedFloat; metavar = metavar_of_type ty }
    | None, _ -> unknown_expr_annotation

  and infer_write_like state env ~pointee_target (write : Core.write) : expr_annotation =
    let target_ann = infer_expression state env write.value.target in
    let expected_value =
      match (pointee_target, target_ann.resolved_type) with
      | false, Some resolved -> Some resolved
      | true, Some (ResolvedPointer inner | ResolvedBox inner | ResolvedCell inner) ->
          Some inner
      | _ -> None
    in
    let value_ann = infer_expression state env ~expected_type:expected_value write.value.value in
    match (target_ann.inferred_type, target_ann.resolved_type, value_ann.inferred_type, value_ann.resolved_type) with
    | Some ty, resolved_type, _, _ ->
        { inferred_type = Some ty; resolved_type; metavar = metavar_of_type ty }
    | None, Some resolved_type, _, _ ->
        let ty = core_type_of_resolved_ty write.loc resolved_type in
        { inferred_type = Some ty; resolved_type = Some resolved_type; metavar = metavar_of_type ty }
    | None, None, Some ty, resolved_type ->
        { inferred_type = Some ty; resolved_type; metavar = metavar_of_type ty }
    | None, None, None, Some resolved_type ->
        let ty = core_type_of_resolved_ty write.loc resolved_type in
        { inferred_type = Some ty; resolved_type = Some resolved_type; metavar = metavar_of_type ty }
    | None, None, None, None -> unknown_expr_annotation

  let run (program : Core.parsed_program) =
    let type_env = type_env_of_program program.program in
    let state =
      {
        annotations = make_annotations ();
        diagnostics_rev = [];
        globals = String_map.empty;
        type_env;
      }
    in
    collect_globals state program.program;
    let env = [ state.globals ] in
    List.iter
      (fun (decl : Core.top_decl) ->
        match decl.value with
        | Core.FDecl fn -> (
            match fn.value.definition with
            | None -> ()
            | Some body ->
                let env = push_scope env in
        let return_expected =
          Option.bind fn.value.return_type (resolve_core_type state.type_env [] [] fn.loc)
        in
        let env =
          List.fold_left
            (fun env (param : Core.param) ->
              let binding =
                        binding_from_type ~is_mutable:false state.type_env param.value.ty
                      in
                      bind_current env param.value.name.value binding)
                    env fn.value.params.value.params
                in
                ignore
                  (infer_block state env ~result_expected:return_expected ~return_expected
                     body))
        | Core.Foreign foreign ->
            List.iter
              (fun (fn : Core.function_decl) ->
                match fn.value.definition with
                | None -> ()
                | Some body ->
                    let return_expected =
                      Option.bind fn.value.return_type
                        (resolve_core_type state.type_env [] [] fn.loc)
                    in
                    let env = push_scope env in
                    let env =
                      List.fold_left
                        (fun env (param : Core.param) ->
                          let binding =
                            binding_from_type ~is_mutable:false state.type_env param.value.ty
                          in
                          bind_current env param.value.name.value binding)
                        env fn.value.params.value.params
                    in
                    ignore
                      (infer_block state env ~result_expected:return_expected
                         ~return_expected body))
              foreign.value.decls
        | Core.VDecl binding ->
            Option.iter
              (fun init ->
                let expected_type =
                  resolve_core_type state.type_env [] [] binding.loc binding.value.ty
                in
                ignore (infer_expression state env ~expected_type init);
                ignore
                  (record_binding state
                     {
                       Core.value =
                         {
                           mut = binding.value.is_mutable;
                           ty = Some binding.value.ty;
                           name = binding.value.name;
                           init_expr = init;
                         };
                       loc = binding.loc;
                     }
                     {
                       inferred_type = Some binding.value.ty;
                       resolved_type =
                         resolve_core_type state.type_env [] [] binding.loc binding.value.ty;
                       metavar = metavar_of_type binding.value.ty;
                       is_mutable = binding.value.is_mutable;
                     }))
              binding.value.init_expr
        | Core.TDecl _ | Core.Import _ | Core.CImport _ -> ())
      program.program.value.decls;
    {
      program;
      annotations = state.annotations;
      diagnostics = List.rev state.diagnostics_rev;
    }
end
