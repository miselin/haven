open Linol.Lsp.Types

module Cst = Haven.Cst.Cst
module Pretty = Haven.Cst.Pretty

let type_text ty = Format.asprintf "%a" Pretty.pp_type ty

let function_detail (fn : Cst.function_decl) =
  let params =
    List.map
      (fun (param : Cst.param) -> type_text param.value.ty)
      fn.value.params.value.params
  in
  let params =
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
  let return_suffix =
    match fn.value.return_type with
    | Some ty -> " -> " ^ type_text ty
    | None -> ""
  in
  Printf.sprintf "%s (%s)%s" prefix (String.concat ", " params) return_suffix

let variable_detail (decl : Cst.var_decl) =
  Printf.sprintf "%s%s"
    (if decl.value.is_mutable then "mut " else "")
    (type_text decl.value.ty)

let field_detail (field : Cst.struct_field) = type_text field.value.ty

let variant_detail (variant : Cst.enum_variant) =
  match variant.value.inner_tys with
  | [] -> None
  | tys -> Some (String.concat ", " (List.map type_text tys))

let make_symbol ?children ?detail ~kind ~name ~range ~selection_range () =
  DocumentSymbol.create ?children ?detail ~kind ~name ~range
    ~selectionRange:selection_range ()

let field_symbol (field : Cst.struct_field) =
  make_symbol ~kind:SymbolKind.Field ~name:field.value.name.value
    ~range:(Lsp_helpers.loc_to_range field.loc)
    ~selection_range:(Lsp_helpers.loc_to_range field.value.name.loc)
    ?detail:(Some (field_detail field)) ()

let variant_symbol (variant : Cst.enum_variant) =
  make_symbol ~kind:SymbolKind.EnumMember ~name:variant.value.name.value
    ~range:(Lsp_helpers.loc_to_range variant.loc)
    ~selection_range:(Lsp_helpers.loc_to_range variant.value.name.loc)
    ?detail:(variant_detail variant) ()

let extend_item_symbol (item : Cst.extend_item) =
  match item.value with
  | Cst.ExtendConstruct construct ->
      make_symbol ~kind:SymbolKind.Constructor ~name:"construct"
        ~range:(Lsp_helpers.loc_to_range item.loc)
        ~selection_range:(Lsp_helpers.loc_to_range construct.loc)
        ?detail:
          (Some
             (Printf.sprintf "(%s)"
                (String.concat ", "
                   (List.map
                      (fun (param : Cst.param) -> type_text param.value.ty)
                      construct.value.params))))
        ()
  | Cst.ExtendDestruct block ->
      make_symbol ~kind:SymbolKind.Method ~name:"destruct"
        ~range:(Lsp_helpers.loc_to_range item.loc)
        ~selection_range:(Lsp_helpers.loc_to_range block.loc) ()

let extend_symbol (ext : Cst.type_extend) =
  make_symbol
    ~kind:SymbolKind.Object
    ~name:(Printf.sprintf "extend %s" ext.value.target.value)
    ~range:(Lsp_helpers.loc_to_range ext.loc)
    ~selection_range:(Lsp_helpers.loc_to_range ext.value.target.loc)
    ~children:(List.map extend_item_symbol ext.value.items)
    ()

let function_symbol (fn : Cst.function_decl) =
  make_symbol ~kind:SymbolKind.Function ~name:fn.value.name.value
    ~range:(Lsp_helpers.loc_to_range fn.loc)
    ~selection_range:(Lsp_helpers.loc_to_range fn.value.name.loc)
    ~detail:(function_detail fn) ()

let variable_symbol (decl : Cst.var_decl) =
  make_symbol
    ~kind:
      (if decl.value.is_mutable then SymbolKind.Variable else SymbolKind.Constant)
    ~name:decl.value.name.value ~range:(Lsp_helpers.loc_to_range decl.loc)
    ~selection_range:(Lsp_helpers.loc_to_range decl.value.name.loc)
    ~detail:(variable_detail decl) ()

let type_symbol (decl : Cst.type_decl) =
  let kind, children, detail =
    match decl.value.data with
    | Cst.TypeDeclAlias ty ->
        (SymbolKind.TypeParameter, None, Some (type_text ty))
    | Cst.TypeDeclForward ->
        (SymbolKind.TypeParameter, None, None)
    | Cst.TypeDeclStruct struct_decl ->
        ( SymbolKind.Struct,
          Some (List.map field_symbol struct_decl.value.fields),
          Some "struct" )
    | Cst.TypeDeclEnum enum_decl ->
        ( SymbolKind.Enum,
          Some (List.map variant_symbol enum_decl.value.variants),
          Some "enum" )
  in
  make_symbol ?children ?detail ~kind ~name:decl.value.name.value
    ~range:(Lsp_helpers.loc_to_range decl.loc)
    ~selection_range:(Lsp_helpers.loc_to_range decl.value.name.loc) ()

let symbols_for_program (parsed : Cst.parsed_program) =
  let top_decl_symbols (decl : Cst.top_decl) =
    match decl.value with
    | Cst.FDecl fn ->
        [ function_symbol fn ]
    | Cst.VDecl var_decl ->
        [ variable_symbol var_decl ]
    | Cst.TDecl type_decl ->
        [ type_symbol type_decl ]
    | Cst.Extend ext ->
        [ extend_symbol ext ]
    | Cst.Foreign foreign ->
        List.map function_symbol foreign.value.decls
    | Cst.Import _ | Cst.CImport _ ->
        []
  in
  List.concat_map top_decl_symbols parsed.program.value.decls
