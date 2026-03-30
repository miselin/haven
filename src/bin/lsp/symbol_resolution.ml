module Analysis = Haven.Ast.Analysis
module Core = Analysis.Core
module Pretty = Haven.Ast.Pretty
module String_map = Map.Make (String)

type binding = {
  hover_text : string;
  definition_loc : Haven_core.Loc.t;
}

type candidate = {
  loc : Haven_core.Loc.t;
  priority : int;
  hover_text : string option;
  definition_loc : Haven_core.Loc.t option;
}

type resolution = {
  loc : Haven_core.Loc.t;
  hover_text : string option;
  definition_loc : Haven_core.Loc.t option;
}

type highlight_kind = [ `Read | `Text | `Write ]

type env = binding String_map.t list

type state = {
  position : Lexing.position;
  typing : Analysis.typing_result;
  type_env : Analysis.type_env;
  root_env : binding String_map.t;
  type_decls : Core.type_decl String_map.t;
  mutable best : candidate option;
}

let range_size (loc : Haven_core.Loc.t) =
  loc.end_pos.Lexing.pos_cnum - loc.start_pos.Lexing.pos_cnum

let better_candidate (left : candidate) (right : candidate) =
  let left_size = range_size left.loc in
  let right_size = range_size right.loc in
  left_size < right_size || (left_size = right_size && left.priority > right.priority)

let maybe_pick state (candidate : candidate) =
  if Haven_core.Loc.contains_position candidate.loc state.position then
    match state.best with
    | None -> state.best <- Some candidate
    | Some current when better_candidate candidate current -> state.best <- Some candidate
    | Some _ -> ()

let hover_candidate loc priority contents =
  { loc; priority; hover_text = Some contents; definition_loc = None }

let binding_candidate loc priority (binding : binding) =
  {
    loc;
    priority;
    hover_text = Some binding.hover_text;
    definition_loc = Some binding.definition_loc;
  }

let maybe_pick_binding state loc priority binding =
  maybe_pick state (binding_candidate loc priority binding)

let push_scope (env : env) = String_map.empty :: env

let bind_current (env : env) name binding =
  match env with
  | [] -> [ String_map.singleton name binding ]
  | scope :: rest -> String_map.add name binding scope :: rest

let rec lookup (env : env) name =
  match env with
  | [] -> None
  | scope :: rest -> (
      match String_map.find_opt name scope with
      | Some binding -> Some binding
      | None -> lookup rest name)

let format_core_type (ty : Core.haven_type) =
  Format.asprintf "%a" Pretty.pp_core_type ty

let format_resolved_type loc resolved =
  format_core_type (Analysis.core_type_of_resolved_ty loc resolved)

let expr_annotation typing (expr : Core.expression) =
  Hashtbl.find_opt typing.Analysis.annotations.exprs (Analysis.expr_id expr)

let binding_annotation typing (binding : Core.let_stmt) =
  Hashtbl.find_opt typing.Analysis.annotations.bindings (Analysis.binding_id binding)

let type_summary loc inferred resolved =
  match (inferred, resolved) with
  | Some inferred, Some resolved ->
      let resolved_text = format_resolved_type loc resolved in
      let inferred_text = format_core_type inferred in
      if String.equal inferred_text resolved_text then inferred_text
      else Printf.sprintf "inferred %s\nresolved %s" inferred_text resolved_text
  | Some inferred, None -> format_core_type inferred
  | None, Some resolved -> format_resolved_type loc resolved
  | None, None -> "unknown"

let function_signature (fn : Core.function_decl) =
  let params =
    let params =
      List.map
        (fun (param : Core.param) ->
          Printf.sprintf "%s: %s" param.value.name.value
            (format_core_type param.value.ty))
        fn.value.params.value.params
    in
    if fn.value.vararg then params @ [ "..." ] else params
  in
  let prefix =
    String.concat " "
      (List.filter
         (fun part -> not (String.equal part ""))
         [
           if fn.value.public then "pub" else "";
           if fn.value.impure then "impure" else "";
           "fn";
         ])
  in
  let return_type =
    match fn.value.return_type with
    | Some ty -> format_core_type ty
    | None -> "void"
  in
  Printf.sprintf "%s %s(%s) -> %s" prefix fn.value.name.value
    (String.concat ", " params) return_type

let type_decl_summary (decl : Core.type_decl) =
  match decl.value.data with
  | Core.TypeDeclAlias ty ->
      Printf.sprintf "type %s = %s" decl.value.name.value (format_core_type ty)
  | Core.TypeDeclStruct _ -> Printf.sprintf "type %s = struct" decl.value.name.value
  | Core.TypeDeclEnum enum_decl ->
      let generics =
        match enum_decl.value.generics with
        | [] -> ""
        | generics ->
            Printf.sprintf "<%s>"
              (String.concat ", "
                 (List.map (fun (id : Core.identifier) -> id.value) generics))
      in
      Printf.sprintf "type %s = enum%s" decl.value.name.value generics
  | Core.TypeDeclForward -> Printf.sprintf "type %s" decl.value.name.value

let hover_block text = Printf.sprintf "```haven\n%s\n```" text

let make_binding hover_text definition_loc = { hover_text; definition_loc }

let expression_contents typing (expr : Core.expression) =
  match expr_annotation typing expr with
  | None -> None
  | Some annotation ->
      Some
        (hover_block
           (Printf.sprintf "expr: %s"
              (type_summary expr.loc annotation.inferred_type annotation.resolved_type)))

let binding_contents typing (binding : Core.let_stmt) =
  let annotation = binding_annotation typing binding in
  let explicit_type =
    match binding.value.ty with
    | Some ty -> Some (format_core_type ty)
    | None -> None
  in
  let inferred_type =
    match annotation with
    | Some ann ->
        Some (type_summary binding.loc ann.inferred_type ann.resolved_type)
    | None -> None
  in
  let ty =
    match (explicit_type, inferred_type) with
    | Some ty, _ -> ty
    | None, Some ty -> ty
    | None, None -> "unknown"
  in
  hover_block
    (Printf.sprintf "let%s %s: %s" (if binding.value.mut then " mut" else "")
       binding.value.name.value ty)

let global_contents (decl : Core.var_decl) =
  hover_block
    (Printf.sprintf "global%s%s %s: %s"
       (if decl.value.public then " pub" else "")
       (if decl.value.is_mutable then " mut" else "")
       decl.value.name.value
       (format_core_type decl.value.ty))

let parameter_contents (param : Core.param) =
  hover_block
    (Printf.sprintf "param %s: %s" param.value.name.value
       (format_core_type param.value.ty))

let type_contents (ty : Core.haven_type) = hover_block (format_core_type ty)

let struct_field_contents (field : Core.struct_field) =
  hover_block
    (Printf.sprintf "field %s: %s" field.value.name.value
       (format_core_type field.value.ty))

let enum_variant_contents (variant : Core.enum_variant) =
  let payload =
    match variant.value.inner_tys with
    | [] -> ""
    | tys ->
        Printf.sprintf "(%s)"
          (String.concat ", " (List.map format_core_type tys))
  in
  hover_block (Printf.sprintf "variant %s%s" variant.value.name.value payload)

let pattern_binding_contents loc name resolved_ty =
  match resolved_ty with
  | Some ty ->
      hover_block
        (Printf.sprintf "pattern %s: %s" name (format_resolved_type loc ty))
  | None -> hover_block (Printf.sprintf "pattern %s" name)

let rec bind_pattern_payloads bindings payload_tys f acc =
  match bindings with
  | [] -> acc
  | binding :: rest ->
      let payload_ty, rest_payload_tys =
        match payload_tys with
        | payload_ty :: rest_payload_tys -> (Some payload_ty, rest_payload_tys)
        | [] -> (None, [])
      in
      bind_pattern_payloads rest rest_payload_tys f (f acc binding payload_ty)

let root_bindings (typing : Analysis.typing_result) =
  List.fold_left
    (fun env (decl : Core.top_decl) ->
      match decl.value with
      | Core.FDecl fn ->
          String_map.add fn.value.name.value
            (make_binding (hover_block (function_signature fn)) fn.value.name.loc)
            env
      | Core.Foreign foreign ->
          List.fold_left
            (fun env (fn : Core.function_decl) ->
              String_map.add fn.value.name.value
                (make_binding (hover_block (function_signature fn))
                   fn.value.name.loc)
                env)
            env foreign.value.decls
      | Core.VDecl var_decl ->
          String_map.add var_decl.value.name.value
            (make_binding (global_contents var_decl) var_decl.value.name.loc)
            env
      | Core.TDecl _ | Core.Import _ | Core.CImport _ ->
          env)
    String_map.empty typing.program.program.value.decls

let type_decls (typing : Analysis.typing_result) =
  List.fold_left
    (fun decls (decl : Core.top_decl) ->
      match decl.value with
      | Core.TDecl type_decl ->
          String_map.add type_decl.value.name.value type_decl decls
      | Core.FDecl _ | Core.VDecl _ | Core.Import _ | Core.CImport _
      | Core.Foreign _ ->
          decls)
    String_map.empty typing.program.program.value.decls

let maybe_pick_type_decl state id priority =
  Option.iter
    (fun decl ->
      maybe_pick_binding state id.Core.loc priority
        (make_binding (hover_block (type_decl_summary decl)) decl.value.name.loc))
    (String_map.find_opt id.value state.type_decls)

let rec walk_type state (ty : Core.haven_type) =
  maybe_pick state (hover_candidate ty.loc 20 (type_contents ty));
  match ty.value with
  | Core.CellType inner | PointerType inner | BoxType inner -> walk_type state inner
  | Core.ArrayType arr ->
      walk_type state arr.value.element
  | Core.FunctionType fn_ty ->
      List.iter (walk_type state) fn_ty.value.param_types;
      walk_type state fn_ty.value.return_type
  | Core.TemplatedType templ ->
      maybe_pick_type_decl state templ.value.outer 30;
      List.iter (walk_type state) templ.value.inner
  | Core.CustomType custom ->
      maybe_pick_type_decl state custom.name 30
  | NumericType _ | VecType _ | MatrixType _ | FloatType | VoidType | StringType ->
      ()

and walk_match_pattern state (pattern : Core.match_pattern) =
  match pattern.value with
  | Core.PatternDefault ->
      maybe_pick state (hover_candidate pattern.loc 30 (hover_block "pattern _"))
  | PatternLiteral _ -> ()
  | Core.PatternEnum enum ->
      maybe_pick state
        (hover_candidate enum.value.enum_variant.loc 30
           (hover_block
              (Printf.sprintf "pattern %s" enum.value.enum_variant.value)));
      Option.iter (fun id -> maybe_pick_type_decl state id 35) enum.value.enum_name

and bind_pattern state env scrutinee_resolved (pattern : Core.match_pattern) =
  let env = push_scope env in
  walk_match_pattern state pattern;
  match pattern.value with
  | Core.PatternDefault | PatternLiteral _ ->
      env
  | Core.PatternEnum enum ->
      let resolved_variant =
        Option.bind scrutinee_resolved (fun resolved ->
            Analysis.lookup_enum_variant state.type_env pattern.loc resolved
              enum.value.enum_variant.value)
      in
      Option.iter
        (fun (variant, _inner_ty) ->
          maybe_pick_binding state enum.value.enum_variant.loc 35
            (make_binding (enum_variant_contents variant) variant.value.name.loc))
        resolved_variant;
      let payload_tys =
        Option.value ~default:[]
          (Option.map (fun (_variant, inner_tys) -> inner_tys) resolved_variant)
      in
      bind_pattern_payloads enum.value.binding payload_tys
        (fun env (binding : Core.pattern_binding) payload_ty ->
          match binding.value with
          | Core.BindingIgnored ->
              env
          | Core.BindingNamed id ->
              let binding =
                make_binding
                  (pattern_binding_contents id.loc id.value payload_ty)
                  id.loc
              in
              maybe_pick_binding state id.loc 40 binding;
              bind_current env id.value binding)
        env

and enum_literal_hover state expr (enum_lit : Core.enum_literal) =
  let expr_resolved_type =
    match expr_annotation state.typing expr with
    | None -> None
    | Some annotation -> annotation.resolved_type
  in
  maybe_pick_type_decl state enum_lit.value.enum_name 35;
  Option.iter
    (fun (variant, _inner_ty) ->
      maybe_pick_binding state enum_lit.value.enum_variant.loc 35
        (make_binding (enum_variant_contents variant) variant.value.name.loc))
    (Option.bind expr_resolved_type (fun resolved ->
         Analysis.lookup_enum_variant state.type_env expr.loc resolved
           enum_lit.value.enum_variant.value))

and walk_expression state env (expr : Core.expression) =
  Option.iter
    (fun contents -> maybe_pick state (hover_candidate expr.loc 10 contents))
    (expression_contents state.typing expr);
  match expr.value with
  | Core.Binary binary ->
      walk_expression state env binary.value.left;
      walk_expression state env binary.value.right
  | Unary unary ->
      walk_expression state env unary.value.inner
  | Block block ->
      ignore (walk_block state env block)
  | Identifier id ->
      Option.iter
        (fun binding -> maybe_pick_binding state id.loc 35 binding)
        (lookup env id.value)
  | Literal literal -> (
      match literal.value with
      | Core.Enum enum_lit -> enum_literal_hover state expr enum_lit
      | _ -> ())
  | Nil -> ()
  | ToBool inner | SizeExpr inner | BoxExpr inner | Unbox inner | Ref inner | Load inner ->
      walk_expression state env inner
  | Initializer init ->
      List.iter (walk_expression state env) init.value.exprs
  | As cast ->
      walk_type state cast.value.target_type;
      walk_expression state env cast.value.inner
  | SizeType ty | BoxType ty ->
      walk_type state ty
  | Match match_expr ->
      walk_expression state env match_expr.value.expr;
      let scrutinee_resolved =
        match expr_annotation state.typing match_expr.value.expr with
        | Some annotation -> annotation.resolved_type
        | None -> None
      in
      List.iter
        (fun (arm : Core.match_arm) ->
          let arm_env = bind_pattern state env scrutinee_resolved arm.value.pattern in
          walk_expression state arm_env arm.value.expr)
        match_expr.value.arms
  | Call call ->
      walk_expression state env call.value.target;
      List.iter (walk_expression state env) call.value.params
  | Index index ->
      walk_expression state env index.value.target;
      walk_expression state env index.value.index
  | Field field ->
      maybe_pick state
        (hover_candidate field.value.field.loc 25
           (hover_block (Printf.sprintf "field %s" field.value.field.value)));
      walk_expression state env field.value.target
  | Assign write | Mutate write ->
      walk_expression state env write.value.target;
      walk_expression state env write.value.value

and walk_statement state env (stmt : Core.statement) =
  match stmt.value with
  | Core.Expression expr ->
      walk_expression state env expr;
      env
  | Let binding ->
      let binding_info =
        make_binding (binding_contents state.typing binding) binding.value.name.loc
      in
      maybe_pick_binding state binding.value.name.loc 40 binding_info;
      Option.iter (walk_type state) binding.value.ty;
      walk_expression state env binding.value.init_expr;
      bind_current env binding.value.name.value binding_info
  | Return expr ->
      Option.iter (walk_expression state env) expr;
      env
  | Defer expr ->
      walk_expression state env expr;
      env
  | Loop loop ->
      let loop_env =
        List.fold_left (walk_statement state) (push_scope env) loop.value.init
      in
      walk_expression state loop_env loop.value.cond;
      ignore (walk_block state loop_env loop.value.body);
      ignore (List.fold_left (walk_statement state) loop_env loop.value.step);
      env
  | Break | Continue ->
      env

and walk_block state env (block : Core.block) =
  let env = push_scope env in
  let env = List.fold_left (walk_statement state) env block.value.statements in
  Option.iter (walk_expression state env) block.value.result;
  env

let walk_function state (fn : Core.function_decl) =
  maybe_pick_binding state fn.value.name.loc 50
    (make_binding (hover_block (function_signature fn)) fn.value.name.loc);
  let env =
    List.fold_left
      (fun env (param : Core.param) ->
        let binding =
          make_binding (parameter_contents param) param.value.name.loc
        in
        maybe_pick_binding state param.value.name.loc 45 binding;
        walk_type state param.value.ty;
        bind_current env param.value.name.value binding)
      [ state.root_env ] fn.value.params.value.params
  in
  Option.iter
    (fun (intrinsic : Core.intrinsic) ->
      List.iter (walk_type state) intrinsic.value.types)
    fn.value.intrinsic;
  Option.iter (walk_type state) fn.value.return_type;
  Option.iter (fun block -> ignore (walk_block state env block)) fn.value.definition

let walk_type_decl state (decl : Core.type_decl) =
  maybe_pick_binding state decl.value.name.loc 50
    (make_binding (hover_block (type_decl_summary decl)) decl.value.name.loc);
  match decl.value.data with
  | Core.TypeDeclAlias ty ->
      walk_type state ty
  | TypeDeclStruct struct_decl ->
      List.iter
        (fun (field : Core.struct_field) ->
          maybe_pick_binding state field.value.name.loc 40
            (make_binding (struct_field_contents field) field.value.name.loc);
          walk_type state field.value.ty)
        struct_decl.value.fields
  | TypeDeclEnum enum_decl ->
      List.iter
        (fun (variant : Core.enum_variant) ->
          maybe_pick_binding state variant.value.name.loc 40
            (make_binding (enum_variant_contents variant) variant.value.name.loc);
          List.iter (walk_type state) variant.value.inner_tys)
        enum_decl.value.variants
  | TypeDeclForward -> ()

let walk_global state (decl : Core.var_decl) =
  maybe_pick_binding state decl.value.name.loc 50
    (make_binding (global_contents decl) decl.value.name.loc);
  walk_type state decl.value.ty;
  Option.iter (walk_expression state [ state.root_env ]) decl.value.init_expr

let walk_top_decl state (decl : Core.top_decl) =
  match decl.value with
  | Core.FDecl fn ->
      walk_function state fn
  | TDecl ty_decl ->
      walk_type_decl state ty_decl
  | VDecl var_decl ->
      walk_global state var_decl
  | Foreign foreign ->
      List.iter (walk_function state) foreign.value.decls
  | Import _ | CImport _ -> ()

let resolve_at (typing : Analysis.typing_result) position : resolution option =
  let state =
    {
      position;
      typing;
      type_env = Analysis.type_env_of_program typing.program.program;
      root_env = root_bindings typing;
      type_decls = type_decls typing;
      best = None;
    }
  in
  List.iter (walk_top_decl state) typing.program.program.value.decls;
  Option.map
    (fun (candidate : candidate) ->
      {
        loc = candidate.loc;
        hover_text = candidate.hover_text;
        definition_loc = candidate.definition_loc;
      })
    state.best

let hover_text_at typing position =
  Option.bind (resolve_at typing position) (fun resolution ->
      Option.map (fun hover_text -> resolution.loc, hover_text) resolution.hover_text)

let definition_at typing position =
  Option.bind (resolve_at typing position) (fun resolution -> resolution.definition_loc)

let same_loc (left : Haven_core.Loc.t) (right : Haven_core.Loc.t) =
  let left_start = left.start_pos in
  let left_end = left.end_pos in
  let right_start = right.start_pos in
  let right_end = right.end_pos in
  String.equal left_start.pos_fname right_start.pos_fname
  && left_start.pos_lnum = right_start.pos_lnum
  && left_start.pos_cnum = right_start.pos_cnum
  && left_end.pos_lnum = right_end.pos_lnum
  && left_end.pos_cnum = right_end.pos_cnum

type highlight = {
  loc : Haven_core.Loc.t;
  kind : highlight_kind;
}

type highlight_state = {
  target_loc : Haven_core.Loc.t;
  current_file : string;
  typing : Analysis.typing_result;
  type_env : Analysis.type_env;
  root_env : binding String_map.t;
  type_decls : Core.type_decl String_map.t;
  mutable highlights_rev : highlight list;
}

let add_highlight state loc kind =
  if String.equal loc.Haven_core.Loc.start_pos.pos_fname state.current_file then
    let already_present =
      List.exists
        (fun (highlight : highlight) ->
          same_loc highlight.loc loc
          &&
          match (highlight.kind, kind) with
          | `Write, `Read -> false
          | `Write, `Text -> false
          | _ -> highlight.kind = kind)
        state.highlights_rev
    in
    if not already_present then
      state.highlights_rev <- { loc; kind } :: state.highlights_rev

let maybe_add_binding_highlight state loc kind (binding : binding) =
  if same_loc binding.definition_loc state.target_loc then add_highlight state loc kind

let maybe_add_type_decl_highlight state kind (id : Core.identifier) =
  match String_map.find_opt id.value state.type_decls with
  | None -> ()
  | Some decl ->
      if same_loc decl.value.name.loc state.target_loc then
        add_highlight state id.loc kind

let rec highlight_type state (ty : Core.haven_type) =
  match ty.value with
  | Core.CellType inner | PointerType inner | BoxType inner ->
      highlight_type state inner
  | Core.ArrayType arr ->
      highlight_type state arr.value.element
  | Core.FunctionType fn_ty ->
      List.iter (highlight_type state) fn_ty.value.param_types;
      highlight_type state fn_ty.value.return_type
  | Core.TemplatedType templ ->
      maybe_add_type_decl_highlight state `Text templ.value.outer;
      List.iter (highlight_type state) templ.value.inner
  | Core.CustomType custom ->
      maybe_add_type_decl_highlight state `Text custom.name
  | NumericType _ | VecType _ | MatrixType _ | FloatType | VoidType | StringType ->
      ()

let highlight_match_pattern state env scrutinee_resolved
    (pattern : Core.match_pattern) =
  match pattern.value with
  | Core.PatternDefault | PatternLiteral _ ->
      env
  | Core.PatternEnum enum ->
      let resolved_variant =
        Option.bind scrutinee_resolved (fun resolved ->
            Analysis.lookup_enum_variant state.type_env pattern.loc resolved
              enum.value.enum_variant.value)
      in
      Option.iter
        (fun ((variant : Core.enum_variant), _inner_ty) ->
          if same_loc variant.value.name.loc state.target_loc then
            add_highlight state enum.value.enum_variant.loc `Text)
        resolved_variant;
      let payload_tys =
        Option.value ~default:[]
          (Option.map (fun (_variant, inner_tys) -> inner_tys) resolved_variant)
      in
      bind_pattern_payloads enum.value.binding payload_tys
        (fun env (binding : Core.pattern_binding) payload_ty ->
          match binding.value with
          | Core.BindingIgnored ->
              env
          | Core.BindingNamed id ->
              let binding_info =
                make_binding
                  (pattern_binding_contents id.loc id.value payload_ty)
                  id.loc
              in
              maybe_add_binding_highlight state id.loc `Write binding_info;
              bind_current env id.value binding_info)
        (push_scope env)

let rec highlight_expression state env (expr : Core.expression) =
  match expr.value with
  | Core.Binary binary ->
      highlight_expression state env binary.value.left;
      highlight_expression state env binary.value.right
  | Unary unary ->
      highlight_expression state env unary.value.inner
  | Block block ->
      ignore (highlight_block state env block)
  | Identifier id ->
      Option.iter
        (maybe_add_binding_highlight state id.loc `Read)
        (lookup env id.value)
  | Literal literal -> (
      match literal.value with
      | Core.Enum enum_lit ->
          maybe_add_type_decl_highlight state `Text enum_lit.value.enum_name;
          let expr_resolved_type =
            match expr_annotation state.typing expr with
            | None -> None
            | Some annotation -> annotation.resolved_type
          in
          Option.iter
            (fun ((variant : Core.enum_variant), _inner_ty) ->
              if same_loc variant.value.name.loc state.target_loc then
                add_highlight state enum_lit.value.enum_variant.loc `Text)
            (Option.bind expr_resolved_type (fun resolved ->
                 Analysis.lookup_enum_variant state.type_env expr.loc resolved
                   enum_lit.value.enum_variant.value))
      | _ -> ())
  | Nil -> ()
  | ToBool inner | SizeExpr inner | BoxExpr inner | Unbox inner | Ref inner | Load inner ->
      highlight_expression state env inner
  | Initializer init ->
      List.iter (highlight_expression state env) init.value.exprs
  | As cast ->
      highlight_type state cast.value.target_type;
      highlight_expression state env cast.value.inner
  | SizeType ty | BoxType ty ->
      highlight_type state ty
  | Match match_expr ->
      highlight_expression state env match_expr.value.expr;
      let scrutinee_resolved =
        match expr_annotation state.typing match_expr.value.expr with
        | Some annotation -> annotation.resolved_type
        | None -> None
      in
      List.iter
        (fun (arm : Core.match_arm) ->
          let arm_env =
            highlight_match_pattern state env scrutinee_resolved arm.value.pattern
          in
          highlight_expression state arm_env arm.value.expr)
        match_expr.value.arms
  | Call call ->
      highlight_expression state env call.value.target;
      List.iter (highlight_expression state env) call.value.params
  | Index index ->
      highlight_expression state env index.value.target;
      highlight_expression state env index.value.index
  | Field field ->
      highlight_expression state env field.value.target
  | Assign write | Mutate write -> (
      match write.value.target.value with
      | Core.Identifier id ->
          Option.iter
            (maybe_add_binding_highlight state id.loc `Write)
            (lookup env id.value)
      | _ -> highlight_expression state env write.value.target);
      highlight_expression state env write.value.value

and highlight_statement state env (stmt : Core.statement) =
  match stmt.value with
  | Core.Expression expr ->
      highlight_expression state env expr;
      env
  | Let binding ->
      let binding_info =
        make_binding (binding_contents state.typing binding) binding.value.name.loc
      in
      maybe_add_binding_highlight state binding.value.name.loc `Write binding_info;
      Option.iter (highlight_type state) binding.value.ty;
      highlight_expression state env binding.value.init_expr;
      bind_current env binding.value.name.value binding_info
  | Return expr ->
      Option.iter (highlight_expression state env) expr;
      env
  | Defer expr ->
      highlight_expression state env expr;
      env
  | Loop loop ->
      let loop_env =
        List.fold_left (highlight_statement state) (push_scope env) loop.value.init
      in
      highlight_expression state loop_env loop.value.cond;
      ignore (highlight_block state loop_env loop.value.body);
      ignore (List.fold_left (highlight_statement state) loop_env loop.value.step);
      env
  | Break | Continue ->
      env

and highlight_block state env (block : Core.block) =
  let env = push_scope env in
  let env =
    List.fold_left (highlight_statement state) env block.value.statements
  in
  Option.iter (highlight_expression state env) block.value.result;
  env

let highlight_function state (fn : Core.function_decl) =
  if same_loc fn.value.name.loc state.target_loc then
    add_highlight state fn.value.name.loc `Write;
  let env =
    List.fold_left
      (fun env (param : Core.param) ->
        let binding =
          make_binding (parameter_contents param) param.value.name.loc
        in
        maybe_add_binding_highlight state param.value.name.loc `Write binding;
        highlight_type state param.value.ty;
        bind_current env param.value.name.value binding)
      [ state.root_env ] fn.value.params.value.params
  in
  Option.iter
    (fun (intrinsic : Core.intrinsic) ->
      List.iter (highlight_type state) intrinsic.value.types)
    fn.value.intrinsic;
  Option.iter (highlight_type state) fn.value.return_type;
  Option.iter (fun block -> ignore (highlight_block state env block)) fn.value.definition

let highlight_type_decl state (decl : Core.type_decl) =
  if same_loc decl.value.name.loc state.target_loc then
    add_highlight state decl.value.name.loc `Text;
  match decl.value.data with
  | Core.TypeDeclAlias ty ->
      highlight_type state ty
  | TypeDeclStruct struct_decl ->
      List.iter
        (fun (field : Core.struct_field) ->
          highlight_type state field.value.ty)
        struct_decl.value.fields
  | TypeDeclEnum enum_decl ->
      List.iter
        (fun (variant : Core.enum_variant) ->
          if same_loc variant.value.name.loc state.target_loc then
            add_highlight state variant.value.name.loc `Text;
          List.iter (highlight_type state) variant.value.inner_tys)
        enum_decl.value.variants
  | TypeDeclForward -> ()

let highlight_global state (decl : Core.var_decl) =
  let binding = make_binding (global_contents decl) decl.value.name.loc in
  maybe_add_binding_highlight state decl.value.name.loc `Write binding;
  highlight_type state decl.value.ty;
  Option.iter (highlight_expression state [ state.root_env ]) decl.value.init_expr

let highlight_top_decl state (decl : Core.top_decl) =
  match decl.value with
  | Core.FDecl fn ->
      highlight_function state fn
  | TDecl ty_decl ->
      highlight_type_decl state ty_decl
  | VDecl var_decl ->
      highlight_global state var_decl
  | Foreign foreign ->
      List.iter (highlight_function state) foreign.value.decls
  | Import _ | CImport _ -> ()

let highlights_at (typing : Analysis.typing_result) position =
  match definition_at typing position with
  | None -> []
  | Some target_loc ->
      let state =
        {
          target_loc;
          current_file = position.pos_fname;
          typing;
          type_env = Analysis.type_env_of_program typing.program.program;
          root_env = root_bindings typing;
          type_decls = type_decls typing;
          highlights_rev = [];
        }
      in
      List.iter (highlight_top_decl state) typing.program.program.value.decls;
      List.rev state.highlights_rev
