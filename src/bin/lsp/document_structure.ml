open Linol.Lsp.Types

module Cst = Haven.Cst.Cst
module Locate = Haven.Cst.Locate

let compare_loc_size left right =
  let left_size =
    left.Haven_core.Loc.end_pos.pos_cnum - left.start_pos.pos_cnum
  in
  let right_size =
    right.Haven_core.Loc.end_pos.pos_cnum - right.start_pos.pos_cnum
  in
  let by_size = compare left_size right_size in
  if by_size <> 0 then by_size
  else
    let by_start = compare left.start_pos.pos_cnum right.start_pos.pos_cnum in
    if by_start <> 0 then by_start
    else compare left.end_pos.pos_cnum right.end_pos.pos_cnum

let loc_key (loc : Haven_core.Loc.t) =
  let start_pos = loc.start_pos in
  let end_pos = loc.end_pos in
  ( start_pos.pos_fname,
    start_pos.pos_lnum,
    start_pos.pos_cnum,
    end_pos.pos_lnum,
    end_pos.pos_cnum )

let folding_range ?kind (loc : Haven_core.Loc.t) =
  if not (Lsp_helpers.is_multiline_loc loc) then None
  else
    let range = Lsp_helpers.loc_to_range loc in
    Some
      (FoldingRange.create ?kind ~startLine:range.start.line
         ~endLine:range.end_.line ())

let import_folding_ranges (parsed : Cst.parsed_program) =
  let flush (start_loc : Haven_core.Loc.t option)
      (end_loc : Haven_core.Loc.t option) acc =
    match (start_loc, end_loc) with
    | Some start_loc, Some end_loc ->
        let loc =
          Haven_core.Loc.location_between ~start_pos:start_loc.start_pos
            ~end_pos:end_loc.end_pos
        in
        Option.to_list (folding_range ~kind:FoldingRangeKind.Imports loc) @ acc
    | _ -> acc
  in
  let rec loop start_loc end_loc acc = function
    | [] ->
        List.rev (flush start_loc end_loc acc)
    | (decl : Cst.top_decl) :: rest -> (
        match decl.value with
        | Cst.Import _ | Cst.CImport _ ->
            let start_loc =
              match start_loc with None -> Some decl.loc | Some loc -> Some loc
            in
            loop start_loc (Some decl.loc) acc rest
        | _ ->
            loop None None (flush start_loc end_loc acc) rest)
  in
  loop None None [] parsed.program.value.decls

let folding_ranges (parsed : Cst.parsed_program) =
  let structural =
    Locate.nodes_matching
      (function
        | Locate.Block _ | StructDecl _ | EnumDecl _ | Foreign _ | TypeExtend _ -> true
        | _ -> false)
      parsed.program
    |> List.filter_map (fun node ->
           match node with
           | Locate.Block block ->
               folding_range ~kind:FoldingRangeKind.Region block.loc
           | StructDecl struct_decl ->
               folding_range ~kind:FoldingRangeKind.Region struct_decl.loc
           | EnumDecl enum_decl ->
               folding_range ~kind:FoldingRangeKind.Region enum_decl.loc
           | Foreign foreign ->
               folding_range ~kind:FoldingRangeKind.Region foreign.loc
           | TypeExtend ext ->
               folding_range ~kind:FoldingRangeKind.Region ext.loc
           | _ ->
               None)
  in
  import_folding_ranges parsed @ structural

let selection_range_at_position (parsed : Cst.parsed_program) position =
  let locs =
    Locate.nodes_at_position position parsed.program
    |> List.map Locate.location_of
    |> List.sort_uniq (fun left right -> compare (loc_key left) (loc_key right))
    |> List.sort compare_loc_size
  in
  let rec build = function
    | [] -> None
    | loc :: rest ->
        let parent = build rest in
        Some
          (SelectionRange.create ?parent ~range:(Lsp_helpers.loc_to_range loc) ())
  in
  match build locs with
  | Some range -> range
  | None ->
      let point = Lsp_helpers.position_of_lex_position position in
      SelectionRange.create ~range:(Range.create ~start:point ~end_:point) ()

let selection_ranges_at_positions parsed positions =
  List.map (selection_range_at_position parsed) positions
