open Haven_token.Token
open Haven_core

module Core = Core_ast

module String_map = Map.Make (String)

type type_class =
  | TypeClassUnknown
  | TypeClassNumeric
  | TypeClassBool
  | TypeClassFloat
  | TypeClassString
  | TypeClassVoid
  | TypeClassNil
  | TypeClassPointer
  | TypeClassBox
  | TypeClassArray
  | TypeClassVector
  | TypeClassMatrix
  | TypeClassFunction
  | TypeClassCustom of string

type constant_value =
  | ConstantInt of int
  | ConstantBool of bool
  | ConstantFloat of float
  | ConstantString of string
  | ConstantChar of char

type integer_shape = {
  exact_value : int option;
  minimum_bits : int option;
  signedness : signedness option;
}

type metavar = {
  classes : type_class list;
  constant : constant_value option;
  integer : integer_shape option;
}

type expr_annotation = {
  inferred_type : Core.haven_type option;
  metavar : metavar;
}

type binding_annotation = {
  inferred_type : Core.haven_type option;
  metavar : metavar;
  is_mutable : bool;
}

type diagnostic_level = Error | Warning

type diagnostic = {
  level : diagnostic_level;
  loc : Loc.t;
  message : string;
}

type annotations = {
  exprs : (string, expr_annotation) Hashtbl.t;
  bindings : (string, binding_annotation) Hashtbl.t;
}

type typing_result = {
  program : Core.parsed_program;
  annotations : annotations;
  diagnostics : diagnostic list;
}

type semantic_result = { diagnostics : diagnostic list }

let make_annotations () =
  {
    exprs = Hashtbl.create 256;
    bindings = Hashtbl.create 256;
  }

let node_id (loc : Loc.t) =
  let start_pos = loc.start_pos in
  let end_pos = loc.end_pos in
  Printf.sprintf "%s:%d:%d-%d:%d" start_pos.pos_fname start_pos.pos_lnum
    start_pos.pos_cnum end_pos.pos_lnum end_pos.pos_cnum

let expr_id (expr : Core.expression) = node_id expr.loc
let binding_id (binding : Core.let_stmt) = node_id binding.loc

let mk_type loc value : Core.haven_type = { Core.value; loc }
let mk_expr loc value : Core.expression = { Core.value; loc }
let mk_stmt loc value : Core.statement = { Core.value; loc }
let mk_block loc value : Core.block = { Core.value; loc }
let mk_identifier loc value : Core.identifier = { Core.value; loc }
let mk_pattern loc value : Core.match_pattern = { Core.value; loc }
let mk_arm loc value : Core.match_arm = { Core.value; loc }
let mk_literal loc value : Core.literal = { Core.value; loc }

let void_type loc = mk_type loc Core.VoidType
let float_type loc = mk_type loc Core.FloatType
let string_type loc = mk_type loc Core.StringType
let bool_type loc =
  mk_type loc (Core.NumericType { signedness = Unsigned; bits = 1 })

let numeric_type loc signedness bits =
  mk_type loc (Core.NumericType { signedness; bits })

let pointer_type loc inner = mk_type loc (Core.PointerType inner)
let box_type loc inner = mk_type loc (Core.BoxType inner)

let type_class_of_type (ty : Core.haven_type) =
  match ty.value with
  | Core.NumericType { signedness = Unsigned; bits = 1 } ->
      [ TypeClassBool; TypeClassNumeric ]
  | Core.NumericType _ -> [ TypeClassNumeric ]
  | Core.FloatType -> [ TypeClassFloat ]
  | Core.StringType -> [ TypeClassString ]
  | Core.VoidType -> [ TypeClassVoid ]
  | Core.PointerType _ -> [ TypeClassPointer ]
  | Core.BoxType _ -> [ TypeClassBox ]
  | Core.ArrayType _ -> [ TypeClassArray ]
  | Core.VecType _ -> [ TypeClassVector ]
  | Core.MatrixType _ -> [ TypeClassMatrix ]
  | Core.FunctionType _ -> [ TypeClassFunction ]
  | Core.CustomType custom -> [ TypeClassCustom custom.name.value ]
  | Core.CellType _ -> [ TypeClassPointer ]
  | Core.TemplatedType templ -> [ TypeClassCustom templ.value.outer.value ]

let unknown_metavar = { classes = [ TypeClassUnknown ]; constant = None; integer = None }

let metavar_of_type ?constant ?integer ty =
  { classes = type_class_of_type ty; constant; integer }

let unknown_expr_annotation = { inferred_type = None; metavar = unknown_metavar }

let exact_integer_bits value =
  if value >= 0 then
    let rec loop bits =
      if bits >= Sys.int_size - 1 then bits
      else if value <= ((1 lsl bits) - 1) then max 1 bits
      else loop (bits + 1)
    in
    loop 1
  else
    let rec loop bits =
      if bits >= Sys.int_size - 1 then bits
      else
        let lower_bound = -(1 lsl (bits - 1)) in
        let upper_bound = (1 lsl (bits - 1)) - 1 in
        if value >= lower_bound && value <= upper_bound then bits
        else loop (bits + 1)
    in
    loop 2

let smallest_integer_type loc value =
  if value >= 0 then numeric_type loc Unsigned (exact_integer_bits value)
  else numeric_type loc Signed (exact_integer_bits value)

let equal_list eq a b =
  let rec loop xs ys =
    match (xs, ys) with
    | [], [] -> true
    | x :: xs', y :: ys' -> eq x y && loop xs' ys'
    | _ -> false
  in
  loop a b

let rec equal_type (left : Core.haven_type) (right : Core.haven_type) =
  match (left.value, right.value) with
  | Core.NumericType a, Core.NumericType b ->
      a.signedness = b.signedness && a.bits = b.bits
  | Core.VecType a, Core.VecType b -> a = b
  | Core.MatrixType a, Core.MatrixType b -> a = b
  | Core.FloatType, Core.FloatType
  | Core.VoidType, Core.VoidType
  | Core.StringType, Core.StringType ->
      true
  | Core.CustomType a, Core.CustomType b -> String.equal a.name.value b.name.value
  | Core.CellType a, Core.CellType b
  | Core.PointerType a, Core.PointerType b
  | Core.BoxType a, Core.BoxType b ->
      equal_type a b
  | Core.FunctionType a, Core.FunctionType b ->
      a.value.vararg = b.value.vararg
      && equal_type a.value.return_type b.value.return_type
      && equal_list equal_type a.value.param_types b.value.param_types
  | Core.ArrayType a, Core.ArrayType b ->
      equal_type a.value.element b.value.element && a.value.count.value = b.value.count.value
  | Core.TemplatedType a, Core.TemplatedType b ->
      String.equal a.value.outer.value b.value.outer.value
      && equal_list equal_type a.value.inner b.value.inner
  | _ -> false

let is_boolean_type (ty : Core.haven_type) =
  match ty.value with
  | Core.NumericType { signedness = Unsigned; bits = 1 } -> true
  | _ -> false

let is_numeric_type (ty : Core.haven_type) =
  match ty.value with Core.NumericType _ -> true | _ -> false

let is_scalar_truthy_type (ty : Core.haven_type) =
  match ty.value with
  | Core.NumericType _
  | Core.FloatType
  | Core.PointerType _
  | Core.BoxType _
  | Core.CellType _ ->
      true
  | _ -> false

let wider_numeric_type loc (left : Core.haven_type) (right : Core.haven_type) =
  match (left.value, right.value) with
  | Core.FloatType, _ | _, Core.FloatType -> float_type loc
  | Core.NumericType a, Core.NumericType b ->
      let signedness =
        match (a.signedness, b.signedness) with
        | Signed, _ | _, Signed -> Signed
        | Unsigned, Unsigned -> Unsigned
      in
      numeric_type loc signedness (max a.bits b.bits)
  | _ -> left

let inferred_inner_type = function
  | Some ty -> Some ty
  | None -> None

let root_identifier_name (expr : Core.expression) =
  let rec loop (expr : Core.expression) =
    match expr.value with
    | Core.Identifier id -> Some id.value
    | Core.Index idx -> loop idx.value.target
    | Core.Field field -> loop field.value.target
    | Core.Load inner -> loop inner
    | _ -> None
  in
  loop expr

let is_lvalue (expr : Core.expression) =
  let rec loop (expr : Core.expression) =
    match expr.value with
    | Core.Identifier _ | Core.Index _ | Core.Field _ -> true
    | Core.Load inner -> loop inner
    | _ -> false
  in
  loop expr

module Typing = struct
  type env = binding_annotation String_map.t list

  type state = {
    annotations : annotations;
    mutable diagnostics_rev : diagnostic list;
    mutable globals : binding_annotation String_map.t;
  }

  let add_diagnostic state level loc message =
    state.diagnostics_rev <- { level; loc; message } :: state.diagnostics_rev

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

  let binding_from_type ?(is_mutable = false) ty =
    {
      inferred_type = Some ty;
      metavar = metavar_of_type ty;
      is_mutable;
    }

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
        { inferred_type = Some ty; metavar = metavar_of_type ty; is_mutable = false }
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
                metavar = metavar_of_type binding.value.ty;
                is_mutable = binding.value.is_mutable;
              }
        | Core.TDecl _ | Core.Import _ | Core.CImport _ -> ())
      program.value.decls

  let rec infer_block state env (block : Core.block) : expr_annotation =
    let env = push_scope env in
    let env =
      List.fold_left (infer_statement state) env block.value.statements
    in
    match block.value.result with
    | Some expr ->
        let expr_ann = infer_expression state env expr in
        {
          inferred_type = expr_ann.inferred_type;
          metavar = expr_ann.metavar;
        }
    | None ->
        let ty = void_type block.loc in
        { inferred_type = Some ty; metavar = metavar_of_type ty }

  and infer_statement state env (stmt : Core.statement) : env =
    match stmt.value with
    | Core.Expression expr ->
        ignore (infer_expression state env expr);
        env
    | Core.Return expr ->
        Option.iter
          (fun (expr : Core.expression) -> ignore (infer_expression state env expr))
          expr;
        env
    | Core.Defer expr ->
        ignore (infer_expression state env expr);
        env
    | Core.Break | Core.Continue -> env
    | Core.Let binding ->
        let init_ann = infer_expression state env binding.value.init_expr in
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
            metavar = binding_metavar;
            is_mutable = binding.value.mut;
          }
        in
        ignore (record_binding state binding binding_ann);
        bind_current env binding.value.name.value binding_ann
    | Core.Loop loop ->
        let loop_env = push_scope env in
        let loop_env = List.fold_left (infer_statement state) loop_env loop.value.init in
        ignore (infer_expression state loop_env loop.value.cond);
        ignore (infer_block state loop_env loop.value.body);
        ignore (List.fold_left (infer_statement state) loop_env loop.value.step);
        env

  and infer_expression state env (expr : Core.expression) : expr_annotation =
    let annotation =
      match expr.value with
      | Core.Identifier id -> (
          match lookup env id.value with
          | Some binding ->
              {
                inferred_type = binding.inferred_type;
                metavar = binding.metavar;
              }
          | None -> (
              match String_map.find_opt id.value state.globals with
              | Some binding ->
                  {
                    inferred_type = binding.inferred_type;
                    metavar = binding.metavar;
                  }
              | None ->
                  add_diagnostic state Error expr.loc
                    (Printf.sprintf "unknown identifier %s" id.value);
                  unknown_expr_annotation))
      | Core.Literal literal -> infer_literal state env expr.loc literal
      | Core.ToBool inner ->
          ignore (infer_expression state env inner);
          let ty = bool_type expr.loc in
          { inferred_type = Some ty; metavar = metavar_of_type ty }
      | Core.Unary unary -> infer_unary state env expr.loc unary
      | Core.Binary binary -> infer_binary state env expr.loc binary
      | Core.Block block -> infer_block state env block
      | Core.Initializer init ->
          List.iter
            (fun (expr : Core.expression) -> ignore (infer_expression state env expr))
            init.value.exprs;
          unknown_expr_annotation
      | Core.As cast ->
          ignore (infer_expression state env cast.value.inner);
          {
            inferred_type = Some cast.value.target_type;
            metavar = metavar_of_type cast.value.target_type;
          }
      | Core.SizeExpr inner ->
          ignore (infer_expression state env inner);
          let ty = numeric_type expr.loc Unsigned 64 in
          { inferred_type = Some ty; metavar = metavar_of_type ty }
      | Core.SizeType _ ->
          let ty = numeric_type expr.loc Unsigned 64 in
          { inferred_type = Some ty; metavar = metavar_of_type ty }
      | Core.Nil ->
          {
            inferred_type = None;
            metavar =
              {
                classes = [ TypeClassNil ];
                constant = None;
                integer = None;
              };
          }
      | Core.Match match_expr -> infer_match state env expr.loc match_expr
      | Core.BoxExpr inner -> (
          let inner_ann = infer_expression state env inner in
          match inner_ann.inferred_type with
          | Some inner_ty ->
              let ty = box_type expr.loc inner_ty in
              { inferred_type = Some ty; metavar = metavar_of_type ty }
          | None ->
              {
                inferred_type = None;
                metavar = { unknown_metavar with classes = [ TypeClassBox ] };
              })
      | Core.BoxType ty ->
          let ty = box_type expr.loc ty in
          { inferred_type = Some ty; metavar = metavar_of_type ty }
      | Core.Unbox inner -> (
          let inner_ann = infer_expression state env inner in
          match inner_ann.inferred_type with
          | Some ty -> (
              match ty.value with
              | Core.BoxType inner_ty | Core.PointerType inner_ty | Core.CellType inner_ty ->
                  { inferred_type = Some inner_ty; metavar = metavar_of_type inner_ty }
              | _ -> unknown_expr_annotation)
          | None -> unknown_expr_annotation)
      | Core.Ref inner -> (
          let inner_ann = infer_expression state env inner in
          match inner_ann.inferred_type with
          | Some inner_ty ->
              let ty = pointer_type expr.loc inner_ty in
              { inferred_type = Some ty; metavar = metavar_of_type ty }
          | None ->
              {
                inferred_type = None;
                metavar = { unknown_metavar with classes = [ TypeClassPointer ] };
              })
      | Core.Load inner -> (
          let inner_ann = infer_expression state env inner in
          match inner_ann.inferred_type with
          | Some ty -> (
              match ty.value with
              | Core.PointerType inner_ty | Core.BoxType inner_ty | Core.CellType inner_ty ->
                  { inferred_type = Some inner_ty; metavar = metavar_of_type inner_ty }
              | _ -> unknown_expr_annotation)
          | None -> unknown_expr_annotation)
      | Core.Call call -> infer_call state env expr.loc call
      | Core.Index index -> infer_index state env expr.loc index
      | Core.Field field ->
          ignore (infer_expression state env field.value.target);
          unknown_expr_annotation
      | Core.Assign write ->
          infer_write_like state env write
      | Core.Mutate write ->
          infer_write_like state env write
    in
    record_expr state expr annotation

  and infer_literal state env loc (literal : Core.literal) : expr_annotation =
    match literal.value with
    | Core.Integer value ->
        let ty = smallest_integer_type loc value in
        {
          inferred_type = Some ty;
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
        let element_anns = List.map (infer_expression state env) vec.value.elements in
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
              infer_literal state env row.loc (mk_literal row.loc (Core.Vector row)))
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
          metavar =
            {
              classes = [ TypeClassMatrix ];
              constant = None;
              integer = None;
            };
        }
    | Core.Enum enum ->
        let ty =
          if enum.value.types = [] then
            mk_type loc
              (Core.CustomType { name = mk_identifier enum.value.enum_name.loc enum.value.enum_name.value })
          else
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
        { inferred_type = Some ty; metavar = metavar_of_type ty }

  and infer_unary state env loc (unary : Core.unary) : expr_annotation =
    let inner_ann = infer_expression state env unary.value.inner in
    match unary.value.op with
    | Core.Not ->
        let ty = bool_type loc in
        { inferred_type = Some ty; metavar = metavar_of_type ty }
    | Core.Negate | Core.Complement -> (
        match inner_ann.inferred_type with
        | Some ty -> { inferred_type = Some ty; metavar = metavar_of_type ty }
        | None ->
            {
              inferred_type = None;
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
        { inferred_type = Some ty; metavar = metavar_of_type ty }
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
            { inferred_type = Some ty; metavar = metavar_of_type ty }
        | Some left_ty, Some _ ->
            { inferred_type = Some left_ty; metavar = metavar_of_type left_ty }
        | Some ty, None | None, Some ty ->
            { inferred_type = Some ty; metavar = metavar_of_type ty }
        | None, None -> (
            match
              ( Option.bind left_ann.metavar.integer (fun i -> i.exact_value),
                Option.bind right_ann.metavar.integer (fun i -> i.exact_value) )
            with
            | Some left_value, Some right_value ->
                let ty = smallest_integer_type loc (max left_value right_value) in
                { inferred_type = Some ty; metavar = metavar_of_type ty }
            | _ ->
                {
                  inferred_type = None;
                  metavar = { unknown_metavar with classes = [ TypeClassNumeric ] };
                }))

  and infer_match state env loc (match_expr : Core.match_expr) : expr_annotation =
    ignore (infer_expression state env match_expr.value.expr);
    let arm_types =
      List.map
        (fun (arm : Core.match_arm) ->
          let env = infer_pattern_bindings state env match_expr.value.expr arm.value.pattern in
          let ann = infer_expression state env arm.value.expr in
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
      metavar =
        (match inferred_type with
        | Some ty -> metavar_of_type ty
        | None -> unknown_metavar);
    }

  and infer_pattern_bindings _state env (scrutinee : Core.expression)
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
                  {
                    inferred_type = None;
                    metavar = unknown_metavar;
                    is_mutable = false;
                  }
                in
                ignore scrutinee;
                bind_current env id.value binding_ann)
          initial enum.value.binding

  and infer_call state env _loc (call : Core.call) : expr_annotation =
    let target_ann = infer_expression state env call.value.target in
    List.iter
      (fun (expr : Core.expression) -> ignore (infer_expression state env expr))
      call.value.params;
    match target_ann.inferred_type with
    | Some ty -> (
        match ty.value with
        | Core.FunctionType fn ->
            {
              inferred_type = Some fn.value.return_type;
              metavar = metavar_of_type fn.value.return_type;
            }
        | _ -> unknown_expr_annotation)
    | None -> unknown_expr_annotation

  and infer_index state env _loc (index : Core.index) : expr_annotation =
    let target_ann = infer_expression state env index.value.target in
    ignore (infer_expression state env index.value.index);
    match target_ann.inferred_type with
    | Some ty -> (
        match ty.value with
        | Core.ArrayType arr ->
            { inferred_type = Some arr.value.element; metavar = metavar_of_type arr.value.element }
        | Core.PointerType inner | Core.BoxType inner | Core.CellType inner ->
            { inferred_type = Some inner; metavar = metavar_of_type inner }
        | Core.VecType _ ->
            let ty = float_type index.loc in
            { inferred_type = Some ty; metavar = metavar_of_type ty }
        | _ -> unknown_expr_annotation)
    | None -> unknown_expr_annotation

  and infer_write_like state env (write : Core.write) : expr_annotation =
    let target_ann = infer_expression state env write.value.target in
    let value_ann = infer_expression state env write.value.value in
    match (target_ann.inferred_type, value_ann.inferred_type) with
    | Some ty, _ -> { inferred_type = Some ty; metavar = metavar_of_type ty }
    | None, Some ty -> { inferred_type = Some ty; metavar = metavar_of_type ty }
    | None, None -> unknown_expr_annotation

  let run (program : Core.parsed_program) =
    let state =
      {
        annotations = make_annotations ();
        diagnostics_rev = [];
        globals = String_map.empty;
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
                let env =
                  List.fold_left
                    (fun env (param : Core.param) ->
                      let binding =
                        binding_from_type ~is_mutable:false param.value.ty
                      in
                      bind_current env param.value.name.value binding)
                    env fn.value.params.value.params
                in
                ignore (infer_block state env body))
        | Core.Foreign foreign ->
            List.iter
              (fun (fn : Core.function_decl) ->
                match fn.value.definition with
                | None -> ()
                | Some body ->
                    let env = push_scope env in
                    let env =
                      List.fold_left
                        (fun env (param : Core.param) ->
                          let binding =
                            binding_from_type ~is_mutable:false param.value.ty
                          in
                          bind_current env param.value.name.value binding)
                        env fn.value.params.value.params
                    in
                    ignore (infer_block state env body))
              foreign.value.decls
        | Core.VDecl binding ->
            Option.iter
              (fun init ->
                ignore (infer_expression state env init);
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

module Semantic = struct
  type env = binding_annotation String_map.t list

  type state = {
    typed : typing_result;
    mutable diagnostics_rev : diagnostic list;
  }

  let add_diagnostic state level loc message =
    state.diagnostics_rev <- { level; loc; message } :: state.diagnostics_rev

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
              metavar = metavar_of_type (Typing.function_type_of_decl fn);
              is_mutable = false;
            }
            scope
      | Core.VDecl binding ->
          String_map.add binding.value.name.value
            {
              inferred_type = Some binding.value.ty;
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

  let rec check_block state env loop_depth (block : Core.block) =
    let env = push_scope env in
    let env =
      List.fold_left
        (fun env (stmt : Core.statement) -> check_statement state env loop_depth stmt)
        env
        block.value.statements
    in
    Option.iter (check_expression state env loop_depth) block.value.result;
    env

  and check_statement state env loop_depth (stmt : Core.statement) =
    match stmt.value with
    | Core.Expression expr ->
        check_expression state env loop_depth expr;
        env
    | Core.Return expr ->
        Option.iter (check_expression state env loop_depth) expr;
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
              check_statement state env (loop_depth + 1) stmt)
            env loop.value.init
        in
        check_expression state env (loop_depth + 1) loop.value.cond;
        ignore (check_block state env (loop_depth + 1) loop.value.body);
        ignore
          (List.fold_left
             (fun env (stmt : Core.statement) ->
               check_statement state env (loop_depth + 1) stmt)
             env loop.value.step);
        env

  and check_expression state env loop_depth (expr : Core.expression) =
    let check_scalar_truthy inner =
      match expr_annotation state inner with
      | Some { inferred_type = Some ty; _ } ->
          if not (is_scalar_truthy_type ty || is_boolean_type ty) then
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
        check_expression state env loop_depth binary.value.right
    | Core.Block block ->
        ignore (check_block state env loop_depth block)
    | Core.Initializer init ->
        List.iter (check_expression state env loop_depth) init.value.exprs
    | Core.As cast -> check_expression state env loop_depth cast.value.inner
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
        if (not has_default) && (not (is_bool_scrutinee && bool_match_is_exhaustive match_expr))
        then
          add_diagnostic state Error expr.loc
            "match expression is not obviously exhaustive";
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
                              metavar = unknown_metavar;
                              is_mutable = false;
                            })
                    env enum.value.binding
              | Core.PatternDefault | Core.PatternLiteral _ -> env
            in
            check_expression state env loop_depth arm.value.expr)
          match_expr.value.arms
    | Core.BoxExpr inner | Core.Unbox inner | Core.Ref inner | Core.Load inner ->
        check_expression state env loop_depth inner
    | Core.BoxType _ -> ()
    | Core.Call call ->
        check_expression state env loop_depth call.value.target;
        List.iter (check_expression state env loop_depth) call.value.params
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
          (root_identifier_name write.value.target)
    | Core.Mutate write ->
        check_expression state env loop_depth write.value.target;
        check_expression state env loop_depth write.value.value;
        if not (is_lvalue write.value.target) then
          add_diagnostic state Error expr.loc
            "mutation target must be assignable";
        (match expr_annotation state write.value.target with
        | Some { inferred_type = Some ty; _ } -> (
            match ty.value with
            | Core.PointerType _ | Core.BoxType _ | Core.CellType _ -> ()
            | _ ->
                add_diagnostic state Error expr.loc
                  "mutation requires a pointer-like target")
        | _ -> ())

  let run typed =
    let state = { typed; diagnostics_rev = [] } in
    let env = [ initial_scope typed ] in
    List.iter
      (fun (decl : Core.top_decl) ->
        match decl.value with
        | Core.FDecl fn -> (
            match fn.value.definition with
            | None -> ()
            | Some body ->
                let env = push_scope env in
                let env =
                  List.fold_left
                    (fun env (param : Core.param) ->
                      bind_current env param.value.name.value
                        {
                          inferred_type = Some param.value.ty;
                          metavar = metavar_of_type param.value.ty;
                          is_mutable = false;
                        })
                    env fn.value.params.value.params
                in
                ignore (check_block state env 0 body))
        | Core.Foreign foreign ->
            List.iter
              (fun (fn : Core.function_decl) ->
                match fn.value.definition with
                | None -> ()
                | Some body ->
                    let env = push_scope env in
                    let env =
                      List.fold_left
                        (fun env (param : Core.param) ->
                          bind_current env param.value.name.value
                            {
                              inferred_type = Some param.value.ty;
                              metavar = metavar_of_type param.value.ty;
                              is_mutable = false;
                            })
                        env fn.value.params.value.params
                    in
                    ignore (check_block state env 0 body))
              foreign.value.decls
        | Core.VDecl binding ->
            Option.iter (check_expression state env 0) binding.value.init_expr
        | Core.TDecl _ | Core.Import _ | Core.CImport _ -> ())
      typed.program.program.value.decls;
    { diagnostics = List.rev state.diagnostics_rev }
end

module Cleanup = struct
  let expr_annotation typed expr =
    Hashtbl.find_opt typed.annotations.exprs (expr_id expr)

  let rec clean_expression typed (expr : Core.expression) : Core.expression =
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
      | Core.Assign write ->
          Core.Assign
            {
              write with
              value =
                {
                  Core.target = clean_expression typed write.value.target;
                  value = clean_expression typed write.value.value;
                };
            }
      | Core.Mutate write ->
          Core.Mutate
            {
              write with
              value =
                {
                  Core.target = clean_expression typed write.value.target;
                  value = clean_expression typed write.value.value;
                };
            }
      | (Core.Identifier _ | Core.Literal _ | Core.SizeType _ | Core.Nil | Core.BoxType _) as
        value ->
          value
    in
    { expr with value = cleaned_value }

  and clean_statement typed (stmt : Core.statement) : Core.statement =
    let value =
      match stmt.value with
      | Core.Expression expr -> Core.Expression (clean_expression typed expr)
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

  let run typed =
    {
      Core.program =
        {
          typed.program.program with
          value =
            { Core.decls = List.map (clean_decl typed) typed.program.program.value.decls };
        };
    }
end

module Pipeline = struct
  type result = {
    core : Core.parsed_program;
    typing : typing_result;
    semantic : semantic_result;
    cleaned : Core.parsed_program;
  }

  let run_core core =
    let typing = Typing.run core in
    let semantic = Semantic.run typing in
    let cleaned = Cleanup.run typing in
    { core; typing; semantic; cleaned }

  let run_cst parsed = run_core (Convert.core_of_cst parsed)
end
