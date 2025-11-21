type t = { start_pos : Lexing.position; end_pos : Lexing.position }

let pp fmt { start_pos; _ } =
  Format.fprintf fmt "<filename todo>:%d:%d" start_pos.pos_lnum
    start_pos.pos_cnum

let location_between ~start_pos ~end_pos = { start_pos; end_pos }
let pos_key (pos : Lexing.position) = (pos.pos_fname, pos.pos_cnum)

let pos_before (a : Lexing.position) (b : Lexing.position) =
  a.pos_cnum < b.pos_cnum

let contains_position loc pos =
  (not (pos_before pos loc.start_pos)) && not (pos_before loc.end_pos pos)

let overlaps_range loc range =
  not
    (pos_before loc.end_pos range.start_pos
    || pos_before range.end_pos loc.start_pos)
