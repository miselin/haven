open Linol.Lsp.Types

module Analysis = Haven.Ast.Analysis
module Core = Analysis.Core
module Pretty = Haven.Ast.Pretty

let format_core_type (ty : Core.haven_type) =
  Format.asprintf "%a" Pretty.pp_core_type ty

let format_resolved_type loc resolved =
  format_core_type (Analysis.core_type_of_resolved_ty loc resolved)

let binding_annotation typing (binding : Core.let_stmt) =
  Hashtbl.find_opt typing.Analysis.annotations.bindings (Analysis.binding_id binding)

let expr_annotation typing (expr : Core.expression) =
  Hashtbl.find_opt typing.Analysis.annotations.exprs (Analysis.expr_id expr)

let type_text loc inferred resolved =
  match (inferred, resolved) with
  | _, Some resolved -> format_resolved_type loc resolved
  | Some inferred, None -> format_core_type inferred
  | None, None -> "unknown"

let add_type_hint acc loc text =
  InlayHint.create ~kind:InlayHintKind.Type
    ~label:(`String (": " ^ text)) ~paddingLeft:true
    ~position:(Lsp_helpers.position_of_lex_position loc.Haven_core.Loc.end_pos)
    ()
  :: acc

let maybe_add_hint query_range acc loc text =
  if Haven_core.Loc.overlaps_range loc query_range then add_type_hint acc loc text
  else acc

let rec walk_expression typing type_env query_range acc (expr : Core.expression) =
  match expr.value with
  | Core.Binary binary ->
      let acc = walk_expression typing type_env query_range acc binary.value.left in
      walk_expression typing type_env query_range acc binary.value.right
  | Unary unary ->
      walk_expression typing type_env query_range acc unary.value.inner
  | Block block ->
      walk_block typing type_env query_range acc block
  | Literal _ | Identifier _ | Nil ->
      acc
  | ToBool inner | SizeExpr inner | BoxExpr inner | Unbox inner | Ref inner | Load inner ->
      walk_expression typing type_env query_range acc inner
  | Initializer init ->
      List.fold_left
        (walk_expression typing type_env query_range)
        acc init.value.exprs
  | As cast ->
      walk_expression typing type_env query_range acc cast.value.inner
  | SizeType _ | BoxType _ ->
      acc
  | Match match_expr ->
      let acc =
        walk_expression typing type_env query_range acc match_expr.value.expr
      in
      let scrutinee_resolved =
        match expr_annotation typing match_expr.value.expr with
        | Some annotation -> annotation.resolved_type
        | None -> None
      in
      List.fold_left
        (walk_match_arm typing type_env query_range scrutinee_resolved)
        acc match_expr.value.arms
  | Call call ->
      let acc = walk_expression typing type_env query_range acc call.value.target in
      List.fold_left
        (walk_expression typing type_env query_range)
        acc call.value.params
  | Index index ->
      let acc = walk_expression typing type_env query_range acc index.value.target in
      walk_expression typing type_env query_range acc index.value.index
  | Field field ->
      walk_expression typing type_env query_range acc field.value.target
  | Assign write | Mutate write ->
      let acc = walk_expression typing type_env query_range acc write.value.target in
      walk_expression typing type_env query_range acc write.value.value

and walk_statement typing type_env query_range acc (stmt : Core.statement) =
  match stmt.value with
  | Core.Expression expr ->
      walk_expression typing type_env query_range acc expr
  | Let binding ->
      let acc =
        match (binding.value.ty, binding_annotation typing binding) with
        | None, Some annotation ->
            maybe_add_hint query_range acc binding.value.name.loc
              (type_text binding.loc annotation.inferred_type annotation.resolved_type)
        | _ -> acc
      in
      walk_expression typing type_env query_range acc binding.value.init_expr
  | Return expr ->
      Option.fold ~none:acc
        ~some:(walk_expression typing type_env query_range acc)
        expr
  | Defer expr ->
      walk_expression typing type_env query_range acc expr
  | Loop loop ->
      let acc =
        List.fold_left (walk_statement typing type_env query_range) acc loop.value.init
      in
      let acc = walk_expression typing type_env query_range acc loop.value.cond in
      let acc = walk_block typing type_env query_range acc loop.value.body in
      List.fold_left (walk_statement typing type_env query_range) acc loop.value.step
  | Break | Continue ->
      acc

and walk_block typing type_env query_range acc (block : Core.block) =
  let acc =
    List.fold_left
      (walk_statement typing type_env query_range)
      acc block.value.statements
  in
  Option.fold ~none:acc
    ~some:(walk_expression typing type_env query_range acc)
    block.value.result

and walk_match_arm typing type_env query_range scrutinee_resolved acc
    (arm : Core.match_arm) =
  let acc =
    match arm.value.pattern.value with
    | Core.PatternDefault | PatternLiteral _ ->
        acc
    | Core.PatternEnum enum ->
        let payload_tys =
          Option.bind scrutinee_resolved (fun resolved ->
              Option.bind
                (Analysis.lookup_enum_variant type_env arm.value.pattern.loc resolved
                   enum.value.enum_variant.value)
                (fun (_variant, inner_tys) -> Some inner_tys))
        in
        let rec add_binding_hints acc bindings payload_tys =
          match bindings with
          | [] -> acc
          | (binding : Core.pattern_binding) :: rest ->
              let payload_ty, rest_payload_tys =
                match payload_tys with
                | Some (payload_ty :: rest_payload_tys) ->
                    (Some payload_ty, Some rest_payload_tys)
                | _ ->
                    (None, None)
              in
              let acc =
                match (binding.value, payload_ty) with
                | Core.BindingNamed id, Some ty ->
                    maybe_add_hint query_range acc id.loc (format_resolved_type id.loc ty)
                | _ ->
                    acc
              in
              add_binding_hints acc rest rest_payload_tys
        in
        add_binding_hints acc enum.value.binding payload_tys
  in
  walk_expression typing type_env query_range acc arm.value.expr

let hints_for_range (typing : Analysis.typing_result) query_range =
  let type_env = Analysis.type_env_of_program typing.program.program in
  let hints =
    List.fold_left
      (fun acc (decl : Core.top_decl) ->
        match decl.value with
        | Core.FDecl fn ->
            Option.fold ~none:acc
              ~some:(walk_block typing type_env query_range acc)
              fn.value.definition
        | Core.Foreign foreign ->
            List.fold_left
              (fun acc (fn : Core.function_decl) ->
                Option.fold ~none:acc
                  ~some:(walk_block typing type_env query_range acc)
                  fn.value.definition)
              acc foreign.value.decls
        | Core.VDecl var_decl ->
            Option.fold ~none:acc
              ~some:(walk_expression typing type_env query_range acc)
              var_decl.value.init_expr
        | Core.TDecl _ | Core.Import _ | Core.CImport _ ->
            acc)
      [] typing.program.program.value.decls
  in
  List.rev hints
