module Analysis = Haven.Ast.Analysis
module Core = Analysis.Core
module Pretty = Haven.Ast.Pretty

type candidate = {
  loc : Haven_core.Loc.t;
  priority : int;
  contents : string;
}

type state = {
  position : Lexing.position;
  typing : Analysis.typing_result;
  mutable best : candidate option;
}

let range_size (loc : Haven_core.Loc.t) =
  loc.end_pos.Lexing.pos_cnum - loc.start_pos.Lexing.pos_cnum

let better_candidate left right =
  let left_size = range_size left.loc in
  let right_size = range_size right.loc in
  left_size < right_size || (left_size = right_size && left.priority > right.priority)

let maybe_pick state candidate =
  if Haven_core.Loc.contains_position candidate.loc state.position then
    match state.best with
    | None -> state.best <- Some candidate
    | Some current when better_candidate candidate current -> state.best <- Some candidate
    | Some _ -> ()

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
      (List.filter (fun part -> not (String.equal part ""))
         [ if fn.value.public then "pub" else "";
           if fn.value.impure then "impure" else "";
           "fn" ])
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

let type_contents (ty : Core.haven_type) =
  hover_block (format_core_type ty)

let struct_field_contents (field : Core.struct_field) =
  hover_block
    (Printf.sprintf "field %s: %s" field.value.name.value
       (format_core_type field.value.ty))

let enum_variant_contents (variant : Core.enum_variant) =
  let payload =
    match variant.value.inner_ty with
    | None -> ""
    | Some ty -> Printf.sprintf "(%s)" (format_core_type ty)
  in
  hover_block (Printf.sprintf "variant %s%s" variant.value.name.value payload)

let rec walk_type state (ty : Core.haven_type) =
  maybe_pick state { loc = ty.loc; priority = 20; contents = type_contents ty };
  match ty.value with
  | Core.CellType inner | PointerType inner | BoxType inner -> walk_type state inner
  | Core.ArrayType arr ->
      walk_type state arr.value.element
  | Core.FunctionType fn_ty ->
      List.iter (walk_type state) fn_ty.value.param_types;
      walk_type state fn_ty.value.return_type
  | Core.TemplatedType templ ->
      List.iter (walk_type state) templ.value.inner
  | NumericType _ | VecType _ | MatrixType _ | FloatType | VoidType | StringType
  | CustomType _ ->
      ()

and walk_match_pattern state (pattern : Core.match_pattern) =
  match pattern.value with
  | Core.PatternDefault | PatternLiteral _ -> ()
  | Core.PatternEnum enum ->
      maybe_pick state
        {
          loc = enum.value.enum_variant.loc;
          priority = 30;
          contents = hover_block (Printf.sprintf "pattern %s" enum.value.enum_variant.value);
        }

and walk_expression state (expr : Core.expression) =
  Option.iter
    (fun contents -> maybe_pick state { loc = expr.loc; priority = 10; contents })
    (expression_contents state.typing expr);
  match expr.value with
  | Core.Binary binary ->
      walk_expression state binary.value.left;
      walk_expression state binary.value.right
  | Unary unary ->
      walk_expression state unary.value.inner
  | Block block ->
      walk_block state block
  | Identifier _ | Literal _ | Nil -> ()
  | ToBool inner | SizeExpr inner | BoxExpr inner | Unbox inner | Ref inner
  | Load inner ->
      walk_expression state inner
  | Initializer init ->
      List.iter (walk_expression state) init.value.exprs
  | As cast ->
      walk_type state cast.value.target_type;
      walk_expression state cast.value.inner
  | SizeType ty | BoxType ty ->
      walk_type state ty
  | Match match_expr ->
      walk_expression state match_expr.value.expr;
      List.iter
        (fun (arm : Core.match_arm) ->
          walk_match_pattern state arm.value.pattern;
          walk_expression state arm.value.expr)
        match_expr.value.arms
  | Call call ->
      walk_expression state call.value.target;
      List.iter (walk_expression state) call.value.params
  | Index index ->
      walk_expression state index.value.target;
      walk_expression state index.value.index
  | Field field ->
      maybe_pick state
        {
          loc = field.value.field.loc;
          priority = 25;
          contents = hover_block (Printf.sprintf "field %s" field.value.field.value);
        };
      walk_expression state field.value.target
  | Assign write | Mutate write ->
      walk_expression state write.value.target;
      walk_expression state write.value.value

and walk_statement state (stmt : Core.statement) =
  match stmt.value with
  | Core.Expression expr -> walk_expression state expr
  | Let binding ->
      maybe_pick state
        {
          loc = binding.value.name.loc;
          priority = 40;
          contents = binding_contents state.typing binding;
        };
      Option.iter (walk_type state) binding.value.ty;
      walk_expression state binding.value.init_expr
  | Return expr ->
      Option.iter (walk_expression state) expr
  | Defer expr ->
      walk_expression state expr
  | Loop loop ->
      List.iter (walk_statement state) loop.value.init;
      walk_expression state loop.value.cond;
      walk_block state loop.value.body;
      List.iter (walk_statement state) loop.value.step
  | Break | Continue -> ()

and walk_block state (block : Core.block) =
  List.iter (walk_statement state) block.value.statements;
  Option.iter (walk_expression state) block.value.result

let walk_function state (fn : Core.function_decl) =
  maybe_pick state
    {
      loc = fn.value.name.loc;
      priority = 50;
      contents = hover_block (function_signature fn);
    };
  List.iter
    (fun (param : Core.param) ->
      maybe_pick state
        {
          loc = param.value.name.loc;
          priority = 45;
          contents = parameter_contents param;
        };
      walk_type state param.value.ty)
    fn.value.params.value.params;
  Option.iter (walk_type state) fn.value.return_type;
  Option.iter (walk_block state) fn.value.definition

let walk_type_decl state (decl : Core.type_decl) =
  maybe_pick state
    {
      loc = decl.value.name.loc;
      priority = 50;
      contents = hover_block (type_decl_summary decl);
    };
  match decl.value.data with
  | Core.TypeDeclAlias ty ->
      walk_type state ty
  | TypeDeclStruct struct_decl ->
      List.iter
        (fun (field : Core.struct_field) ->
          maybe_pick state
            {
              loc = field.value.name.loc;
              priority = 40;
              contents = struct_field_contents field;
            };
          walk_type state field.value.ty)
        struct_decl.value.fields
  | TypeDeclEnum enum_decl ->
      List.iter
        (fun (variant : Core.enum_variant) ->
          maybe_pick state
            {
              loc = variant.value.name.loc;
              priority = 40;
              contents = enum_variant_contents variant;
            };
          Option.iter (walk_type state) variant.value.inner_ty)
        enum_decl.value.variants
  | TypeDeclForward -> ()

let walk_global state (decl : Core.var_decl) =
  maybe_pick state
    {
      loc = decl.value.name.loc;
      priority = 50;
      contents = global_contents decl;
    };
  walk_type state decl.value.ty;
  Option.iter (walk_expression state) decl.value.init_expr

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

let hover_text_at (typing : Analysis.typing_result) position =
  let state = { position; typing; best = None } in
  List.iter (walk_top_decl state) typing.program.program.value.decls;
  Option.map (fun candidate -> candidate.loc, candidate.contents) state.best
