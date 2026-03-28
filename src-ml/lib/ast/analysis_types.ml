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

type diagnostic_category = TypeCheck | Semantic | Cleanup

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

let resolved_is_pointerish = function
  | ResolvedPointer _ | ResolvedBox _ | ResolvedCell _ | ResolvedString -> true
  | _ -> false

let resolved_compatible actual expected =
  equal_resolved_type actual expected
  || (resolved_is_numeric actual && resolved_is_numeric expected)

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
      numeric_type loc signedness (max a.bits b.bits)
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

let resolved_deref_once = function
  | ResolvedPointer inner | ResolvedBox inner | ResolvedCell inner -> Some inner
  | _ -> None

let vector_field_index field =
  match field with
  | "x" | "r" -> Some 0
  | "y" | "g" -> Some 1
  | "z" | "b" -> Some 2
  | "w" | "a" -> Some 3
  | _ -> (
      try Some (int_of_string field) with Failure _ -> None)

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
