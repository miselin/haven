open Linol.Lsp.Types

let line_offsets text =
  let offsets = ref [ 0 ] in
  String.iteri
    (fun index ch ->
      if ch = '\n' then offsets := (index + 1) :: !offsets)
    text;
  Array.of_list (List.rev !offsets)

let position_of_lex_position (position : Lexing.position) =
  Position.create ~line:(position.pos_lnum - 1)
    ~character:(position.pos_cnum - position.pos_bol)

let loc_to_range (loc : Haven_core.Loc.t) =
  Range.create ~start:(position_of_lex_position loc.start_pos)
    ~end_:(position_of_lex_position loc.end_pos)

let end_position_of_text text =
  let offsets = line_offsets text in
  let last_line = max 0 (Array.length offsets - 1) in
  let bol = offsets.(last_line) in
  Position.create ~line:last_line ~character:(String.length text - bol)

let full_document_range text =
  Range.create ~start:(Position.create ~line:0 ~character:0)
    ~end_:(end_position_of_text text)

let lex_position_of_lsp_position ~filename ~text (position : Position.t) =
  let offsets = line_offsets text in
  let max_line = max 0 (Array.length offsets - 1) in
  let line = min position.line max_line in
  let bol = offsets.(line) in
  let next_bol =
    if line + 1 < Array.length offsets then offsets.(line + 1) else String.length text
  in
  let line_limit =
    if next_bol > bol && text.[next_bol - 1] = '\n' then next_bol - 1 else next_bol
  in
  let character = min position.character (line_limit - bol) in
  {
    Lexing.pos_fname = filename;
    pos_lnum = line + 1;
    pos_bol = bol;
    pos_cnum = bol + character;
  }

let lex_range_of_lsp_range ~filename ~text (range : Range.t) =
  {
    Haven_core.Loc.start_pos =
      lex_position_of_lsp_position ~filename ~text range.start;
    end_pos = lex_position_of_lsp_position ~filename ~text range.end_;
  }

let zero_range =
  let pos = Position.create ~line:0 ~character:0 in
  Range.create ~start:pos ~end_:pos

let is_multiline_loc (loc : Haven_core.Loc.t) =
  loc.start_pos.pos_lnum < loc.end_pos.pos_lnum
