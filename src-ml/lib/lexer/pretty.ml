open Lexer
open Haven_token

let symbol_tag = function
  | Arrow -> "ARROW"
  | FatArrow -> "FATARROW"
  | Scope -> "SCOPE"
  | Walrus -> "WALRUS"
  | LogicAnd -> "LOGIC_AND"
  | LogicOr -> "LOGIC_OR"
  | EqEq -> "EQEQ"
  | BangEq -> "BANGEQ"
  | LtEq -> "LE"
  | GtEq -> "GE"
  | LShift -> "LSHIFT"
  | RShift -> "RSHIFT"
  | LParen -> "LPAREN"
  | RParen -> "RPAREN"
  | LBrace -> "LBRACE"
  | RBrace -> "RBRACE"
  | LBracket -> "LBRACKET"
  | RBracket -> "RBRACKET"
  | Lt -> "LT"
  | Gt -> "GT"
  | Comma -> "COMMA"
  | Dot -> "DOT"
  | Semicolon -> "SEMICOLON"
  | Colon -> "COLON"
  | Star -> "STAR"
  | Caret -> "CARET"
  | Plus -> "PLUS"
  | Minus -> "MINUS"
  | Slash -> "SLASH"
  | Percent -> "PERCENT"
  | Equal -> "EQUAL"
  | Ampersand -> "AMP"
  | Pipe -> "PIPE"
  | Bang -> "BANG"
  | Tilde -> "TILDE"
  | Underscore -> "UNDERSCORE"

let symbol_lexeme = function
  | Arrow -> "->"
  | FatArrow -> "=>"
  | Scope -> "::"
  | Walrus -> ":="
  | LogicAnd -> "&&"
  | LogicOr -> "||"
  | EqEq -> "=="
  | BangEq -> "!="
  | LtEq -> "<="
  | GtEq -> ">="
  | LShift -> "<<"
  | RShift -> ">>"
  | LParen -> "("
  | RParen -> ")"
  | LBrace -> "{"
  | RBrace -> "}"
  | LBracket -> "["
  | RBracket -> "]"
  | Lt -> "<"
  | Gt -> ">"
  | Comma -> ","
  | Dot -> "."
  | Semicolon -> ";"
  | Colon -> ":"
  | Star -> "*"
  | Caret -> "^"
  | Plus -> "+"
  | Minus -> "-"
  | Slash -> "/"
  | Percent -> "%"
  | Equal -> "="
  | Ampersand -> "&"
  | Pipe -> "|"
  | Bang -> "!"
  | Tilde -> "~"
  | Underscore -> "_"

let pp_trivia = function
  | Whitespace { text; contains_newline } ->
      Printf.printf "WHITESPACE[%d chars] newline=%b\n" (String.length text)
        contains_newline
  | Comment { text; multiline; ends_with_newline } ->
      Printf.printf "COMMENT[%s] multiline=%b ends_with_newline=%b\n" text
        multiline ends_with_newline

let pp_literal = function
  | Int_lit value -> Printf.printf "INT %d\n" value
  | Float_lit value -> Printf.printf "FLOAT %f\n" value
  | Hex_lit value -> Printf.printf "HEX %d\n" value
  | Oct_lit value -> Printf.printf "OCT %d\n" value
  | Bin_lit value -> Printf.printf "BIN %d\n" value
  | String_lit text -> Printf.printf "STRING %S\n" text
  | Char_lit ch -> Printf.printf "CHAR %C\n" ch

let pp_token (token : Raw.tok) =
  match token.tok with
  | Trivia trivia -> pp_trivia trivia
  | Ident text -> Printf.printf "IDENT %s\n" text
  | Numeric_type desc ->
      Printf.printf "NUMERIC_TYPE %s\n" (numeric_type_to_string desc)
  | Vec_type desc -> Printf.printf "VEC_TYPE %s\n" (vec_type_to_string desc)
  | Mat_type desc -> Printf.printf "MAT_TYPE %s\n" (mat_type_to_string desc)
  | Float_type -> Printf.printf "FLOAT_TYPE float\n"
  | Void_type -> Printf.printf "VOID_TYPE void\n"
  | Str_type -> Printf.printf "STR_TYPE str\n"
  | Literal lit -> pp_literal lit
  | Symbol sym -> Printf.printf "%s %s\n" (symbol_tag sym) (symbol_lexeme sym)
  | Newline text -> Printf.printf "NEWLINE %S\n" text
  | EOF -> print_endline "EOF"
