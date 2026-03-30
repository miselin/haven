open Haven_token.Token

type trivia =
  | Whitespace of { text : string; contains_newline : bool }
  | Comment of { text : string; multiline : bool; ends_with_newline : bool }

type literal =
  | Int_lit of int
  | Float_lit of float
  | Hex_lit of int
  | Oct_lit of int
  | Bin_lit of int
  | String_lit of string
  | Char_lit of char

type symbol =
  | Arrow
  | FatArrow
  | Scope
  | Walrus
  | LogicAnd
  | LogicOr
  | EqEq
  | BangEq
  | LtEq
  | GtEq
  | LShift
  | RShift
  | LParen
  | RParen
  | LBrace
  | RBrace
  | LBracket
  | RBracket
  | Lt
  | Gt
  | Comma
  | Dot
  | Semicolon
  | Colon
  | Star
  | Caret
  | Plus
  | Minus
  | Slash
  | Percent
  | Equal
  | Ampersand
  | Pipe
  | Bang
  | Tilde
  | Underscore

module Raw = struct
  type t =
    | Trivia of trivia
    | Ident of string
    | Numeric_type of numeric_type
    | Vec_type of vec_type
    | Mat_type of mat_type
    | Vec_hole_type
    | Mat_hole_type
    | Float_type
    | Void_type
    | Str_type
    | Literal of literal
    | Symbol of symbol
    | Newline of string
    | EOF

  type tok = { tok : t; startp : Lexing.position; endp : Lexing.position }
end

open Raw

let digit = [%sedlex.regexp? '0' .. '9']
let nonzero = [%sedlex.regexp? '1' .. '9']
let hex_digit = [%sedlex.regexp? digit | 'a' .. 'f' | 'A' .. 'F']
let oct_digit = [%sedlex.regexp? '0' .. '7']
let bin_digit = [%sedlex.regexp? '0' | '1']
let lowercase = [%sedlex.regexp? 'a' .. 'z']
let uppercase = [%sedlex.regexp? 'A' .. 'Z']
let letter = [%sedlex.regexp? lowercase | uppercase]
let ident_inner = [%sedlex.regexp? letter | digit | '_']
let ident_segment = [%sedlex.regexp? Plus ident_inner]
let numeric_type = [%sedlex.regexp? ('i' | 'u'), nonzero, Star digit]
let vec_type = [%sedlex.regexp? "fvec", nonzero, Star digit]
let vec_hole_type = [%sedlex.regexp? "fvec?"]

let mat_type =
  [%sedlex.regexp?
    ("fmat" | "mat"), nonzero, Star digit, 'x', nonzero, Star digit]

let mat_hole_type = [%sedlex.regexp? "mat?"]

let float_type = [%sedlex.regexp? "float"]
let void_type = [%sedlex.regexp? "void"]
let str_type = [%sedlex.regexp? "str"]
let int_literal = [%sedlex.regexp? Plus digit]
let float_literal = [%sedlex.regexp? Plus digit, '.', Plus digit]
let hex_literal = [%sedlex.regexp? "0x", Plus hex_digit]
let oct_literal = [%sedlex.regexp? "0o", Plus oct_digit]
let bin_literal = [%sedlex.regexp? "0b", Plus bin_digit]
let escape_sequence = [%sedlex.regexp? '\\', any]
let string_char = [%sedlex.regexp? Compl ('"' | '\\') | escape_sequence]
let char_char = [%sedlex.regexp? Compl ('\'' | '\\') | escape_sequence]
let string_literal = [%sedlex.regexp? '"', Star string_char, '"']
let char_literal = [%sedlex.regexp? '\'', char_char, '\'']

let ident =
  [%sedlex.regexp? (letter | '_'), Star ident_inner, Star ('-', ident_segment)]

let newline = [%sedlex.regexp? "\r\n" | '\n' | '\r']
let preprocessor_directive = [%sedlex.regexp? '#', Star (Compl ('\n' | '\r')), Opt newline]
let whitespace = [%sedlex.regexp? Plus (Chars " \t\012\013")]
let line_comment = [%sedlex.regexp? "//", Star (Compl ('\n' | '\r'))]

let block_comment =
  [%sedlex.regexp? "/*", Star (Compl '*' | '*', Compl '/'), "*/"]

let has_newline text =
  let len = String.length text in
  let rec loop i =
    if i >= len then false
    else
      match String.unsafe_get text i with
      | '\n' | '\r' -> true
      | _ -> loop (i + 1)
  in
  loop 0

let ends_with_newline text =
  let len = String.length text in
  if len = 0 then false
  else
    match String.unsafe_get text (len - 1) with
    | '\n' | '\r' -> true
    | _ -> false

let make_trivia token = Trivia token
let make_symbol sym = Symbol sym

let push_token buf tok acc =
  let startp, endp = Sedlexing.lexing_positions buf in
  { tok; startp; endp } :: acc

let trim_ascii_whitespace text =
  let len = String.length text in
  let is_space = function
    | ' ' | '\t' | '\012' | '\013' -> true
    | _ -> false
  in
  let rec find_start i =
    if i >= len then len
    else if is_space (String.unsafe_get text i) then find_start (i + 1)
    else i
  in
  let rec find_end i =
    if i < 0 then -1
    else if is_space (String.unsafe_get text i) then find_end (i - 1)
    else i
  in
  let start = find_start 0 in
  let finish = find_end (len - 1) in
  if finish < start then "" else String.sub text start (finish - start + 1)

let strip_trailing_newline text =
  let len = String.length text in
  let rec find_end i =
    if i < 0 then -1
    else
      match String.unsafe_get text i with
      | '\n' | '\r' -> find_end (i - 1)
      | _ -> i
  in
  let finish = find_end (len - 1) in
  if finish < 0 then "" else String.sub text 0 (finish + 1)

let parse_decimal_prefix text start =
  let len = String.length text in
  let rec loop i =
    if i < len then
      match String.unsafe_get text i with
      | '0' .. '9' -> loop (i + 1)
      | _ -> i
    else i
  in
  let finish = loop start in
  if finish = start then None
  else Some (int_of_string (String.sub text start (finish - start)), finish)

let parse_quoted_string text start =
  let len = String.length text in
  if start >= len || String.unsafe_get text start <> '"' then None
  else
    let buf = Buffer.create 32 in
    let rec loop i =
      if i >= len then None
      else
        match String.unsafe_get text i with
        | '"' -> Some (Buffer.contents buf, i + 1)
        | '\\' when i + 1 < len ->
            Buffer.add_char buf (String.unsafe_get text (i + 1));
            loop (i + 2)
        | c ->
            Buffer.add_char buf c;
            loop (i + 1)
    in
    loop (start + 1)

let parse_line_directive text =
  let body =
    text |> strip_trailing_newline |> trim_ascii_whitespace
  in
  let body =
    if String.length body > 0 && String.unsafe_get body 0 = '#' then
      String.sub body 1 (String.length body - 1) |> trim_ascii_whitespace
    else body
  in
  let body =
    if
      String.length body >= 4
      && String.equal (String.sub body 0 4) "line"
      &&
      (String.length body = 4
      ||
      match String.unsafe_get body 4 with
      | ' ' | '\t' | '\012' | '\013' -> true
      | _ -> false)
    then
      String.sub body 4 (String.length body - 4) |> trim_ascii_whitespace
    else body
  in
  match parse_decimal_prefix body 0 with
  | None -> None
  | Some (line, after_line) ->
      let rest =
        String.sub body after_line (String.length body - after_line)
        |> trim_ascii_whitespace
      in
      let file =
        match parse_quoted_string rest 0 with
        | Some (path, _) -> Some path
        | None -> None
      in
      Some (line, file)

let apply_line_directive buf (endp : Lexing.position) line file =
  let filename = Option.value ~default:endp.pos_fname file in
  let next_pos =
    {
      endp with
      pos_fname = filename;
      pos_lnum = line;
      (* Keep absolute offsets monotonic while rebasing the next logical line. *)
      pos_bol = endp.pos_cnum;
    }
  in
  Sedlexing.set_filename buf filename;
  Sedlexing.set_position ~bytes_position:next_pos buf next_pos

let rec next_significant_token = function
  | [] -> None
  | { tok = Trivia _ | Newline _; _ } :: rest -> next_significant_token rest
  | tok :: _ -> Some tok

let should_split_rshift rest =
  match next_significant_token rest with
  | None -> true
  | Some { tok = EOF; _ } -> true
  | Some { tok = Symbol sym; _ } -> (
      match sym with
      | Comma
      | Dot
      | Semicolon
      | Colon
      | Star
      | Caret
      | RParen
      | RBrace
      | RBracket
      | LBracket
      | Gt
      | Scope ->
          true
      | Arrow
      | FatArrow
      | Walrus
      | LogicAnd
      | LogicOr
      | EqEq
      | BangEq
      | LtEq
      | GtEq
      | LShift
      | RShift
      | LParen
      | LBrace
      | Lt
      | Plus
      | Minus
      | Slash
      | Percent
      | Equal
      | Ampersand
      | Pipe
      | Bang
      | Tilde
      | Underscore ->
          false)
  | Some _ -> false

let split_closing_rshifts tokens =
  let gt_of tok = { tok with tok = Symbol Gt } in
  let rec loop acc = function
    | [] -> List.rev acc
    | ({ tok = Symbol RShift; _ } as tok) :: rest when should_split_rshift rest ->
        loop (gt_of tok :: gt_of tok :: acc) rest
    | tok :: rest -> loop (tok :: acc) rest
  in
  loop [] tokens

let rec lex buf acc =
  match%sedlex buf with
  | preprocessor_directive ->
      let text = Sedlexing.Utf8.lexeme buf in
      let _, endp = Sedlexing.lexing_positions buf in
      Option.iter
        (fun (line, file) -> apply_line_directive buf endp line file)
        (parse_line_directive text);
      lex buf acc
  | newline ->
      let text = Sedlexing.Utf8.lexeme buf in
      lex buf (push_token buf (Newline text) acc)
  | whitespace ->
      let text = Sedlexing.Utf8.lexeme buf in
      let trivia = Whitespace { text; contains_newline = has_newline text } in
      lex buf (push_token buf (make_trivia trivia) acc)
  | block_comment ->
      let text = Sedlexing.Utf8.lexeme buf in
      let trivia =
        Comment
          { text; multiline = true; ends_with_newline = ends_with_newline text }
      in
      lex buf (push_token buf (make_trivia trivia) acc)
  | line_comment ->
      let text = Sedlexing.Utf8.lexeme buf in
      let trivia =
        Comment { text; multiline = false; ends_with_newline = false }
      in
      lex buf (push_token buf (make_trivia trivia) acc)
  | "->" -> lex buf (push_token buf (make_symbol Arrow) acc)
  | "=>" -> lex buf (push_token buf (make_symbol FatArrow) acc)
  | "::" -> lex buf (push_token buf (make_symbol Scope) acc)
  | ":=" -> lex buf (push_token buf (make_symbol Walrus) acc)
  | "&&" -> lex buf (push_token buf (make_symbol LogicAnd) acc)
  | "||" -> lex buf (push_token buf (make_symbol LogicOr) acc)
  | "==" -> lex buf (push_token buf (make_symbol EqEq) acc)
  | "!=" -> lex buf (push_token buf (make_symbol BangEq) acc)
  | "<=" -> lex buf (push_token buf (make_symbol LtEq) acc)
  | ">=" -> lex buf (push_token buf (make_symbol GtEq) acc)
  | "<<" -> lex buf (push_token buf (make_symbol LShift) acc)
  | ">>" -> lex buf (push_token buf (make_symbol RShift) acc)
  | numeric_type ->
      let text = Sedlexing.Utf8.lexeme buf in
      lex buf (push_token buf (Numeric_type (numeric_type_of_string text)) acc)
  | vec_hole_type -> lex buf (push_token buf Vec_hole_type acc)
  | vec_type ->
      let text = Sedlexing.Utf8.lexeme buf in
      lex buf (push_token buf (Vec_type (vec_type_of_string text)) acc)
  | mat_hole_type -> lex buf (push_token buf Mat_hole_type acc)
  | mat_type ->
      let text = Sedlexing.Utf8.lexeme buf in
      lex buf (push_token buf (Mat_type (mat_type_of_string text)) acc)
  | float_type -> lex buf (push_token buf Float_type acc)
  | void_type -> lex buf (push_token buf Void_type acc)
  | str_type -> lex buf (push_token buf Str_type acc)
  | hex_literal ->
      let text = Sedlexing.Utf8.lexeme buf in
      lex buf
        (push_token buf (Literal (Hex_lit (int_literal_of_lexeme text))) acc)
  | oct_literal ->
      let text = Sedlexing.Utf8.lexeme buf in
      lex buf
        (push_token buf (Literal (Oct_lit (int_literal_of_lexeme text))) acc)
  | bin_literal ->
      let text = Sedlexing.Utf8.lexeme buf in
      lex buf
        (push_token buf (Literal (Bin_lit (int_literal_of_lexeme text))) acc)
  | float_literal ->
      let text = Sedlexing.Utf8.lexeme buf in
      lex buf
        (push_token buf
           (Literal (Float_lit (float_literal_of_lexeme text)))
           acc)
  | int_literal ->
      let text = Sedlexing.Utf8.lexeme buf in
      lex buf
        (push_token buf (Literal (Int_lit (int_literal_of_lexeme text))) acc)
  | string_literal ->
      let text = Sedlexing.Utf8.lexeme buf in
      lex buf
        (push_token buf
           (Literal (String_lit (string_literal_of_lexeme text)))
           acc)
  | char_literal ->
      let text = Sedlexing.Utf8.lexeme buf in
      lex buf
        (push_token buf (Literal (Char_lit (char_literal_of_lexeme text))) acc)
  | '(' -> lex buf (push_token buf (make_symbol LParen) acc)
  | ')' -> lex buf (push_token buf (make_symbol RParen) acc)
  | '{' -> lex buf (push_token buf (make_symbol LBrace) acc)
  | '}' -> lex buf (push_token buf (make_symbol RBrace) acc)
  | '[' -> lex buf (push_token buf (make_symbol LBracket) acc)
  | ']' -> lex buf (push_token buf (make_symbol RBracket) acc)
  | '<' -> lex buf (push_token buf (make_symbol Lt) acc)
  | '>' -> lex buf (push_token buf (make_symbol Gt) acc)
  | ',' -> lex buf (push_token buf (make_symbol Comma) acc)
  | '.' -> lex buf (push_token buf (make_symbol Dot) acc)
  | ';' -> lex buf (push_token buf (make_symbol Semicolon) acc)
  | ':' -> lex buf (push_token buf (make_symbol Colon) acc)
  | '*' -> lex buf (push_token buf (make_symbol Star) acc)
  | '^' -> lex buf (push_token buf (make_symbol Caret) acc)
  | '+' -> lex buf (push_token buf (make_symbol Plus) acc)
  | '-' -> lex buf (push_token buf (make_symbol Minus) acc)
  | '/' -> lex buf (push_token buf (make_symbol Slash) acc)
  | '%' -> lex buf (push_token buf (make_symbol Percent) acc)
  | '=' -> lex buf (push_token buf (make_symbol Equal) acc)
  | '&' -> lex buf (push_token buf (make_symbol Ampersand) acc)
  | '|' -> lex buf (push_token buf (make_symbol Pipe) acc)
  | '!' -> lex buf (push_token buf (make_symbol Bang) acc)
  | '~' -> lex buf (push_token buf (make_symbol Tilde) acc)
  | '_' -> lex buf (push_token buf (make_symbol Underscore) acc)
  | ident -> lex buf (push_token buf (Ident (Sedlexing.Utf8.lexeme buf)) acc)
  | eof ->
      let acc = push_token buf EOF acc in
      List.rev acc
  | any ->
      let bad = Sedlexing.Utf8.lexeme buf in
      failwith (Printf.sprintf "Unexpected character: %s" bad)
  | _ -> lex buf acc

let tokenize buf = lex buf [] |> split_closing_rshifts

let tokenize_channel ?(filename = "") ch =
  let lexbuf = Sedlexing.Utf8.from_channel ch in
  Sedlexing.set_filename lexbuf filename;
  tokenize lexbuf

let tokenize_stdin ?(filename = "<stdin>") () = tokenize_channel ~filename stdin

let tokenize_str ?(filename = "") s =
  let lexbuf = Sedlexing.Utf8.from_string s in
  Sedlexing.set_filename lexbuf filename;
  tokenize lexbuf

let tokenize_gen g =
  let lexbuf = Sedlexing.Utf8.from_gen g in
  tokenize lexbuf

let lexbuf_from_stdin = Sedlexing.Utf8.from_channel stdin

type token_with_trivia = {
  token : Raw.t;
  startp : Lexing.position;
  endp : Lexing.position;
  leading_trivia : Raw.tok list;
  trailing_trivia : Raw.tok list;
}

let trivia_has_newline (tok : Raw.tok) =
  match tok.tok with
  | Trivia (Whitespace { contains_newline; _ }) -> contains_newline
  | Trivia (Comment { ends_with_newline; multiline; _ }) ->
      multiline && ends_with_newline
  | Newline _ -> true
  | _ -> false

let is_trivia = function
  | { tok = Trivia _; _ } | { tok = Newline _; _ } -> true
  | _ -> false

let add_trailing trailing (entry : token_with_trivia) =
  { entry with trailing_trivia = entry.trailing_trivia @ trailing }

let group_trivia (raw_tokens : Raw.tok list) =
  let rec loop prev_entry acc trivia_buf = function
    | [] -> (
        match prev_entry with
        | None -> List.rev acc
        | Some entry ->
            let entry = add_trailing trivia_buf entry in
            List.rev (entry :: acc))
    | tok :: rest when is_trivia tok ->
        loop prev_entry acc (trivia_buf @ [ tok ]) rest
    | tok :: rest ->
        let contains_newline = List.exists trivia_has_newline trivia_buf in
        let leading =
          if contains_newline || Option.is_none prev_entry then trivia_buf
          else []
        in
        let trailing_prev =
          if contains_newline || Option.is_none prev_entry then []
          else trivia_buf
        in

        let acc =
          match prev_entry with
          | None -> acc
          | Some entry -> add_trailing trailing_prev entry :: acc
        in

        let current =
          {
            token = tok.tok;
            startp = tok.startp;
            endp = tok.endp;
            leading_trivia = leading;
            trailing_trivia = [];
          }
        in
        loop (Some current) acc [] rest
  in
  loop None [] [] raw_tokens

let tokenize_with_trivia buf = lex buf [] |> group_trivia

let tokenize_channel_with_trivia ?(filename = "") ch =
  let lexbuf = Sedlexing.Utf8.from_channel ch in
  Sedlexing.set_filename lexbuf filename;
  tokenize_with_trivia lexbuf

let tokenize_str_with_trivia ?(filename = "") s =
  let lexbuf = Sedlexing.Utf8.from_string s in
  Sedlexing.set_filename lexbuf filename;
  tokenize_with_trivia lexbuf
