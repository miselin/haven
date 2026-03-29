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

type resolved_ty =
  | ResolvedInt of signedness * int
  | ResolvedFloat
  | ResolvedString
  | ResolvedVoid
  | ResolvedPointer of resolved_ty
  | ResolvedBox of resolved_ty
  | ResolvedCell of resolved_ty
  | ResolvedArray of resolved_ty * int
  | ResolvedVec of vec_type
  | ResolvedMatrix of mat_type
  | ResolvedFunction of resolved_ty list * resolved_ty * bool
  | ResolvedNamed of string * resolved_ty list
  | ResolvedGenericParam of string

type type_decl_info =
  | TypeAlias of Core.haven_type
  | TypeStruct of Core.struct_decl
  | TypeEnum of Core.enum_decl
  | TypeForward

type type_env = type_decl_info String_map.t

type expr_annotation = {
  inferred_type : Core.haven_type option;
  resolved_type : resolved_ty option;
  metavar : metavar;
}

type binding_annotation = {
  inferred_type : Core.haven_type option;
  resolved_type : resolved_ty option;
  metavar : metavar;
  is_mutable : bool;
}

type diagnostic_category =
  | Import
  | TypeCheck
  | TypeVerify
  | Semantic
  | Purity
  | Cleanup
  | Ownership

type diagnostic_level = Error | Warning

type diagnostic = {
  category : diagnostic_category;
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
type verify_result = { diagnostics : diagnostic list }
type purity_result = { diagnostics : diagnostic list }

type ownership_anchor =
  | AfterExpr of string
  | BeforeExpr of string
  | BeforeStmt of string
  | OnBlockExit of string
  | OnLoopExit of string
  | OnFunctionExit of string
  | OnGlobalInit of string

type ownership_action_kind = Retain | Release

type ownership_reason =
  | BindingInit
  | CallArg
  | ReturnValue
  | AssignValue
  | AssignOverwrite
  | MutateValue
  | MutateOverwrite
  | ScopeExit
  | LoopExit
  | FunctionExit

type ownership_subject =
  | OwnershipExpr of string * string option
  | OwnershipBinding of string
  | OwnershipParam of string
  | OwnershipTarget of string * string option

type ownership_action = {
  anchor : ownership_anchor;
  kind : ownership_action_kind;
  reason : ownership_reason;
  subject : ownership_subject;
  loc : Loc.t;
  resolved_type : resolved_ty option;
}

type ownership_index = {
  after_expr : (string, ownership_action list) Hashtbl.t;
  before_expr : (string, ownership_action list) Hashtbl.t;
  before_stmt : (string, ownership_action list) Hashtbl.t;
  on_block_exit : (string, ownership_action list) Hashtbl.t;
  on_loop_exit : (string, ownership_action list) Hashtbl.t;
  on_function_exit : (string, ownership_action list) Hashtbl.t;
  on_global_init : (string, ownership_action list) Hashtbl.t;
}

type ownership_result = {
  actions : ownership_action list;
  index : ownership_index;
  diagnostics : diagnostic list;
}

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
let statement_id (stmt : Core.statement) = node_id stmt.loc
let block_id (block : Core.block) = node_id block.loc
let function_id (fn : Core.function_decl) = node_id fn.loc

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

let unknown_expr_annotation =
  { inferred_type = None; resolved_type = None; metavar = unknown_metavar }

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

let rec zip_lists left right =
  match (left, right) with
  | [], [] -> Some []
  | left :: left_rest, right :: right_rest -> (
      match zip_lists left_rest right_rest with
      | Some rest -> Some ((left, right) :: rest)
      | None -> None)
  | _ -> None

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

let rec equal_resolved_type left right =
  match (left, right) with
  | ResolvedInt (ls, lb), ResolvedInt (rs, rb) -> ls = rs && lb = rb
  | ResolvedFloat, ResolvedFloat
  | ResolvedString, ResolvedString
  | ResolvedVoid, ResolvedVoid ->
      true
  | ResolvedPointer left, ResolvedPointer right
  | ResolvedBox left, ResolvedBox right
  | ResolvedCell left, ResolvedCell right ->
      equal_resolved_type left right
  | ResolvedArray (le, lc), ResolvedArray (re, rc) ->
      lc = rc && equal_resolved_type le re
  | ResolvedVec left, ResolvedVec right -> left = right
  | ResolvedMatrix left, ResolvedMatrix right -> left = right
  | ResolvedFunction (lp, lr, lv), ResolvedFunction (rp, rr, rv) ->
      lv = rv && equal_resolved_type lr rr
      && equal_list equal_resolved_type lp rp
  | ResolvedNamed (ln, la), ResolvedNamed (rn, ra) ->
      String.equal ln rn && equal_list equal_resolved_type la ra
  | ResolvedGenericParam left, ResolvedGenericParam right -> String.equal left right
  | _ -> false

let rec core_type_of_resolved_ty loc = function
  | ResolvedInt (signedness, bits) -> numeric_type loc signedness bits
  | ResolvedFloat -> float_type loc
  | ResolvedString -> string_type loc
  | ResolvedVoid -> void_type loc
  | ResolvedPointer inner -> pointer_type loc (core_type_of_resolved_ty loc inner)
  | ResolvedBox inner -> box_type loc (core_type_of_resolved_ty loc inner)
  | ResolvedCell inner -> mk_type loc (Core.CellType (core_type_of_resolved_ty loc inner))
  | ResolvedArray (inner, count) ->
      mk_type loc
        (Core.ArrayType
           {
             Core.value =
               {
                 element = core_type_of_resolved_ty loc inner;
                 count = mk_literal loc (Core.Integer count);
               };
             loc;
           })
  | ResolvedVec vec -> mk_type loc (Core.VecType vec)
  | ResolvedMatrix mat -> mk_type loc (Core.MatrixType mat)
  | ResolvedFunction (params, ret, vararg) ->
      mk_type loc
        (Core.FunctionType
           {
             Core.value =
               {
                 param_types = List.map (core_type_of_resolved_ty loc) params;
                 return_type = core_type_of_resolved_ty loc ret;
                 vararg;
               };
             loc;
           })
  | ResolvedNamed (name, []) ->
      mk_type loc (Core.CustomType { name = mk_identifier loc name })
  | ResolvedNamed (name, args) ->
      mk_type loc
        (Core.TemplatedType
           {
             Core.value =
               {
                 outer = mk_identifier loc name;
                 inner = List.map (core_type_of_resolved_ty loc) args;
               };
             loc;
           })
  | ResolvedGenericParam name ->
      mk_type loc (Core.CustomType { name = mk_identifier loc name })

let resolved_is_bool = function ResolvedInt (Unsigned, 1) -> true | _ -> false

let resolved_is_numeric = function ResolvedInt _ | ResolvedFloat -> true | _ -> false

let resolved_is_vector = function ResolvedVec _ -> true | _ -> false
let resolved_is_matrix = function ResolvedMatrix _ -> true | _ -> false

let resolved_is_pointerish = function
  | ResolvedPointer _ | ResolvedBox _ | ResolvedCell _ | ResolvedString -> true
  | _ -> false

let resolved_value_type = function ResolvedCell inner -> inner | ty -> ty

let combine_matrix_kind (left : mat_type) (right : mat_type) =
  match (left.kind, right.kind) with
  | FloatMat, FloatMat -> FloatMat
  | _ -> GenericMat

(* TODO: The language needs a story for dimension-polymorphic vectors/matrices.
   Hard-coding every operator and function per concrete size will make useful
   linear algebra libraries impractical. *)
let resolved_arithmetic_binary_result op left right =
  match (op, left, right) with
  | ( Core.Add | Core.Subtract | Core.Multiply | Core.Divide | Core.Modulo ),
    ResolvedVec left,
    ResolvedVec right
    when left = right ->
      Some (ResolvedVec left)
  | (Core.Multiply | Core.Divide | Core.Modulo), ResolvedVec vec, ResolvedFloat
  | (Core.Multiply | Core.Divide | Core.Modulo), ResolvedFloat, ResolvedVec vec ->
      Some (ResolvedVec vec)
  | (Core.Add | Core.Subtract), ResolvedMatrix left, ResolvedMatrix right
    when left.rows = right.rows && left.columns = right.columns ->
      Some
        (ResolvedMatrix
           { kind = combine_matrix_kind left right; rows = left.rows; columns = left.columns })
  | Core.Multiply, ResolvedMatrix left, ResolvedMatrix right
    when left.columns = right.rows ->
      Some
        (ResolvedMatrix
           { kind = combine_matrix_kind left right; rows = left.rows; columns = right.columns })
  | Core.Multiply, ResolvedMatrix mat, ResolvedFloat
  | Core.Multiply, ResolvedFloat, ResolvedMatrix mat ->
      Some (ResolvedMatrix mat)
  | Core.Multiply, ResolvedVec (vec : vec_type), ResolvedMatrix mat
    when vec.dimension = mat.rows ->
      Some (ResolvedVec { kind = vec.kind; dimension = mat.columns })
  | _ -> None

let rec resolved_compatible actual expected =
  equal_resolved_type actual expected
  ||
  match (actual, expected) with
  | actual, expected when resolved_is_numeric actual && resolved_is_numeric expected -> true
  | ResolvedPointer actual, ResolvedPointer expected
  | ResolvedBox actual, ResolvedBox expected
  | ResolvedCell actual, ResolvedCell expected ->
      resolved_compatible actual expected
  | ResolvedArray (actual, actual_count), ResolvedArray (expected, expected_count) ->
      actual_count = expected_count && resolved_compatible actual expected
  | ResolvedFunction (actual_params, actual_ret, actual_vararg),
    ResolvedFunction (expected_params, expected_ret, expected_vararg) ->
      actual_vararg = expected_vararg
      && equal_list resolved_compatible actual_params expected_params
      && resolved_compatible actual_ret expected_ret
  | ResolvedNamed (actual_name, []), ResolvedNamed (expected_name, _)
  | ResolvedNamed (actual_name, _), ResolvedNamed (expected_name, [])
    when String.equal actual_name expected_name ->
      true
  | ResolvedNamed (actual_name, actual_args), ResolvedNamed (expected_name, expected_args)
    when String.equal actual_name expected_name ->
      equal_list resolved_compatible actual_args expected_args
  | _ -> false

let resolved_can_cast source target =
  resolved_compatible source target
  ||
  match (resolved_value_type source, target) with
  | source, target when resolved_is_numeric source && resolved_is_numeric target -> true
  | source, target when resolved_is_pointerish source && resolved_is_pointerish target -> true
  | _ -> false

let coerce_annotation_to_expected loc expected (annotation : expr_annotation) =
  let coerced_resolved =
    match annotation.resolved_type with
    | Some actual when equal_resolved_type actual expected -> Some expected
    | Some (ResolvedCell actual) when equal_resolved_type actual expected -> Some expected
    | None when List.mem TypeClassNil annotation.metavar.classes && resolved_is_pointerish expected
      ->
        Some expected
    | _ -> None
  in
  match coerced_resolved with
  | Some resolved ->
      let inferred_type = core_type_of_resolved_ty loc resolved in
      {
        inferred_type = Some inferred_type;
        resolved_type = Some resolved;
        metavar = { annotation.metavar with classes = type_class_of_type inferred_type };
      }
  | None -> annotation

let type_env_of_program (program : Core.program) =
  List.fold_left
    (fun env (decl : Core.top_decl) ->
      match decl.value with
      | Core.TDecl type_decl -> (
          match type_decl.value.data with
          | Core.TypeDeclAlias ty ->
              String_map.add type_decl.value.name.value (TypeAlias ty) env
          | Core.TypeDeclStruct struct_decl ->
              String_map.add type_decl.value.name.value (TypeStruct struct_decl) env
          | Core.TypeDeclEnum enum_decl ->
              String_map.add type_decl.value.name.value (TypeEnum enum_decl) env
          | Core.TypeDeclForward ->
              String_map.add type_decl.value.name.value TypeForward env)
      | _ -> env)
    String_map.empty program.value.decls

let resolve_array_count (lit : Core.literal) =
  match lit.value with Core.Integer count when count >= 0 -> Some count | _ -> None

let rec substitute_resolved_ty subst = function
  | ResolvedGenericParam name -> (
      match List.assoc_opt name subst with Some ty -> ty | None -> ResolvedGenericParam name)
  | ResolvedPointer inner -> ResolvedPointer (substitute_resolved_ty subst inner)
  | ResolvedBox inner -> ResolvedBox (substitute_resolved_ty subst inner)
  | ResolvedCell inner -> ResolvedCell (substitute_resolved_ty subst inner)
  | ResolvedArray (inner, count) ->
      ResolvedArray (substitute_resolved_ty subst inner, count)
  | ResolvedFunction (params, ret, vararg) ->
      ResolvedFunction
        ( List.map (substitute_resolved_ty subst) params,
          substitute_resolved_ty subst ret,
          vararg )
  | ResolvedNamed (name, args) ->
      ResolvedNamed (name, List.map (substitute_resolved_ty subst) args)
  | ty -> ty

let rec resolve_named_type type_env active subst loc name args =
  match List.assoc_opt name subst with
  | Some ty when args = [] -> Some ty
  | Some _ -> None
  | None -> (
      if List.mem name active then Some (ResolvedNamed (name, args))
      else
        match String_map.find_opt name type_env with
        | Some (TypeAlias ty) when args = [] ->
            resolve_core_type type_env (name :: active) subst loc ty
        | Some (TypeStruct _) | Some (TypeEnum _) | Some TypeForward ->
            Some (ResolvedNamed (name, args))
        | Some (TypeAlias _) -> None
        | None -> None)

and resolve_core_type type_env active subst loc (ty : Core.haven_type) =
  match ty.value with
  | Core.NumericType num -> Some (ResolvedInt (num.signedness, num.bits))
  | Core.FloatType -> Some ResolvedFloat
  | Core.StringType -> Some ResolvedString
  | Core.VoidType -> Some ResolvedVoid
  | Core.VecType vec -> Some (ResolvedVec vec)
  | Core.MatrixType mat -> Some (ResolvedMatrix mat)
  | Core.PointerType inner ->
      Option.map (fun inner -> ResolvedPointer inner)
        (resolve_core_type type_env active subst loc inner)
  | Core.BoxType inner ->
      Option.map (fun inner -> ResolvedBox inner)
        (resolve_core_type type_env active subst loc inner)
  | Core.CellType inner ->
      Option.map (fun inner -> ResolvedCell inner)
        (resolve_core_type type_env active subst loc inner)
  | Core.ArrayType arr -> (
      match
        ( resolve_core_type type_env active subst loc arr.value.element,
          resolve_array_count arr.value.count )
      with
      | Some element, Some count -> Some (ResolvedArray (element, count))
      | _ -> None)
  | Core.FunctionType fn -> (
      let params =
        List.map (resolve_core_type type_env active subst loc) fn.value.param_types
      in
      let rec collect = function
        | [] -> Some []
        | Some ty :: rest -> Option.map (fun rest -> ty :: rest) (collect rest)
        | None :: _ -> None
      in
      match
        ( collect params,
          resolve_core_type type_env active subst loc fn.value.return_type )
      with
      | Some params, Some ret ->
          Some (ResolvedFunction (params, ret, fn.value.vararg))
      | _ -> None)
  | Core.CustomType custom ->
      resolve_named_type type_env active subst loc custom.name.value []
  | Core.TemplatedType templ -> (
      let args =
        List.map (resolve_core_type type_env active subst loc) templ.value.inner
      in
      let rec collect = function
        | [] -> Some []
        | Some ty :: rest -> Option.map (fun rest -> ty :: rest) (collect rest)
        | None :: _ -> None
      in
      match collect args with
      | Some args ->
          resolve_named_type type_env active subst loc templ.value.outer.value args
      | None -> None)

let wider_numeric_type loc (left : Core.haven_type) (right : Core.haven_type) =
  match (left.value, right.value) with
  | Core.FloatType, _ | _, Core.FloatType -> float_type loc
  | Core.NumericType a, Core.NumericType b ->
      let signedness =
        match (a.signedness, b.signedness) with
        | Signed, _ | _, Signed -> Signed
        | Unsigned, Unsigned -> Unsigned
      in
      numeric_type loc signedness (max 32 (max a.bits b.bits))
  | _ -> left

let lookup_named_type type_env name = String_map.find_opt name type_env

let rec lookup_enum_decl type_env loc ty =
  match ty with
  | ResolvedNamed (name, args) -> (
      match lookup_named_type type_env name with
      | Some (TypeEnum decl) ->
          Option.map
            (fun subst -> (decl, subst))
            (zip_lists (List.map (fun (id : Core.identifier) -> id.value) decl.value.generics) args)
      | Some (TypeAlias alias) -> (
          match resolve_core_type type_env [] [] loc alias with
          | Some ty -> lookup_enum_decl type_env loc ty
          | None -> None)
      | _ -> None)
  | _ -> None

let lookup_enum_variant type_env loc ty variant_name =
  match lookup_enum_decl type_env loc ty with
  | Some (decl, subst) ->
      List.find_opt
        (fun (variant : Core.enum_variant) ->
          String.equal variant.value.name.value variant_name)
        decl.value.variants
      |> Option.map (fun (variant : Core.enum_variant) ->
             let inner_ty =
               Option.bind variant.value.inner_ty
                 (resolve_core_type type_env [] subst loc)
             in
             (variant, inner_ty))
  | None -> None

let rec lookup_struct_fields type_env loc ty =
  match ty with
  | ResolvedNamed (name, _args) -> (
      match lookup_named_type type_env name with
      | Some (TypeStruct decl) ->
          let resolve_field (field : Core.struct_field) =
            Option.map
              (fun ty -> (field.value.name.value, ty))
              (resolve_core_type type_env [] [] loc field.value.ty)
          in
          let fields = List.map resolve_field decl.value.fields in
          let rec collect = function
            | [] -> Some []
            | Some field :: rest ->
                Option.map (fun rest -> field :: rest) (collect rest)
            | None :: _ -> None
          in
          collect fields
      | Some (TypeAlias alias) -> (
          match resolve_core_type type_env [] [] loc alias with
          | Some ty -> lookup_struct_fields type_env loc ty
          | None -> None)
      | _ -> None)
  | _ -> None

let rec resolved_contains_box_ownership type_env active loc ty =
  match ty with
  | ResolvedBox _ -> true
  | ResolvedPointer _
  | ResolvedCell _
  | ResolvedString
  | ResolvedInt _
  | ResolvedFloat
  | ResolvedVoid
  | ResolvedVec _
  | ResolvedMatrix _
  | ResolvedFunction _
  | ResolvedGenericParam _ ->
      false
  | ResolvedArray (inner, _) ->
      resolved_contains_box_ownership type_env active loc inner
  | ResolvedNamed (name, _args) as resolved -> (
      if List.mem name active then false
      else
        match lookup_named_type type_env name with
        | Some (TypeAlias alias) -> (
            match resolve_core_type type_env [] [] loc alias with
            | Some alias_ty ->
                resolved_contains_box_ownership type_env (name :: active) loc alias_ty
            | None -> false)
        | Some (TypeStruct decl) ->
            List.exists
              (fun (field : Core.struct_field) ->
                match resolve_core_type type_env [] [] field.loc field.value.ty with
                | Some field_ty ->
                    resolved_contains_box_ownership type_env (name :: active) field.loc
                      field_ty
                | None -> false)
              decl.value.fields
        | Some (TypeEnum decl) -> (
            match lookup_enum_decl type_env loc resolved with
            | Some (_, subst) ->
                List.exists
                  (fun (variant : Core.enum_variant) ->
                    match variant.value.inner_ty with
                    | Some inner_ty -> (
                        match resolve_core_type type_env [] subst variant.loc inner_ty with
                        | Some variant_ty ->
                            resolved_contains_box_ownership type_env (name :: active)
                              variant.loc variant_ty
                        | None -> false)
                    | None -> false)
                  decl.value.variants
            | None -> false)
        | Some TypeForward | None -> false)

let resolved_deref_once = function
  | ResolvedPointer inner | ResolvedBox inner | ResolvedCell inner -> Some inner
  | _ -> None

let vector_field_index field =
  match field with
  | "x" | "r" -> Some 0
  | "y" | "g" | "t" -> Some 1
  | "z" | "b" | "p" -> Some 2
  | "w" | "a" | "q" -> Some 3
  | "s" -> Some 0
  | _ -> (
      try Some (int_of_string field) with Failure _ -> None)

let root_identifier_name (expr : Core.expression) =
  let rec loop (expr : Core.expression) =
    match expr.value with
    | Core.Identifier id -> Some id.value
    | Core.Index idx -> loop idx.value.target
    | Core.Field field -> loop field.value.target
    | Core.Unbox inner -> loop inner
    | Core.Load inner -> loop inner
    | _ -> None
  in
  loop expr

let is_lvalue (expr : Core.expression) =
  let rec loop (expr : Core.expression) =
    match expr.value with
    | Core.Identifier _ | Core.Index _ | Core.Field _ -> true
    | Core.Unbox inner -> loop inner
    | Core.Load inner -> loop inner
    | _ -> false
  in
  loop expr
