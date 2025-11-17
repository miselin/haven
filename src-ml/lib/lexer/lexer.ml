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

let mat_type =
  [%sedlex.regexp?
    ("fmat" | "mat"), nonzero, Star digit, 'x', nonzero, Star digit]

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

let rec lex buf acc =
  match%sedlex buf with
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
  | vec_type ->
      let text = Sedlexing.Utf8.lexeme buf in
      lex buf (push_token buf (Vec_type (vec_type_of_string text)) acc)
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

let tokenize buf = lex buf []

let tokenize_channel ch =
  let lexbuf = Sedlexing.Utf8.from_channel ch in
  tokenize lexbuf

let tokenize_stdin () = tokenize_channel stdin

let tokenize_str s =
  let lexbuf = Sedlexing.Utf8.from_string s in
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
          if contains_newline || Option.is_none prev_entry then [] else trivia_buf
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

let tokenize_channel_with_trivia ch =
  let lexbuf = Sedlexing.Utf8.from_channel ch in
  tokenize_with_trivia lexbuf

let tokenize_str_with_trivia s =
  let lexbuf = Sedlexing.Utf8.from_string s in
  tokenize_with_trivia lexbuf
