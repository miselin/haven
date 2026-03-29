open Haven_core

module Core = Core_ast
module Analysis = Analysis
module String_map = Map.Make (String)

exception Error of Loc.t option * string

let fail ?loc fmt = Printf.ksprintf (fun msg -> raise (Error (loc, msg))) fmt

let with_default default = function Some value -> value | None -> default

type symbol =
  | Function_symbol of {
      fn : Llvm.llvalue;
      fn_type : Llvm.lltype;
      resolved_type : Analysis.resolved_ty;
      vararg : bool;
      named_params : int;
    }
  | Variable_symbol of {
      storage : Llvm.llvalue;
      resolved_type : Analysis.resolved_ty;
      is_mutable : bool;
    }

type function_state = {
  fn_decl : Core.function_decl;
  fn_value : Llvm.llvalue;
  entry_block : Llvm.llbasicblock;
  return_block : Llvm.llbasicblock;
  return_slot : Llvm.llvalue option;
  return_resolved_type : Analysis.resolved_ty;
  mutable scopes : symbol String_map.t list;
  mutable defer_exprs_rev : Core.expression list;
  mutable loop_stack : (Llvm.llbasicblock * Llvm.llbasicblock) list;
  mutable emitting_defers : bool;
}

type preamble = {
  new_empty_box : Llvm.llvalue;
  new_empty_box_ty : Llvm.lltype;
  new_box : Llvm.llvalue;
  new_box_ty : Llvm.lltype;
  box_ref : Llvm.llvalue;
  box_ref_ty : Llvm.lltype;
  box_unref : Llvm.llvalue;
  box_unref_ty : Llvm.lltype;
}

type global_init = {
  decl : Core.var_decl;
  storage : Llvm.llvalue;
}

type t = {
  pipeline : Analysis.Pipeline.result;
  context : Llvm.llcontext;
  llmodule : Llvm.llmodule;
  builder : Llvm.llbuilder;
  target_machine : Llvm_target.TargetMachine.t;
  data_layout : Llvm_target.DataLayout.t;
  preamble : preamble;
  type_env : Analysis.type_env;
  expr_annotations : (string, Analysis.expr_annotation) Hashtbl.t;
  binding_annotations : (string, Analysis.binding_annotation) Hashtbl.t;
  struct_types : (string, Llvm.lltype) Hashtbl.t;
  enum_types : (string, Llvm.lltype) Hashtbl.t;
  box_types : (string, Llvm.lltype) Hashtbl.t;
  functions : (string, symbol) Hashtbl.t;
  globals : (string, symbol) Hashtbl.t;
  mutable current_function : function_state option;
  mutable next_string_id : int;
  mutable global_inits_rev : global_init list;
  ownership_target_storage : (string, Llvm.llvalue) Hashtbl.t;
}

let initialize_llvm () =
  Llvm_all_backends.initialize ();
  ()

let expr_id = Analysis.expr_id
let binding_id = Analysis.binding_id
let statement_id = Analysis.statement_id
let block_id = Analysis.block_id

let sanitize_name s =
  let bytes = Bytes.of_string s in
  for i = 0 to Bytes.length bytes - 1 do
    match Bytes.get bytes i with
    | 'a' .. 'z' | 'A' .. 'Z' | '0' .. '9' | '_' | '.' -> ()
    | _ -> Bytes.set bytes i '_'
  done;
  Bytes.to_string bytes

let rec mangle_resolved_ty = function
  | Analysis.ResolvedInt (signedness, bits) ->
      Printf.sprintf "%c%d"
        (match signedness with Haven_token.Token.Signed -> 'i' | Unsigned -> 'u')
        bits
  | ResolvedFloat -> "f32"
  | ResolvedString -> "str"
  | ResolvedVoid -> "void"
  | ResolvedPointer inner -> "ptr." ^ mangle_resolved_ty inner
  | ResolvedBox inner -> "box." ^ mangle_resolved_ty inner
  | ResolvedCell inner -> "cell." ^ mangle_resolved_ty inner
  | ResolvedArray (inner, count) ->
      Printf.sprintf "array.%d.%s" count (mangle_resolved_ty inner)
  | ResolvedVec vec ->
      Printf.sprintf "vec.%d"
        vec.Haven_token.Token.dimension
  | ResolvedMatrix mat ->
      Printf.sprintf "mat.%d.%d" mat.rows mat.columns
  | ResolvedFunction (params, ret, vararg) ->
      String.concat "."
        ([ "fn" ]
        @ List.map mangle_resolved_ty params
        @ [ if vararg then "vararg" else "fixed"; mangle_resolved_ty ret ])
  | ResolvedNamed (name, []) -> sanitize_name name
  | ResolvedNamed (name, args) ->
      String.concat "."
        (sanitize_name name :: "of" :: List.map mangle_resolved_ty args)
  | ResolvedGenericParam name -> "generic." ^ sanitize_name name

let current_function t =
  match t.current_function with
  | Some fn -> fn
  | None -> fail "internal codegen error: no active function"

let current_scopes t = (current_function t).scopes

let push_scope t =
  let fn = current_function t in
  fn.scopes <- String_map.empty :: fn.scopes

let pop_scope t =
  let fn = current_function t in
  match fn.scopes with
  | [] -> fail "internal codegen error: no scope to pop"
  | _ :: rest -> fn.scopes <- rest

let add_symbol t name symbol =
  let fn = current_function t in
  match fn.scopes with
  | [] -> fail "internal codegen error: no scope to add symbol %s" name
  | scope :: rest -> fn.scopes <- String_map.add name symbol scope :: rest

let rec lookup_symbol_in scopes name =
  match scopes with
  | [] -> None
  | scope :: rest -> (
      match String_map.find_opt name scope with
      | Some symbol -> Some symbol
      | None -> lookup_symbol_in rest name)

let lookup_symbol t name =
  match lookup_symbol_in (current_scopes t) name with
  | Some symbol -> symbol
  | None -> (
      match Hashtbl.find_opt t.globals name with
      | Some symbol -> symbol
      | None -> (
          match Hashtbl.find_opt t.functions name with
          | Some symbol -> symbol
          | None -> fail "unknown symbol %s during LLVM lowering" name))

let lookup_global_symbol t name =
  match Hashtbl.find_opt t.globals name with
  | Some symbol -> symbol
  | None -> fail "unknown global symbol %s during LLVM lowering" name

let lookup_function_symbol t name =
  match Hashtbl.find_opt t.functions name with
  | Some symbol -> symbol
  | None -> fail "unknown function symbol %s during LLVM lowering" name

let ptr_type t = Llvm.pointer_type t.context
let i1_type t = Llvm.i1_type t.context
let i8_type t = Llvm.i8_type t.context
let i32_type t = Llvm.i32_type t.context
let i64_type t = Llvm.i64_type t.context
let float_type t = Llvm.float_type t.context
let double_type t = Llvm.double_type t.context
let void_type t = Llvm.void_type t.context
let unit_value t = Llvm.const_int (i1_type t) 0
let dummy_loc = { Loc.start_pos = Lexing.dummy_pos; end_pos = Lexing.dummy_pos }

let resolved_type_of_core_type t loc ty =
  match Analysis.resolve_core_type t.type_env [] [] loc ty with
  | Some resolved -> resolved
  | None -> fail ~loc "failed to resolve type during LLVM lowering"

let expr_annotation t (expr : Core.expression) =
  Hashtbl.find_opt t.expr_annotations (expr_id expr)

let expr_resolved_type t (expr : Core.expression) =
  match expr_annotation t expr with
  | Some { resolved_type = Some ty; _ } -> ty
  | Some _ | None ->
      fail ~loc:expr.loc "missing resolved type for expression during LLVM lowering"

let binding_resolved_type t (binding : Core.let_stmt) =
  match Hashtbl.find_opt t.binding_annotations (binding_id binding) with
  | Some { resolved_type = Some ty; _ } -> ty
  | Some _ | None ->
      fail ~loc:binding.loc "missing resolved type for binding during LLVM lowering"

let function_resolved_type t (fn : Core.function_decl) =
  let return_ty =
    match fn.value.return_type with
    | Some ty -> resolved_type_of_core_type t fn.loc ty
    | None -> Analysis.ResolvedVoid
  in
  let param_tys =
    List.map
      (fun (param : Core.param) ->
        resolved_type_of_core_type t param.loc param.value.ty)
      fn.value.params.value.params
  in
  Analysis.ResolvedFunction (param_tys, return_ty, fn.value.vararg)

let rec llvm_type_of_resolved t ?loc = function
  | Analysis.ResolvedInt (_, bits) -> Llvm.integer_type t.context bits
  | ResolvedFloat -> float_type t
  | ResolvedString -> ptr_type t
  | ResolvedVoid -> void_type t
  | ResolvedPointer _ | ResolvedBox _ | ResolvedCell _ | ResolvedFunction _ -> ptr_type t
  | ResolvedArray (inner, count) ->
      Llvm.array_type (llvm_type_of_resolved t ?loc inner) count
  | ResolvedVec _ | ResolvedMatrix _ ->
      fail ?loc "vector and matrix lowering are not implemented yet"
  | (ResolvedNamed _ as resolved) -> llvm_named_type t ?loc resolved
  | ResolvedGenericParam name ->
      fail ?loc "unresolved generic parameter %s reached LLVM lowering" name

and llvm_named_type t ?loc (resolved : Analysis.resolved_ty) =
  match resolved with
  | Analysis.ResolvedNamed (name, _args) -> (
      match Analysis.lookup_named_type t.type_env name with
      | Some (Analysis.TypeStruct decl) -> llvm_struct_type t ?loc resolved decl
      | Some (Analysis.TypeEnum decl) -> llvm_enum_type t ?loc resolved decl
      | Some Analysis.TypeAlias alias -> (
          match Analysis.resolve_core_type t.type_env [] [] (with_default dummy_loc loc) alias with
          | Some alias_ty -> llvm_type_of_resolved t ?loc alias_ty
          | None ->
              fail ?loc "failed to resolve alias %s during LLVM lowering" name)
      | Some Analysis.TypeForward | None ->
          fail ?loc "unknown named type %s during LLVM lowering" name)
  | _ -> fail ?loc "expected named type during LLVM lowering"

and llvm_struct_type t ?loc:_ resolved (decl : Core.struct_decl) =
  let key = mangle_resolved_ty resolved in
  match Hashtbl.find_opt t.struct_types key with
  | Some ty -> ty
  | None ->
      let name = "haven.struct." ^ key in
      let ty = Llvm.named_struct_type t.context name in
      Hashtbl.add t.struct_types key ty;
      let field_types =
        Array.of_list
          (List.map
             (fun (field : Core.struct_field) ->
               let resolved_field =
                 with_default
                   (fail ~loc:field.loc "failed to resolve field type for %s"
                      field.value.name.value)
                   (Analysis.resolve_core_type t.type_env [] [] field.loc field.value.ty)
               in
               llvm_type_of_resolved t ~loc:field.loc resolved_field)
             decl.value.fields)
      in
      Llvm.struct_set_body ty field_types false;
      ty

and llvm_enum_type t ?loc resolved decl =
  let key = mangle_resolved_ty resolved in
  match Hashtbl.find_opt t.enum_types key with
  | Some ty -> ty
  | None ->
      let subst =
        match Analysis.lookup_enum_decl t.type_env (with_default dummy_loc loc) resolved with
        | Some (_, subst) -> subst
        | None -> []
      in
      let payload_types =
        List.filter_map
          (fun (variant : Core.enum_variant) ->
            match variant.value.inner_ty with
            | None -> None
            | Some inner_ty -> (
                match Analysis.resolve_core_type t.type_env [] subst variant.loc inner_ty with
                | Some resolved_inner -> Some (variant, resolved_inner)
                | None ->
                    fail ~loc:variant.loc "failed to resolve enum payload for %s"
                      variant.value.name.value))
          decl.value.variants
      in
      if payload_types = [] then
        let ty = i32_type t in
        Hashtbl.add t.enum_types key ty;
        ty
      else
        let name = "haven.enum." ^ key in
        let ty = Llvm.named_struct_type t.context name in
        Hashtbl.add t.enum_types key ty;
        let max_payload_size =
          List.fold_left
            (fun size (_, payload_ty) ->
              let payload_size =
                Llvm_target.DataLayout.abi_size
                  (llvm_type_of_resolved t ~loc:(with_default dummy_loc loc) payload_ty)
                  t.data_layout
                |> Int64.to_int
              in
              max size payload_size)
            0 payload_types
        in
        let body =
          [| i32_type t; Llvm.array_type (i8_type t) max_payload_size |]
        in
        Llvm.struct_set_body ty body false;
        ty

let zero_constant t resolved =
  Llvm.const_null (llvm_type_of_resolved t resolved)

let declare_runtime_function llmodule name return_ty param_tys =
  let fn_ty = Llvm.function_type return_ty (Array.of_list param_tys) in
  let fn = Llvm.declare_function name fn_ty llmodule in
  Llvm.set_linkage Llvm.Linkage.External fn;
  Llvm.set_function_call_conv Llvm.CallConv.c fn;
  (fn, fn_ty)

let create_preamble context llmodule =
  let ptr_type = Llvm.pointer_type context in
  let i32_type = Llvm.i32_type context in
  let void_type = Llvm.void_type context in
  let new_empty_box, new_empty_box_ty =
    declare_runtime_function llmodule "__haven_new_empty_box" ptr_type [ i32_type ]
  in
  let new_box, new_box_ty =
    declare_runtime_function llmodule "__haven_new_box" ptr_type
      [ ptr_type; i32_type; i32_type ]
  in
  let box_ref, box_ref_ty =
    declare_runtime_function llmodule "__haven_box_ref" void_type [ ptr_type ]
  in
  let box_unref, box_unref_ty =
    declare_runtime_function llmodule "__haven_box_unref" void_type [ ptr_type ]
  in
  { new_empty_box; new_empty_box_ty; new_box; new_box_ty; box_ref; box_ref_ty; box_unref; box_unref_ty }

let create_context (pipeline : Analysis.Pipeline.result) =
  initialize_llvm ();
  let context = Llvm.create_context () in
  let llmodule = Llvm.create_module context "haven" in
  let triple = Llvm_target.Target.default_triple () in
  let target = Llvm_target.Target.by_triple triple in
  let target_machine = Llvm_target.TargetMachine.create ~triple target in
  let data_layout = Llvm_target.TargetMachine.data_layout target_machine in
  Llvm.set_target_triple triple llmodule;
  Llvm.set_data_layout (Llvm_target.DataLayout.as_string data_layout) llmodule;
  let builder = Llvm.builder context in
  let type_env = Analysis.type_env_of_program pipeline.cleaned.program in
  let preamble = create_preamble context llmodule in
  {
    pipeline;
    context;
    llmodule;
    builder;
    target_machine;
    data_layout;
    preamble;
    type_env;
    expr_annotations = pipeline.typing.annotations.exprs;
    binding_annotations = pipeline.typing.annotations.bindings;
    struct_types = Hashtbl.create 32;
    enum_types = Hashtbl.create 32;
    box_types = Hashtbl.create 32;
    functions = Hashtbl.create 64;
    globals = Hashtbl.create 64;
    current_function = None;
    next_string_id = 0;
    global_inits_rev = [];
    ownership_target_storage = Hashtbl.create 32;
  }

let current_block_terminated t =
  Llvm.block_terminator (Llvm.insertion_block t.builder) <> None

let position_builder_for_alloca t =
  let fn = current_function t in
  let entry = fn.entry_block in
  match Llvm.instr_begin entry with
  | Llvm.At_end _ -> Llvm.position_at_end entry t.builder
  | Llvm.Before _ as pos -> (
      let rec find_non_alloca = function
        | Llvm.Before inst as here ->
            if Llvm.instr_opcode inst = Llvm.Opcode.Alloca then
              find_non_alloca (Llvm.instr_succ inst)
            else here
        | Llvm.At_end _ as at_end -> at_end
      in
      match find_non_alloca pos with
      | Llvm.Before inst -> Llvm.position_before inst t.builder
      | Llvm.At_end _ -> Llvm.position_at_end entry t.builder)

let build_alloca t ty name =
  let current_block = Llvm.insertion_block t.builder in
  position_builder_for_alloca t;
  let slot = Llvm.build_alloca ty name t.builder in
  Llvm.position_at_end current_block t.builder;
  slot

let create_string_literal t value =
  let name = Printf.sprintf ".str.%d" t.next_string_id in
  t.next_string_id <- t.next_string_id + 1;
  let init = Llvm.const_stringz t.context value in
  let global = Llvm.define_global name init t.llmodule in
  Llvm.set_linkage Llvm.Linkage.Private global;
  Llvm.set_global_constant true global;
  let zero = Llvm.const_int (i32_type t) 0 in
  Llvm.const_in_bounds_gep (Llvm.type_of init) global [| zero; zero |]

let rec constant_of_expr t (expr : Core.expression) =
  let resolved = expr_resolved_type t expr in
  match expr.value with
  | Core.Literal lit -> constant_of_literal t expr.loc resolved lit
  | Core.Nil when Analysis.resolved_is_pointerish resolved ->
      Some (Llvm.const_null (llvm_type_of_resolved t resolved))
  | Core.As cast -> constant_of_expr t cast.value.inner
  | Core.SizeExpr inner ->
      let size =
        Llvm_target.DataLayout.abi_size
          (llvm_type_of_resolved t (expr_resolved_type t inner))
          t.data_layout
      in
      Some (Llvm.const_int (i64_type t) (Int64.to_int size))
  | Core.SizeType ty -> (
      match Analysis.resolve_core_type t.type_env [] [] ty.loc ty with
      | Some resolved_ty ->
          let size =
            Llvm_target.DataLayout.abi_size (llvm_type_of_resolved t resolved_ty) t.data_layout
          in
          Some (Llvm.const_int (i64_type t) (Int64.to_int size))
      | None -> None)
  | Core.Initializer init -> constant_of_initializer t expr.loc resolved init
  | _ -> None

and constant_of_literal t loc resolved (lit : Core.literal) =
  match (lit.value, resolved) with
  | Core.Integer value, Analysis.ResolvedInt (_, bits) ->
      Some (Llvm.const_int (Llvm.integer_type t.context bits) value)
  | Core.Bool value, Analysis.ResolvedInt (_, bits) ->
      Some (Llvm.const_int (Llvm.integer_type t.context bits) (if value then 1 else 0))
  | Core.Float value, Analysis.ResolvedFloat ->
      Some (Llvm.const_float (float_type t) value)
  | Core.Char value, Analysis.ResolvedInt (_, bits) ->
      Some (Llvm.const_int (Llvm.integer_type t.context bits) (Char.code value))
  | Core.String value, Analysis.ResolvedString -> Some (create_string_literal t value)
  | Core.Enum enum_lit, Analysis.ResolvedNamed _ -> constant_of_enum_literal t loc resolved enum_lit
  | Core.Vector _, _ | Core.Matrix _, _ ->
      fail ~loc "vector and matrix lowering are not implemented yet"
  | _ -> None

and constant_of_enum_literal t loc resolved (enum_lit : Core.enum_literal) =
  match Analysis.lookup_enum_variant t.type_env loc resolved enum_lit.value.enum_variant.value with
  | Some (_, None) ->
      let tag = enum_tag_value enum_lit.value.enum_variant.value resolved t.type_env loc in
      Some (Llvm.const_int (i32_type t) tag)
  | Some (_, Some _) | None -> None

and constant_of_initializer t loc resolved init =
  match resolved with
  | Analysis.ResolvedArray (inner, _) -> (
      let elements =
        List.map (constant_of_expr t) init.value.exprs
      in
      if List.for_all Option.is_some elements then
        let element_ty = llvm_type_of_resolved t inner in
        Some
          (Llvm.const_array element_ty
             (Array.of_list (List.map Option.get elements)))
      else None)
  | Analysis.ResolvedNamed _ as struct_ty -> (
      match Analysis.lookup_struct_fields t.type_env loc struct_ty with
      | Some _fields ->
          let elements = List.map (constant_of_expr t) init.value.exprs in
          if List.for_all Option.is_some elements then
            let llvm_ty = llvm_type_of_resolved t struct_ty in
            Some
              (Llvm.const_named_struct llvm_ty
                 (Array.of_list (List.map Option.get elements)))
          else None
      | None -> None)
  | Analysis.ResolvedVec _ | Analysis.ResolvedMatrix _ ->
      fail ~loc "vector and matrix lowering are not implemented yet"
  | _ -> None

and enum_tag_value variant_name resolved type_env loc =
  match Analysis.lookup_enum_decl type_env loc resolved with
  | Some (decl, _) ->
      let rec loop index = function
        | [] ->
            fail ~loc "enum variant %s not found while lowering" variant_name
        | (variant : Core.enum_variant) :: rest ->
            if String.equal variant.value.name.value variant_name then index
            else loop (index + 1) rest
      in
      loop 0 decl.value.variants
  | None -> fail ~loc "failed to find enum declaration while lowering"

let resolved_is_signed = function
  | Analysis.ResolvedInt (Haven_token.Token.Signed, _) -> true
  | _ -> false

let resolved_is_float = function Analysis.ResolvedFloat -> true | _ -> false

let emit_cast t value source target =
  if Analysis.equal_resolved_type source target then value
  else
    match (source, target) with
    | Analysis.ResolvedInt (_, sbits), Analysis.ResolvedInt (_, tbits) ->
        let dest_ty = Llvm.integer_type t.context tbits in
        if sbits = tbits then Llvm.build_bitcast value dest_ty "int.cast" t.builder
        else if sbits < tbits then
          if resolved_is_signed source then
            Llvm.build_sext value dest_ty "sext" t.builder
          else Llvm.build_zext value dest_ty "zext" t.builder
        else Llvm.build_trunc value dest_ty "trunc" t.builder
    | Analysis.ResolvedInt _, Analysis.ResolvedFloat ->
        Llvm.build_sitofp value (float_type t) "sitofp" t.builder
    | Analysis.ResolvedFloat, Analysis.ResolvedInt (_, bits) ->
        Llvm.build_fptosi value (Llvm.integer_type t.context bits) "fptosi" t.builder
    | source, target
      when Analysis.resolved_is_pointerish source
           && Analysis.resolved_is_pointerish target ->
        Llvm.build_pointercast value (ptr_type t) "ptr.cast" t.builder
    | source, Analysis.ResolvedInt (_, bits)
      when Analysis.resolved_is_pointerish source ->
        Llvm.build_ptrtoint value (Llvm.integer_type t.context bits) "ptrtoint" t.builder
    | Analysis.ResolvedInt _, target
      when Analysis.resolved_is_pointerish target ->
        Llvm.build_inttoptr value (ptr_type t) "inttoptr" t.builder
    | _ ->
        fail "unsupported cast from %s to %s during LLVM lowering"
          (mangle_resolved_ty source) (mangle_resolved_ty target)

let emit_box_ref t box =
  ignore (Llvm.build_call t.preamble.box_ref_ty t.preamble.box_ref [| box |] "" t.builder)

let emit_box_unref t box =
  ignore
    (Llvm.build_call t.preamble.box_unref_ty t.preamble.box_unref [| box |] "" t.builder)

let ensure_storage t resolved value =
  let slot = build_alloca t (llvm_type_of_resolved t resolved) "spill" in
  ignore (Llvm.build_store value slot t.builder);
  slot

let rec emit_ownership_on_storage t kind resolved storage =
  match resolved with
  | Analysis.ResolvedBox _ ->
      let box = Llvm.build_load (ptr_type t) storage "box.handle" t.builder in
      if kind = Analysis.Retain then emit_box_ref t box else emit_box_unref t box
  | Analysis.ResolvedArray (inner, count) ->
      for index = 0 to count - 1 do
        let zero = Llvm.const_int (i32_type t) 0 in
        let idx = Llvm.const_int (i32_type t) index in
        let element_ptr =
          Llvm.build_in_bounds_gep (llvm_type_of_resolved t resolved) storage
            [| zero; idx |] "array.elem" t.builder
        in
        emit_ownership_on_storage t kind inner element_ptr
      done
  | (Analysis.ResolvedNamed _ as named) -> emit_ownership_on_named t kind named storage
  | Analysis.ResolvedPointer _
  | Analysis.ResolvedCell _
  | Analysis.ResolvedString
  | Analysis.ResolvedInt _
  | Analysis.ResolvedFloat
  | Analysis.ResolvedVoid
  | Analysis.ResolvedFunction _
  | Analysis.ResolvedGenericParam _ ->
      ()
  | Analysis.ResolvedVec _ | Analysis.ResolvedMatrix _ ->
      fail "vector and matrix lowering are not implemented yet"

and emit_ownership_on_named t kind resolved storage =
  match resolved with
  | Analysis.ResolvedNamed (name, _args) -> (
      match Analysis.lookup_named_type t.type_env name with
      | Some (Analysis.TypeStruct decl) ->
          let field_types =
            with_default [] (Analysis.lookup_struct_fields t.type_env decl.loc resolved)
          in
          let struct_ty = llvm_type_of_resolved t resolved in
          List.iteri
            (fun index (_, field_ty) ->
              let field_ptr =
                Llvm.build_struct_gep struct_ty storage index "field" t.builder
              in
              emit_ownership_on_storage t kind field_ty field_ptr)
            field_types
      | Some (Analysis.TypeEnum decl) ->
          emit_ownership_on_enum t kind resolved decl storage
      | Some (Analysis.TypeAlias alias) -> (
          match Analysis.resolve_core_type t.type_env [] [] dummy_loc alias with
          | Some alias_ty -> emit_ownership_on_storage t kind alias_ty storage
          | None -> ())
      | Some Analysis.TypeForward | None -> ())
  | _ -> ()

and emit_ownership_on_enum t kind resolved decl storage =
  match Analysis.lookup_enum_decl t.type_env decl.loc resolved with
  | Some (_, subst) ->
      let owned_variants =
        List.filter_map
          (fun (variant : Core.enum_variant) ->
            match variant.value.inner_ty with
            | Some inner_ty -> (
                match Analysis.resolve_core_type t.type_env [] subst variant.loc inner_ty with
                | Some resolved_inner
                  when Analysis.resolved_contains_box_ownership t.type_env [] variant.loc
                         resolved_inner ->
                    Some (variant, resolved_inner)
                | Some _ | None -> None)
            | None -> None)
          decl.value.variants
      in
      if owned_variants <> [] then (
        let enum_ty = llvm_type_of_resolved t resolved in
        let tag_ptr = Llvm.build_struct_gep enum_ty storage 0 "enum.tag" t.builder in
        let buf_ptr = Llvm.build_struct_gep enum_ty storage 1 "enum.buf" t.builder in
        let tag = Llvm.build_load (i32_type t) tag_ptr "enum.tag.value" t.builder in
        let current_block = Llvm.insertion_block t.builder in
        let fn_value = Llvm.block_parent current_block in
        let end_block = Llvm.append_block t.context "enum.own.end" fn_value in
        let default_block = Llvm.append_block t.context "enum.own.default" fn_value in
        let switch = Llvm.build_switch tag default_block (List.length owned_variants) t.builder in
        List.iter
          (fun ((variant : Core.enum_variant), resolved_inner) ->
            let tag_value =
              enum_tag_value variant.value.name.value resolved t.type_env variant.loc
            in
            let arm_block =
              Llvm.append_block t.context "enum.own.arm" fn_value
            in
            Llvm.add_case switch (Llvm.const_int (i32_type t) tag_value) arm_block;
            Llvm.position_at_end arm_block t.builder;
            let payload_ptr =
              Llvm.build_bitcast buf_ptr (ptr_type t) "enum.payload.raw" t.builder
            in
            let typed_ptr =
              Llvm.build_pointercast payload_ptr (ptr_type t) "enum.payload.ptr" t.builder
            in
            let typed_slot =
              ensure_storage t resolved_inner
                (Llvm.build_load (llvm_type_of_resolved t resolved_inner) typed_ptr
                   "enum.payload.value" t.builder)
            in
            emit_ownership_on_storage t kind resolved_inner typed_slot;
            ignore (Llvm.build_br end_block t.builder))
          owned_variants;
        Llvm.position_at_end default_block t.builder;
        ignore (Llvm.build_br end_block t.builder);
        Llvm.position_at_end end_block t.builder)
  | None -> ()

let emit_ownership_on_value t kind resolved value =
  match resolved with
  | Analysis.ResolvedBox _ ->
      if kind = Analysis.Retain then emit_box_ref t value else emit_box_unref t value
  | _ ->
      let storage = ensure_storage t resolved value in
      emit_ownership_on_storage t kind resolved storage

let emit_after_expr_actions t (expr : Core.expression) value =
  List.iter
    (fun (action : Analysis.ownership_action) ->
      match (action.kind, action.subject, action.resolved_type) with
      | kind, Analysis.OwnershipExpr (id, _), Some resolved
        when String.equal id (expr_id expr) ->
          emit_ownership_on_value t kind resolved value
      | _ -> ())
    (Analysis.Ownership.actions_after_expr t.pipeline.ownership expr)

let emit_before_expr_actions t (expr : Core.expression) =
  List.iter
    (fun (action : Analysis.ownership_action) ->
      match (action.kind, action.subject, action.resolved_type) with
      | kind, Analysis.OwnershipTarget (id, _), Some resolved -> (
          match Hashtbl.find_opt t.ownership_target_storage id with
          | Some storage -> emit_ownership_on_storage t kind resolved storage
          | None ->
              fail ~loc:expr.loc
                "missing ownership target storage for expression during LLVM lowering")
      | _ -> ())
    (Analysis.Ownership.actions_before_expr t.pipeline.ownership expr)

let emit_scope_action t (action : Analysis.ownership_action) =
  match (action.kind, action.subject, action.resolved_type) with
  | kind, Analysis.OwnershipBinding name, Some resolved
  | kind, Analysis.OwnershipParam name, Some resolved -> (
      match lookup_symbol_in (current_scopes t) name with
      | Some (Variable_symbol { storage; _ }) ->
          emit_ownership_on_storage t kind resolved storage
      | _ -> fail "missing storage for ownership subject %s" name)
  | _ -> ()

let emit_before_stmt_actions t stmt =
  List.iter (emit_scope_action t)
    (Analysis.Ownership.actions_before_stmt t.pipeline.ownership stmt)

let emit_block_exit_actions t block =
  List.iter (emit_scope_action t)
    (Analysis.Ownership.actions_on_block_exit t.pipeline.ownership block)

let emit_loop_exit_actions t stmt =
  List.iter (emit_scope_action t)
    (Analysis.Ownership.actions_on_loop_exit t.pipeline.ownership stmt)

let emit_function_exit_actions t fn_decl =
  List.iter (emit_scope_action t)
    (Analysis.Ownership.actions_on_function_exit t.pipeline.ownership fn_decl)

let load_variable t storage resolved = Llvm.build_load (llvm_type_of_resolved t resolved) storage "load" t.builder

let emit_to_bool t (expr : Core.expression) value =
  match expr_resolved_type t expr with
  | Analysis.ResolvedInt (_, 1) -> value
  | Analysis.ResolvedInt _ ->
      Llvm.build_icmp Llvm.Icmp.Ne value
        (Llvm.const_int (Llvm.type_of value) 0)
        "tobool" t.builder
  | Analysis.ResolvedFloat ->
      Llvm.build_fcmp Llvm.Fcmp.One value
        (Llvm.const_float (float_type t) 0.0)
        "tobool" t.builder
  | ty when Analysis.resolved_is_pointerish ty -> Llvm.build_is_not_null value "tobool" t.builder
  | ty ->
      fail ~loc:expr.loc "cannot convert %s to bool during LLVM lowering"
        (mangle_resolved_ty ty)

let rec emit_lvalue t (expr : Core.expression) =
  match expr.value with
  | Core.Identifier id -> (
      match lookup_symbol t id.value with
      | Variable_symbol { storage; _ } -> storage
      | Function_symbol _ ->
          fail ~loc:expr.loc "function %s is not addressable as an lvalue" id.value)
  | Core.Field field ->
      emit_field_lvalue t expr field
  | Core.Index index ->
      emit_index_lvalue t expr index
  | Core.Unbox inner ->
      let inner_ty = expr_resolved_type t inner in
      let box_handle = emit_expr t inner in
      emit_box_value_ptr t inner_ty box_handle
  | Core.Load inner -> emit_expr t inner
  | _ -> fail ~loc:expr.loc "expression is not an lvalue in LLVM lowering"

and emit_addressable_struct t (expr : Core.expression) =
  let value = emit_expr t expr in
  let resolved = expr_resolved_type t expr in
  match expr.value with
  | Core.Identifier _ | Core.Field _ | Core.Index _ | Core.Unbox _ ->
      emit_lvalue t expr
  | _ -> ensure_storage t resolved value

and emit_box_layout t pointee =
  let key = mangle_resolved_ty pointee in
  match Hashtbl.find_opt t.box_types key with
  | Some ty -> ty
  | None ->
      let ty = Llvm.named_struct_type t.context ("haven.box." ^ key) in
      Hashtbl.add t.box_types key ty;
      Llvm.struct_set_body ty
        [| Llvm.array_type (i32_type t) 4; llvm_type_of_resolved t pointee |]
        true;
      ty

and emit_box_value_ptr t inner_ty box_handle =
  let pointee =
    match inner_ty with
    | Analysis.ResolvedBox inner -> inner
    | Analysis.ResolvedCell inner -> inner
    | _ -> fail "expected box or cell while lowering box access"
  in
  let box_ty = emit_box_layout t pointee in
  Llvm.build_struct_gep box_ty box_handle 1 "box.value" t.builder

and emit_field_lvalue t (expr : Core.expression) (field : Core.field) =
  let target_resolved = expr_resolved_type t field.value.target in
  let struct_storage, struct_resolved =
    if field.value.arrow then
      let pointer_value = emit_expr t field.value.target in
      let pointee =
        match target_resolved with
        | Analysis.ResolvedPointer inner
        | Analysis.ResolvedCell inner ->
            (pointer_value, inner)
        | Analysis.ResolvedBox inner ->
            (emit_box_value_ptr t target_resolved pointer_value, inner)
        | _ -> fail ~loc:expr.loc "arrow field access requires pointer-like target"
      in
      pointee
    else
      let storage = emit_addressable_struct t field.value.target in
      (storage, target_resolved)
  in
  match Analysis.lookup_struct_fields t.type_env expr.loc struct_resolved with
  | Some fields ->
      let rec find_index index = function
        | [] ->
            fail ~loc:expr.loc "unknown field %s during LLVM lowering"
              field.value.field.value
        | (name, _) :: rest ->
            if String.equal name field.value.field.value then index
            else find_index (index + 1) rest
      in
      let struct_ty = llvm_type_of_resolved t struct_resolved in
      Llvm.build_struct_gep struct_ty struct_storage (find_index 0 fields) "field.ptr"
        t.builder
  | None ->
      fail ~loc:expr.loc "field access requires a struct target during LLVM lowering"

and emit_index_lvalue t (expr : Core.expression) (index : Core.index) =
  let target_ty = expr_resolved_type t index.value.target in
  let idx = emit_expr t index.value.index in
  match target_ty with
  | Analysis.ResolvedPointer inner ->
      let target = emit_expr t index.value.target in
      Llvm.build_in_bounds_gep (llvm_type_of_resolved t inner) target [| idx |] "ptr.index"
        t.builder
  | Analysis.ResolvedArray (_inner, _) ->
      let target = emit_addressable_struct t index.value.target in
      let zero = Llvm.const_int (i32_type t) 0 in
      Llvm.build_in_bounds_gep (llvm_type_of_resolved t target_ty) target [| zero; idx |]
        "array.index" t.builder
  | Analysis.ResolvedVec _ | Analysis.ResolvedMatrix _ ->
      fail ~loc:expr.loc "vector and matrix lowering are not implemented yet"
  | _ ->
      fail ~loc:expr.loc "indexing unsupported for %s during LLVM lowering"
        (mangle_resolved_ty target_ty)

and emit_identifier t (_expr : Core.expression) (id : Core.identifier) =
  match lookup_symbol t id.value with
  | Variable_symbol { storage; resolved_type; _ } ->
      load_variable t storage resolved_type
  | Function_symbol { fn; _ } -> fn

and emit_literal t (expr : Core.expression) lit =
  let resolved = expr_resolved_type t expr in
  match constant_of_literal t expr.loc resolved lit with
  | Some value -> value
  | None ->
      fail ~loc:expr.loc "non-constant literal form unsupported during LLVM lowering"

and emit_initializer t (expr : Core.expression) (init : Core.init_list) =
  let resolved = expr_resolved_type t expr in
  let elements = List.map (emit_expr t) init.value.exprs in
  match resolved with
  | Analysis.ResolvedArray _ ->
      List.fold_left
        (fun aggregate (value, index) ->
          Llvm.build_insertvalue aggregate value index "insert" t.builder)
        (Llvm.undef (llvm_type_of_resolved t resolved))
        (List.mapi (fun index value -> (value, index)) elements)
  | Analysis.ResolvedNamed _ -> (
      match Analysis.lookup_struct_fields t.type_env expr.loc resolved with
      | Some _ ->
          List.fold_left
            (fun aggregate (value, index) ->
              Llvm.build_insertvalue aggregate value index "insert" t.builder)
            (Llvm.undef (llvm_type_of_resolved t resolved))
            (List.mapi (fun index value -> (value, index)) elements)
      | None ->
          fail ~loc:expr.loc "initializer expects struct target during LLVM lowering")
  | Analysis.ResolvedVec _ | Analysis.ResolvedMatrix _ ->
      fail ~loc:expr.loc "vector and matrix lowering are not implemented yet"
  | _ -> fail ~loc:expr.loc "initializer unsupported for this type during LLVM lowering"

and emit_enum_literal t (expr : Core.expression) (enum_lit : Core.enum_literal) =
  let resolved = expr_resolved_type t expr in
  match resolved with
  | Analysis.ResolvedNamed _ -> (
      match Analysis.lookup_enum_variant t.type_env expr.loc resolved enum_lit.value.enum_variant.value with
      | Some (_, None) ->
          Llvm.const_int (i32_type t)
            (enum_tag_value enum_lit.value.enum_variant.value resolved t.type_env expr.loc)
      | Some (_, Some inner_ty) ->
          let enum_ty = llvm_type_of_resolved t resolved in
          let slot = build_alloca t enum_ty "enum.literal" in
          let tag_ptr = Llvm.build_struct_gep enum_ty slot 0 "enum.tag" t.builder in
          let buf_ptr = Llvm.build_struct_gep enum_ty slot 1 "enum.buf" t.builder in
          ignore
            (Llvm.build_store
               (Llvm.const_int (i32_type t)
                  (enum_tag_value enum_lit.value.enum_variant.value resolved t.type_env expr.loc))
               tag_ptr t.builder);
          (match enum_lit.value.wrapped with
          | [ wrapped ] ->
              let payload = emit_expr t wrapped in
              let payload_ptr =
                Llvm.build_bitcast buf_ptr (ptr_type t) "enum.payload.raw" t.builder
              in
              let payload_slot =
                ensure_storage t inner_ty
                  (emit_cast t payload (expr_resolved_type t wrapped) inner_ty)
              in
              let payload_value =
                Llvm.build_load (llvm_type_of_resolved t inner_ty) payload_slot "enum.payload"
                  t.builder
              in
              ignore (Llvm.build_store payload_value payload_ptr t.builder)
          | [] -> ()
          | _ ->
              fail ~loc:expr.loc "enum constructors with multiple payload values are unsupported");
          Llvm.build_load enum_ty slot "enum.literal.value" t.builder
      | None ->
          fail ~loc:expr.loc "failed to resolve enum literal during LLVM lowering")
  | _ -> fail ~loc:expr.loc "enum literal requires enum type during LLVM lowering"

and emit_binary t (expr : Core.expression) (binary : Core.binary) =
  let lhs = emit_expr t binary.value.left in
  let rhs = emit_expr t binary.value.right in
  let lhs_ty = expr_resolved_type t binary.value.left in
  let rhs_ty = expr_resolved_type t binary.value.right in
  let result_ty = expr_resolved_type t expr in
  match (binary.value.op, lhs_ty, rhs_ty, result_ty) with
  | ( Core.Add | Core.Subtract | Core.Multiply | Core.Divide | Core.Modulo ),
    Analysis.ResolvedFloat,
    Analysis.ResolvedFloat,
    Analysis.ResolvedFloat -> (
      match binary.value.op with
      | Core.Add -> Llvm.build_fadd lhs rhs "fadd" t.builder
      | Core.Subtract -> Llvm.build_fsub lhs rhs "fsub" t.builder
      | Core.Multiply -> Llvm.build_fmul lhs rhs "fmul" t.builder
      | Core.Divide -> Llvm.build_fdiv lhs rhs "fdiv" t.builder
      | Core.Modulo -> Llvm.build_frem lhs rhs "frem" t.builder
      | _ -> assert false)
  | ( Core.IsEqual | Core.NotEqual | Core.LessThan | Core.LessThanOrEqual | Core.GreaterThan
    | Core.GreaterThanOrEqual ),
    Analysis.ResolvedFloat,
    Analysis.ResolvedFloat,
    Analysis.ResolvedInt (_, 1) ->
      let pred =
        match binary.value.op with
        | Core.IsEqual -> Llvm.Fcmp.Oeq
        | Core.NotEqual -> Llvm.Fcmp.One
        | Core.LessThan -> Llvm.Fcmp.Olt
        | Core.LessThanOrEqual -> Llvm.Fcmp.Ole
        | Core.GreaterThan -> Llvm.Fcmp.Ogt
        | Core.GreaterThanOrEqual -> Llvm.Fcmp.Oge
        | _ -> assert false
      in
      Llvm.build_fcmp pred lhs rhs "fcmp" t.builder
  | ( Core.LogicAnd | Core.LogicOr ), _, _, Analysis.ResolvedInt (_, 1) ->
      let lhs_bool = emit_to_bool t binary.value.left lhs in
      let current_block = Llvm.insertion_block t.builder in
      let fn_value = Llvm.block_parent current_block in
      let rhs_block = Llvm.append_block t.context "logic.rhs" fn_value in
      let end_block = Llvm.append_block t.context "logic.end" fn_value in
      ignore
        (Llvm.build_cond_br lhs_bool
           (if binary.value.op = Core.LogicAnd then rhs_block else end_block)
           (if binary.value.op = Core.LogicAnd then end_block else rhs_block)
           t.builder);
      Llvm.position_at_end rhs_block t.builder;
      let rhs_value = emit_expr t binary.value.right in
      let rhs_bool = emit_to_bool t binary.value.right rhs_value in
      let rhs_block_final = Llvm.insertion_block t.builder in
      ignore (Llvm.build_br end_block t.builder);
      Llvm.position_at_end end_block t.builder;
      let incoming =
        if binary.value.op = Core.LogicAnd then
          [ (Llvm.const_int (i1_type t) 0, current_block); (rhs_bool, rhs_block_final) ]
        else
          [ (Llvm.const_int (i1_type t) 1, current_block); (rhs_bool, rhs_block_final) ]
      in
      Llvm.build_phi incoming "logic.phi" t.builder
  | _ -> emit_nonfloat_binary t expr binary lhs rhs lhs_ty rhs_ty result_ty

and emit_nonfloat_binary t (expr : Core.expression) binary lhs rhs lhs_ty rhs_ty result_ty =
  match binary.value.op with
  | Core.IsEqual ->
      emit_integer_or_pointer_cmp t expr Llvm.Icmp.Eq lhs rhs lhs_ty rhs_ty
  | Core.NotEqual ->
      emit_integer_or_pointer_cmp t expr Llvm.Icmp.Ne lhs rhs lhs_ty rhs_ty
  | Core.LessThan ->
      emit_integer_or_pointer_cmp t expr
        (if resolved_is_signed lhs_ty then Llvm.Icmp.Slt else Llvm.Icmp.Ult)
        lhs rhs lhs_ty rhs_ty
  | Core.LessThanOrEqual ->
      emit_integer_or_pointer_cmp t expr
        (if resolved_is_signed lhs_ty then Llvm.Icmp.Sle else Llvm.Icmp.Ule)
        lhs rhs lhs_ty rhs_ty
  | Core.GreaterThan ->
      emit_integer_or_pointer_cmp t expr
        (if resolved_is_signed lhs_ty then Llvm.Icmp.Sgt else Llvm.Icmp.Ugt)
        lhs rhs lhs_ty rhs_ty
  | Core.GreaterThanOrEqual ->
      emit_integer_or_pointer_cmp t expr
        (if resolved_is_signed lhs_ty then Llvm.Icmp.Sge else Llvm.Icmp.Uge)
        lhs rhs lhs_ty rhs_ty
  | Core.Add when Analysis.resolved_is_pointerish result_ty ->
      Llvm.build_in_bounds_gep
        (llvm_type_of_resolved t
           (match result_ty with Analysis.ResolvedPointer inner -> inner | _ -> result_ty))
        (if Analysis.resolved_is_pointerish lhs_ty then lhs else rhs)
        [| if Analysis.resolved_is_pointerish lhs_ty then rhs else lhs |]
        "ptr.add" t.builder
  | Core.Add -> Llvm.build_add lhs rhs "add" t.builder
  | Core.Subtract -> Llvm.build_sub lhs rhs "sub" t.builder
  | Core.Multiply -> Llvm.build_mul lhs rhs "mul" t.builder
  | Core.Divide ->
      if resolved_is_signed lhs_ty then Llvm.build_sdiv lhs rhs "sdiv" t.builder
      else Llvm.build_udiv lhs rhs "udiv" t.builder
  | Core.Modulo ->
      if resolved_is_signed lhs_ty then Llvm.build_srem lhs rhs "srem" t.builder
      else Llvm.build_urem lhs rhs "urem" t.builder
  | Core.LeftShift -> Llvm.build_shl lhs rhs "shl" t.builder
  | Core.RightShift ->
      if resolved_is_signed lhs_ty then Llvm.build_ashr lhs rhs "ashr" t.builder
      else Llvm.build_lshr lhs rhs "lshr" t.builder
  | Core.BitwiseAnd -> Llvm.build_and lhs rhs "and" t.builder
  | Core.BitwiseXor -> Llvm.build_xor lhs rhs "xor" t.builder
  | Core.BitwiseOr -> Llvm.build_or lhs rhs "or" t.builder
  | Core.LogicAnd | Core.LogicOr ->
      fail ~loc:expr.loc "logical operator lowering bug"

and emit_integer_or_pointer_cmp t (_expr : Core.expression) pred lhs rhs lhs_ty _rhs_ty =
  let lhs, rhs =
    if Analysis.resolved_is_pointerish lhs_ty then
      ( emit_cast t lhs lhs_ty (Analysis.ResolvedInt (Unsigned, 64)),
        emit_cast t rhs lhs_ty (Analysis.ResolvedInt (Unsigned, 64)) )
    else (lhs, rhs)
  in
  Llvm.build_icmp pred lhs rhs "icmp" t.builder

and emit_call t (expr : Core.expression) (call : Core.call) =
  let target_value = emit_expr t call.value.target in
  let target_resolved = expr_resolved_type t call.value.target in
  match target_resolved with
  | Analysis.ResolvedFunction (params, ret, vararg) ->
      let args =
        Array.of_list
          (List.mapi
             (fun index arg ->
               let arg_value = emit_expr t arg in
               let arg_resolved = expr_resolved_type t arg in
               if vararg && index >= List.length params && Analysis.equal_resolved_type arg_resolved Analysis.ResolvedFloat then
                 Llvm.build_fpext arg_value (double_type t) "vararg.float" t.builder
               else
                 let expected =
                   match List.nth_opt params index with
                   | Some ty -> ty
                   | None -> arg_resolved
                 in
                 emit_cast t arg_value arg_resolved expected)
             call.value.params)
      in
      let fn_ty =
        if vararg then
          Llvm.var_arg_function_type (llvm_type_of_resolved t ret)
            (Array.of_list (List.map (llvm_type_of_resolved t) params))
        else
          Llvm.function_type (llvm_type_of_resolved t ret)
            (Array.of_list (List.map (llvm_type_of_resolved t) params))
      in
      let result = Llvm.build_call fn_ty target_value args "call" t.builder in
      if Analysis.equal_resolved_type ret Analysis.ResolvedVoid then unit_value t else result
  | _ -> fail ~loc:expr.loc "call target is not callable during LLVM lowering"

and emit_match t (expr : Core.expression) (match_expr : Core.match_expr) =
  let result_ty = expr_resolved_type t expr in
  let result_slot =
    match result_ty with
    | Analysis.ResolvedVoid -> None
    | ty -> Some (build_alloca t (llvm_type_of_resolved t ty) "match.result")
  in
  let scrutinee_ty = expr_resolved_type t match_expr.value.expr in
  let scrutinee_value = emit_expr t match_expr.value.expr in
  let scrutinee_storage =
    if Analysis.equal_resolved_type scrutinee_ty Analysis.ResolvedVoid then None
    else Some (ensure_storage t scrutinee_ty scrutinee_value)
  in
  let switch_value =
    match (scrutinee_ty, scrutinee_storage) with
    | Analysis.ResolvedNamed _, Some storage when Llvm.classify_type (llvm_type_of_resolved t scrutinee_ty) = Llvm.TypeKind.Struct ->
        let enum_ty = llvm_type_of_resolved t scrutinee_ty in
        let tag_ptr = Llvm.build_struct_gep enum_ty storage 0 "match.tag" t.builder in
        Llvm.build_load (i32_type t) tag_ptr "match.tag.value" t.builder
    | _, _ -> scrutinee_value
  in
  let current_block = Llvm.insertion_block t.builder in
  let fn_value = Llvm.block_parent current_block in
  let end_block = Llvm.append_block t.context "match.end" fn_value in
  let default_block = Llvm.append_block t.context "match.default" fn_value in
  let cases =
    List.filter_map
      (fun (arm : Core.match_arm) ->
        match arm.value.pattern.value with
        | Core.PatternDefault -> None
        | _ -> Some arm)
      match_expr.value.arms
  in
  let switch = Llvm.build_switch switch_value default_block (List.length cases) t.builder in
  List.iter
    (fun (arm : Core.match_arm) ->
      let arm_block = Llvm.append_block t.context "match.arm" fn_value in
      Llvm.add_case switch (match_pattern_value t scrutinee_ty arm.value.pattern) arm_block;
      emit_match_arm t result_ty scrutinee_ty scrutinee_storage result_slot end_block arm
        arm_block)
    cases;
  Llvm.position_at_end default_block t.builder;
  (match List.find_opt
           (fun (arm : Core.match_arm) ->
             arm.value.pattern.value = Core.PatternDefault)
           match_expr.value.arms with
  | Some default_arm ->
      emit_match_arm t result_ty scrutinee_ty scrutinee_storage result_slot end_block
        default_arm default_block
  | None -> ignore (Llvm.build_br end_block t.builder));
  Llvm.position_at_end end_block t.builder;
  match result_slot with
  | Some slot -> Llvm.build_load (llvm_type_of_resolved t result_ty) slot "match.result" t.builder
  | None -> unit_value t

and match_pattern_value t scrutinee_ty (pattern : Core.match_pattern) =
  match pattern.value with
  | Core.PatternDefault ->
      fail ~loc:pattern.loc "default pattern cannot be lowered as a switch case"
  | Core.PatternLiteral lit -> (
      match constant_of_literal t pattern.loc scrutinee_ty lit with
      | Some value -> value
      | None -> fail ~loc:pattern.loc "non-constant pattern during LLVM lowering")
  | Core.PatternEnum enum_pat ->
      Llvm.const_int (i32_type t)
        (enum_tag_value enum_pat.value.enum_variant.value scrutinee_ty t.type_env pattern.loc)

and emit_match_arm t result_ty scrutinee_ty scrutinee_storage result_slot end_block arm
    arm_block =
  Llvm.position_at_end arm_block t.builder;
  push_scope t;
  (match (arm.value.pattern.value, scrutinee_ty, scrutinee_storage) with
  | Core.PatternEnum enum_pat, Analysis.ResolvedNamed _, Some storage ->
      bind_enum_pattern_payload t enum_pat scrutinee_ty storage
  | _ -> ());
  let value = emit_expr t arm.value.expr in
  (match result_slot with
  | Some slot ->
      let cast_value =
        emit_cast t value (expr_resolved_type t arm.value.expr) result_ty
      in
      ignore (Llvm.build_store cast_value slot t.builder)
  | None -> ());
  pop_scope t;
  if not (current_block_terminated t) then ignore (Llvm.build_br end_block t.builder)

and bind_enum_pattern_payload t enum_pat scrutinee_ty storage =
  match Analysis.lookup_enum_variant t.type_env enum_pat.loc scrutinee_ty
          enum_pat.value.enum_variant.value with
  | Some (_, Some inner_ty) -> (
      match enum_pat.value.binding with
      | [ binding ] -> (
          match binding.value with
          | Core.BindingIgnored -> ()
          | Core.BindingNamed id ->
              let enum_ty = llvm_type_of_resolved t scrutinee_ty in
              let buf_ptr = Llvm.build_struct_gep enum_ty storage 1 "match.buf" t.builder in
              let payload_ptr =
                Llvm.build_bitcast buf_ptr (ptr_type t) "match.payload.raw" t.builder
              in
              let payload_value =
                Llvm.build_load (llvm_type_of_resolved t inner_ty) payload_ptr
                  "match.payload" t.builder
              in
              let slot = build_alloca t (llvm_type_of_resolved t inner_ty) id.value in
              ignore (Llvm.build_store payload_value slot t.builder);
              add_symbol t id.value
                (Variable_symbol { storage = slot; resolved_type = inner_ty; is_mutable = false }))
      | [] -> ()
      | _ ->
          fail ~loc:enum_pat.loc
            "enum patterns with multiple bindings are unsupported during LLVM lowering")
  | Some (_, None) | None -> ()

and emit_deferred_exprs t =
  let fn = current_function t in
  if not fn.emitting_defers then (
    fn.emitting_defers <- true;
    List.iter (fun expr -> ignore (emit_expr t expr)) (List.rev fn.defer_exprs_rev);
    fn.emitting_defers <- false)

and emit_expr t (expr : Core.expression) =
  let value =
    match expr.value with
    | Core.Identifier id -> emit_identifier t expr id
    | Core.Literal lit -> emit_literal t expr lit
    | Core.Binary binary -> emit_binary t expr binary
    | Core.Unary unary -> emit_unary t expr unary
    | Core.Block block -> emit_block_expr t expr block
    | Core.ToBool inner ->
        let inner_value = emit_expr t inner in
        emit_to_bool t inner inner_value
    | Core.Initializer init -> emit_initializer t expr init
    | Core.As cast ->
        let value = emit_expr t cast.value.inner in
        emit_cast t value (expr_resolved_type t cast.value.inner) (expr_resolved_type t expr)
    | Core.SizeExpr _ | Core.SizeType _ -> (
        match constant_of_expr t expr with
        | Some value -> value
        | None -> fail ~loc:expr.loc "failed to lower size expression")
    | Core.Nil ->
        Llvm.const_null (llvm_type_of_resolved t (expr_resolved_type t expr))
    | Core.Match match_expr -> emit_match t expr match_expr
    | Core.BoxExpr inner -> emit_box_expr t expr inner
    | Core.BoxType _ ->
        fail ~loc:expr.loc "box type expressions are not supported during LLVM lowering"
    | Core.Unbox inner -> emit_unbox t expr inner
    | Core.Ref inner -> emit_lvalue t inner
    | Core.Load inner ->
        let ptr = emit_expr t inner in
        Llvm.build_load (llvm_type_of_resolved t (expr_resolved_type t expr)) ptr "load"
          t.builder
    | Core.Call call -> emit_call t expr call
    | Core.Index index ->
        let ptr = emit_index_lvalue t expr index in
        Llvm.build_load (llvm_type_of_resolved t (expr_resolved_type t expr)) ptr "index.load"
          t.builder
    | Core.Field field ->
        let ptr = emit_field_lvalue t expr field in
        Llvm.build_load (llvm_type_of_resolved t (expr_resolved_type t expr)) ptr "field.load"
          t.builder
    | Core.Assign write -> emit_assign t expr write
    | Core.Mutate write -> emit_mutate t expr write
  in
  emit_after_expr_actions t expr value;
  value

and emit_box_expr t (_expr : Core.expression) inner =
  let inner_resolved = expr_resolved_type t inner in
  let box_layout = emit_box_layout t inner_resolved in
  let box_size =
    Llvm_target.DataLayout.abi_size box_layout t.data_layout |> Int64.to_int
  in
  match constant_of_expr t inner with
  | _ ->
      let payload = emit_expr t inner in
      let payload_slot = ensure_storage t inner_resolved payload in
      let payload_ptr =
        Llvm.build_pointercast payload_slot (ptr_type t) "box.payload.ptr" t.builder
      in
      let value_size =
        Llvm_target.DataLayout.abi_size (llvm_type_of_resolved t inner_resolved) t.data_layout
        |> Int64.to_int
      in
      Llvm.build_call t.preamble.new_box_ty t.preamble.new_box
        [|
          payload_ptr;
          Llvm.const_int (i32_type t) box_size;
          Llvm.const_int (i32_type t) value_size;
        |]
        "box.new" t.builder

and emit_unbox t (expr : Core.expression) inner =
  let inner_value = emit_expr t inner in
  let inner_ty = expr_resolved_type t inner in
  let value_ptr = emit_box_value_ptr t inner_ty inner_value in
  Llvm.build_load (llvm_type_of_resolved t (expr_resolved_type t expr)) value_ptr "unbox"
    t.builder

and emit_assign t (expr : Core.expression) (write : Core.write) =
  let target_ptr = emit_lvalue t write.value.target in
  Hashtbl.replace t.ownership_target_storage (expr_id write.value.target) target_ptr;
  emit_before_expr_actions t expr;
  let target_resolved = expr_resolved_type t write.value.target in
  let value = emit_expr t write.value.value in
  let cast_value = emit_cast t value (expr_resolved_type t write.value.value) target_resolved in
  ignore (Llvm.build_store cast_value target_ptr t.builder);
  cast_value

and emit_mutate t (expr : Core.expression) (write : Core.write) =
  let target_value = emit_expr t write.value.target in
  let target_resolved = expr_resolved_type t write.value.target in
  let pointee, target_ptr =
    match target_resolved with
    | Analysis.ResolvedPointer inner | Analysis.ResolvedCell inner -> (inner, target_value)
    | Analysis.ResolvedBox inner -> (inner, emit_box_value_ptr t target_resolved target_value)
    | _ ->
        fail ~loc:expr.loc "mutate requires pointer-like target during LLVM lowering"
  in
  Hashtbl.replace t.ownership_target_storage (expr_id write.value.target) target_ptr;
  emit_before_expr_actions t expr;
  let value = emit_expr t write.value.value in
  let cast_value = emit_cast t value (expr_resolved_type t write.value.value) pointee in
  ignore (Llvm.build_store cast_value target_ptr t.builder);
  cast_value

and emit_unary t (expr : Core.expression) (unary : Core.unary) =
  let inner = emit_expr t unary.value.inner in
  match (unary.value.op, expr_resolved_type t unary.value.inner) with
  | Core.Negate, Analysis.ResolvedFloat -> Llvm.build_fneg inner "fneg" t.builder
  | Core.Negate, Analysis.ResolvedInt _ -> Llvm.build_neg inner "neg" t.builder
  | Core.Not, _ ->
      let bool_value = emit_to_bool t unary.value.inner inner in
      Llvm.build_not bool_value "not" t.builder
  | Core.Complement, Analysis.ResolvedInt _ -> Llvm.build_not inner "compl" t.builder
  | _ -> fail ~loc:expr.loc "unsupported unary operator during LLVM lowering"

and emit_block_expr t (expr : Core.expression) block =
  let slot =
    match expr_resolved_type t expr with
    | Analysis.ResolvedVoid -> None
    | ty -> Some (build_alloca t (llvm_type_of_resolved t ty) "block.result")
  in
  let end_block = Llvm.append_block t.context "block.end" (current_function t).fn_value in
  push_scope t;
  emit_statements t block.value.statements;
  if not (current_block_terminated t) then (
    Option.iter
      (fun result ->
        let value = emit_expr t result in
        match slot with
        | Some slot ->
            ignore
              (Llvm.build_store
                 (emit_cast t value (expr_resolved_type t result) (expr_resolved_type t expr))
                 slot t.builder)
        | None -> ())
      block.value.result;
    emit_block_exit_actions t block;
    ignore (Llvm.build_br end_block t.builder));
  pop_scope t;
  Llvm.position_at_end end_block t.builder;
  match slot with
  | Some slot -> Llvm.build_load (llvm_type_of_resolved t (expr_resolved_type t expr)) slot "block.result" t.builder
  | None -> unit_value t

and emit_statements t statements =
  List.iter (emit_statement t) statements

and emit_statement t (stmt : Core.statement) =
  match stmt.value with
  | Core.Expression expr ->
      ignore (emit_expr t expr)
  | Core.Let binding ->
      let resolved = binding_resolved_type t binding in
      let slot =
        build_alloca t (llvm_type_of_resolved t resolved) binding.value.name.value
      in
      let value = emit_expr t binding.value.init_expr in
      ignore
        (Llvm.build_store
           (emit_cast t value (expr_resolved_type t binding.value.init_expr) resolved)
           slot t.builder);
      add_symbol t binding.value.name.value
        (Variable_symbol
           {
             storage = slot;
             resolved_type = resolved;
             is_mutable = binding.value.mut;
           })
  | Core.Return expr ->
      let fn = current_function t in
      Option.iter
        (fun returned ->
          let value = emit_expr t returned in
          match fn.return_slot with
          | Some slot ->
              ignore
                (Llvm.build_store
                   (emit_cast t value (expr_resolved_type t returned)
                      fn.return_resolved_type)
                   slot t.builder)
          | None -> ())
        expr;
      emit_deferred_exprs t;
      emit_before_stmt_actions t stmt;
      ignore (Llvm.build_br fn.return_block t.builder);
      let dead_block = Llvm.append_block t.context "after.return" fn.fn_value in
      Llvm.position_at_end dead_block t.builder
  | Core.Defer expr ->
      let fn = current_function t in
      fn.defer_exprs_rev <- expr :: fn.defer_exprs_rev
  | Core.Loop loop ->
      emit_loop t stmt loop
  | Core.Break ->
      emit_before_stmt_actions t stmt;
      let _, break_block =
        match (current_function t).loop_stack with
        | loop :: _ -> loop
        | [] -> fail ~loc:stmt.loc "break used outside a loop during LLVM lowering"
      in
      ignore (Llvm.build_br break_block t.builder);
      let dead_block = Llvm.append_block t.context "after.break" (current_function t).fn_value in
      Llvm.position_at_end dead_block t.builder
  | Core.Continue ->
      emit_before_stmt_actions t stmt;
      let continue_block, _ =
        match (current_function t).loop_stack with
        | loop :: _ -> loop
        | [] -> fail ~loc:stmt.loc "continue used outside a loop during LLVM lowering"
      in
      ignore (Llvm.build_br continue_block t.builder);
      let dead_block =
        Llvm.append_block t.context "after.continue" (current_function t).fn_value
      in
      Llvm.position_at_end dead_block t.builder

and emit_loop t stmt (loop : Core.loop_stmt) =
  push_scope t;
  emit_statements t loop.value.init;
  let fn = current_function t in
  let cond_block = Llvm.append_block t.context "loop.cond" fn.fn_value in
  let body_block = Llvm.append_block t.context "loop.body" fn.fn_value in
  let step_block = Llvm.append_block t.context "loop.step" fn.fn_value in
  let end_block = Llvm.append_block t.context "loop.end" fn.fn_value in
  ignore (Llvm.build_br cond_block t.builder);
  fn.loop_stack <- (step_block, end_block) :: fn.loop_stack;
  Llvm.position_at_end cond_block t.builder;
  let cond_value = emit_expr t loop.value.cond in
  ignore (Llvm.build_cond_br (emit_to_bool t loop.value.cond cond_value) body_block end_block t.builder);
  Llvm.position_at_end body_block t.builder;
  emit_block_statements t loop.value.body;
  if not (current_block_terminated t) then ignore (Llvm.build_br step_block t.builder);
  Llvm.position_at_end step_block t.builder;
  emit_statements t loop.value.step;
  if not (current_block_terminated t) then ignore (Llvm.build_br cond_block t.builder);
  Llvm.position_at_end end_block t.builder;
  fn.loop_stack <- List.tl fn.loop_stack;
  emit_loop_exit_actions t stmt;
  pop_scope t

and emit_block_statements t (block : Core.block) =
  push_scope t;
  emit_statements t block.value.statements;
  if not (current_block_terminated t) then (
    Option.iter (fun expr -> ignore (emit_expr t expr)) block.value.result;
    emit_block_exit_actions t block);
  pop_scope t

let declare_function_symbol t (fn : Core.function_decl) =
  if fn.value.intrinsic <> None then
    fail ~loc:fn.loc "intrinsic lowering is not implemented yet"
  else
    let resolved = function_resolved_type t fn in
    match resolved with
    | Analysis.ResolvedFunction (params, ret, vararg) ->
        let fn_ty =
          if vararg then
            Llvm.var_arg_function_type (llvm_type_of_resolved t ret)
              (Array.of_list (List.map (llvm_type_of_resolved t) params))
          else
            Llvm.function_type (llvm_type_of_resolved t ret)
              (Array.of_list (List.map (llvm_type_of_resolved t) params))
        in
        let fn_value = Llvm.declare_function fn.value.name.value fn_ty t.llmodule in
        Llvm.set_linkage
          (if fn.value.public then Llvm.Linkage.External else Llvm.Linkage.Internal)
          fn_value;
        Llvm.set_function_call_conv Llvm.CallConv.c fn_value;
        let symbol =
          Function_symbol
            {
              fn = fn_value;
              fn_type = fn_ty;
              resolved_type = resolved;
              vararg;
              named_params = List.length params;
            }
        in
        Hashtbl.replace t.functions fn.value.name.value symbol;
        symbol
    | _ -> fail ~loc:fn.loc "function declaration did not resolve to function type"

let declare_global_symbol t (decl : Core.var_decl) =
  let resolved = resolved_type_of_core_type t decl.loc decl.value.ty in
  let storage =
    Llvm.declare_global (llvm_type_of_resolved t resolved) decl.value.name.value t.llmodule
  in
  Llvm.set_linkage
    (if decl.value.public then Llvm.Linkage.External else Llvm.Linkage.Internal)
    storage;
  Llvm.set_global_constant (not decl.value.is_mutable) storage;
  if decl.value.init_expr = None then Llvm.set_initializer (zero_constant t resolved) storage;
  let symbol =
    Variable_symbol
      { storage; resolved_type = resolved; is_mutable = decl.value.is_mutable }
  in
  Hashtbl.replace t.globals decl.value.name.value symbol;
  symbol

let rec declare_toplevel t (decl : Core.top_decl) =
  match decl.value with
  | Core.TDecl ty -> declare_type_decl t ty
  | Core.FDecl fn ->
      ignore (declare_function_symbol t fn)
  | Core.Foreign foreign ->
      List.iter
        (fun (fn : Core.function_decl) ->
          ignore (declare_function_symbol t { fn with value = { fn.value with definition = None } }))
        foreign.value.decls
  | Core.VDecl binding ->
      ignore (declare_global_symbol t binding)
  | Core.Import _ | Core.CImport _ -> ()

and declare_type_decl t (ty : Core.type_decl) =
  match ty.value.data with
  | Core.TypeDeclStruct decl ->
      ignore
        (llvm_struct_type t
           (Analysis.ResolvedNamed (ty.value.name.value, []))
           decl)
  | Core.TypeDeclEnum decl ->
      ignore
        (llvm_enum_type t
           (Analysis.ResolvedNamed (ty.value.name.value, []))
           decl)
  | Core.TypeDeclAlias _ | Core.TypeDeclForward -> ()

let lower_global_initializer t (decl : Core.var_decl) =
  match lookup_global_symbol t decl.value.name.value with
  | Variable_symbol { storage; resolved_type; _ } -> (
      match decl.value.init_expr with
      | None -> ()
      | Some init -> (
          match constant_of_expr t init with
          | Some constant ->
              Llvm.set_initializer constant storage
          | None ->
              Llvm.set_initializer (zero_constant t resolved_type) storage;
              t.global_inits_rev <- { decl; storage } :: t.global_inits_rev))
  | Function_symbol _ -> fail "global %s unexpectedly resolved to function" decl.value.name.value

let lower_globals t =
  List.iter
    (fun (decl : Core.top_decl) ->
      match decl.value with
      | Core.VDecl binding -> lower_global_initializer t binding
      | _ -> ())
    t.pipeline.cleaned.program.value.decls

let emit_global_ctor t =
  match List.rev t.global_inits_rev with
  | [] -> ()
  | inits ->
      let fn_ty = Llvm.function_type (void_type t) [||] in
      let fn = Llvm.declare_function "__haven_global_init" fn_ty t.llmodule in
      Llvm.set_linkage Llvm.Linkage.Internal fn;
      Llvm.set_function_call_conv Llvm.CallConv.c fn;
      let entry = Llvm.append_block t.context "entry" fn in
      let fn_state =
        {
          fn_decl =
            {
              value =
                {
                  public = false;
                  impure = true;
                  name = { value = "__haven_global_init"; loc = dummy_loc };
                  definition = None;
                  intrinsic = None;
                  params = { value = { params = []; vararg = false }; loc = dummy_loc };
                  return_type = Some { value = Core.VoidType; loc = dummy_loc };
                  vararg = false;
                };
              loc = dummy_loc;
            };
          fn_value = fn;
          entry_block = entry;
          return_block = Llvm.append_block t.context "return" fn;
          return_slot = None;
          return_resolved_type = Analysis.ResolvedVoid;
          scopes = [ String_map.empty ];
          defer_exprs_rev = [];
          loop_stack = [];
          emitting_defers = false;
        }
      in
      t.current_function <- Some fn_state;
      Llvm.position_at_end entry t.builder;
      List.iter
        (fun { decl; storage } ->
          match decl.value.init_expr with
          | Some init ->
              let value = emit_expr t init in
              let resolved = resolved_type_of_core_type t decl.loc decl.value.ty in
              ignore
                (Llvm.build_store
                   (emit_cast t value (expr_resolved_type t init) resolved)
                   storage t.builder)
          | None -> ())
        inits;
      ignore (Llvm.build_br fn_state.return_block t.builder);
      Llvm.position_at_end fn_state.return_block t.builder;
      ignore (Llvm.build_ret_void t.builder);
      t.current_function <- None;
      let ctor_ty = Llvm.struct_type t.context [| i32_type t; ptr_type t; ptr_type t |] in
      let entry =
        Llvm.const_struct t.context
          [|
            Llvm.const_int (i32_type t) 65535;
            Llvm.const_pointercast fn (ptr_type t);
            Llvm.const_null (ptr_type t);
          |]
      in
      let global =
        Llvm.define_global "llvm.global_ctors"
          (Llvm.const_array ctor_ty [| entry |]) t.llmodule
      in
      Llvm.set_linkage Llvm.Linkage.Appending global

let lower_function_body t (fn_decl : Core.function_decl) =
  let symbol_fn, symbol_resolved_type =
    match lookup_function_symbol t fn_decl.value.name.value with
    | Function_symbol { fn; resolved_type; _ } -> (fn, resolved_type)
    | Variable_symbol _ ->
        fail ~loc:fn_decl.loc "function symbol %s resolved to variable"
          fn_decl.value.name.value
  in
  match fn_decl.value.definition with
  | None -> ()
  | Some body -> (
      let entry =
        match Llvm.block_begin symbol_fn with
        | Llvm.Before block -> block
        | Llvm.At_end _ -> Llvm.append_block t.context "entry" symbol_fn
      in
      let return_block = Llvm.append_block t.context "return" symbol_fn in
      Llvm.position_at_end entry t.builder;
      let return_resolved_type =
        match symbol_resolved_type with
        | Analysis.ResolvedFunction (_, ret, _) -> ret
        | _ -> Analysis.ResolvedVoid
      in
      let return_slot =
        match return_resolved_type with
        | Analysis.ResolvedVoid -> None
        | ret ->
            Some (Llvm.build_alloca (llvm_type_of_resolved t ret) "retval" t.builder)
      in
      let fn_state =
        {
          fn_decl;
          fn_value = symbol_fn;
          entry_block = entry;
          return_block;
          return_slot;
          return_resolved_type;
          scopes = [ String_map.empty ];
          defer_exprs_rev = [];
          loop_stack = [];
          emitting_defers = false;
        }
      in
      t.current_function <- Some fn_state;
      Llvm.position_at_end entry t.builder;
      List.iteri
        (fun index (param : Core.param) ->
          let resolved = resolved_type_of_core_type t param.loc param.value.ty in
          let slot =
            build_alloca t (llvm_type_of_resolved t resolved) param.value.name.value
          in
          let param_value = Llvm.param symbol_fn index in
          ignore (Llvm.build_store param_value slot t.builder);
          add_symbol t param.value.name.value
            (Variable_symbol { storage = slot; resolved_type = resolved; is_mutable = false }))
        fn_decl.value.params.value.params;
      push_scope t;
      emit_statements t body.value.statements;
      if not (current_block_terminated t) then (
        Option.iter
          (fun result ->
            let value = emit_expr t result in
            match return_slot with
            | Some slot ->
                ignore
                  (Llvm.build_store
                     (emit_cast t value (expr_resolved_type t result) return_resolved_type)
                     slot t.builder)
            | None -> ())
          body.value.result;
        emit_deferred_exprs t;
        emit_block_exit_actions t body;
        pop_scope t;
        emit_function_exit_actions t fn_decl;
        ignore (Llvm.build_br return_block t.builder));
      if List.length (current_scopes t) > 1 then pop_scope t;
      Llvm.position_at_end return_block t.builder;
      (match return_slot with
      | Some slot ->
          ignore
            (Llvm.build_ret
               (Llvm.build_load (llvm_type_of_resolved t return_resolved_type) slot "retval"
                  t.builder)
               t.builder)
      | None -> ignore (Llvm.build_ret_void t.builder));
      t.current_function <- None)

let lower_functions t =
  List.iter
    (fun (decl : Core.top_decl) ->
      match decl.value with
      | Core.FDecl fn -> lower_function_body t fn
      | Core.Foreign _ -> ()
      | Core.TDecl _ | Core.VDecl _ | Core.Import _ | Core.CImport _ -> ())
    t.pipeline.cleaned.program.value.decls

let verify_and_run_passes t =
  match Llvm_analysis.verify_module t.llmodule with
  | Some reason -> fail "LLVM module verification failed:\n%s" reason
  | None ->
      let opts = Llvm_passbuilder.create_passbuilder_options () in
      let result =
        Llvm_passbuilder.run_passes t.llmodule "globaldce,default<O0>" t.target_machine opts
      in
      Llvm_passbuilder.dispose_passbuilder_options opts;
      (match result with Ok () -> () | Error msg -> fail "LLVM pass pipeline failed: %s" msg)

let emit_module pipeline =
  let t = create_context pipeline in
  List.iter (declare_toplevel t) pipeline.cleaned.program.value.decls;
  lower_globals t;
  emit_global_ctor t;
  lower_functions t;
  verify_and_run_passes t;
  t.llmodule

let emit_ir_string pipeline = Llvm.string_of_llmodule (emit_module pipeline)
