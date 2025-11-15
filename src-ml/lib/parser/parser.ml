open Haven_token

type located_token = Grammar.token * Lexing.position * Lexing.position

type lexer_state = {
  tokens : Haven_lexer.Lexer.Raw.tok list;
  mutable i : int;
  mutable last_token : located_token option;
  mutable prev_token : located_token option;
}

let keywords =
  Hashtbl.of_seq
    (List.to_seq
       [
         ("if", Grammar.IF);
         ("else", Grammar.ELSE);
         ("let", Grammar.LET);
         ("while", Grammar.WHILE);
         ("break", Grammar.BREAK);
         ("continue", Grammar.CONTINUE);
         ("match", Grammar.MATCH);
         ("as", Grammar.AS);
         ("pub", Grammar.PUB);
         ("mut", Grammar.MUT);
         ("fn", Grammar.FN);
         ("iter", Grammar.ITER);
         ("load", Grammar.LOAD);
         ("ret", Grammar.RET);
         ("struct", Grammar.STRUCT);
         ("float", Grammar.FLOAT_TYPE);
         ("str", Grammar.STR_TYPE);
         ("void", Grammar.VOID_TYPE);
         ("type", Grammar.TYPE);
         ("nil", Grammar.NIL);
         ("defer", Grammar.DEFER);
         ("impure", Grammar.IMPURE);
         ("enum", Grammar.ENUM);
         ("import", Grammar.IMPORT);
         ("cimport", Grammar.CIMPORT);
         ("size", Grammar.SIZE);
         ("box", Grammar.BOX);
         ("unbox", Grammar.UNBOX);
         ("intrinsic", Grammar.INTRINSIC);
         ("foreign", Grammar.FOREIGN);
         ("data", Grammar.DATA);
         ("state", Grammar.STATE);
         ("Vec", Grammar.VEC);
         ("Mat", Grammar.MAT);
         ("Function", Grammar.FUNCTION);
         ("VAFunction", Grammar.VAFUNCTION);
         ("Cell", Grammar.CELL);
         ("load", Grammar.LOAD);
         ("ref", Grammar.REF);
       ])

let keyword_or_ident s =
  match Hashtbl.find_opt keywords s with
  | Some kw -> kw
  | None -> Grammar.IDENT s

let store_token st tok =
  st.prev_token <- st.last_token;
  st.last_token <- Some tok;
  tok

let rec next_token st : Grammar.token * Lexing.position * Lexing.position =
  let open Haven_lexer.Lexer.Raw in
  if st.i >= List.length st.tokens then
    let pos = Lexing.dummy_pos in
    (Grammar.EOF, pos, pos)
  else
    let raw = List.nth st.tokens st.i in
    st.i <- st.i + 1;

    let startp = raw.startp in
    let endp = raw.endp in

    (* Haven_lexer.Pretty.pp_token raw; *)
    match raw.tok with
    | Trivia _ | Newline _ ->
        (* Skip trivia by recursion or a loop *)
        next_token st
    | Ident s -> store_token st (keyword_or_ident s, startp, endp)
    | Numeric_type s -> store_token st (Grammar.NUMERIC_TYPE s, startp, endp)
    | Vec_type s -> store_token st (Grammar.VEC_TYPE s, startp, endp)
    | Mat_type s -> store_token st (Grammar.MAT_TYPE s, startp, endp)
    | Float_type -> store_token st (Grammar.FLOAT_TYPE, startp, endp)
    | Void_type -> store_token st (Grammar.VOID_TYPE, startp, endp)
    | Str_type -> store_token st (Grammar.STR_TYPE, startp, endp)
    | Literal lit ->
        let tok =
          match lit with
          | Int_lit s -> Grammar.INT_LIT s
          | Float_lit s -> Grammar.FLOAT_LIT s
          | Hex_lit s -> Grammar.HEX_LIT s
          | Oct_lit s -> Grammar.OCT_LIT s
          | Bin_lit s -> Grammar.BIN_LIT s
          | String_lit s -> Grammar.STRING_LIT s
          | Char_lit s -> Grammar.CHAR_LIT s
        in
        store_token st (tok, startp, endp)
    | Symbol sym ->
        let tok =
          match sym with
          | Arrow -> Grammar.ARROW
          | FatArrow -> Grammar.FATARROW
          | Scope -> Grammar.SCOPE
          | Walrus -> Grammar.WALRUS
          | LogicAnd -> Grammar.LOGIC_AND
          | LogicOr -> Grammar.LOGIC_OR
          | EqEq -> Grammar.EQEQ
          | BangEq -> Grammar.BANGEQ
          | LtEq -> Grammar.LE
          | GtEq -> Grammar.GE
          | LShift -> Grammar.LSHIFT
          | RShift -> Grammar.RSHIFT
          | LParen -> Grammar.LPAREN
          | RParen -> Grammar.RPAREN
          | LBrace -> Grammar.LBRACE
          | RBrace -> Grammar.RBRACE
          | LBracket -> Grammar.LBRACKET
          | RBracket -> Grammar.RBRACKET
          | Lt -> Grammar.LT
          | Gt -> Grammar.GT
          | Comma -> Grammar.COMMA
          | Dot -> Grammar.DOT
          | Semicolon -> Grammar.SEMICOLON
          | Colon -> Grammar.COLON
          | Star -> Grammar.STAR
          | Caret -> Grammar.CARET
          | Plus -> Grammar.PLUS
          | Minus -> Grammar.MINUS
          | Slash -> Grammar.SLASH
          | Percent -> Grammar.PERCENT
          | Equal -> Grammar.EQUAL
          | Ampersand -> Grammar.AMP
          | Pipe -> Grammar.PIPE
          | Bang -> Grammar.BANG
          | Tilde -> Grammar.TILDE
          | Underscore -> Grammar.UNDERSCORE
        in
        store_token st (tok, startp, endp)
    | EOF -> store_token st (Grammar.EOF, startp, endp)

let string_of_position (pos : Lexing.position) =
  let line = pos.Lexing.pos_lnum in
  let col = pos.Lexing.pos_cnum - pos.Lexing.pos_bol + 1 in
  if pos.Lexing.pos_fname = "" then Printf.sprintf "line %d, column %d" line col
  else Printf.sprintf "%s:%d:%d" pos.Lexing.pos_fname line col

let token_to_string = function
  | Grammar.IDENT s -> Printf.sprintf "identifier %S" s
  | Grammar.INT_LIT v -> Printf.sprintf "integer literal %d" v
  | Grammar.FLOAT_LIT v -> Printf.sprintf "float literal %g" v
  | Grammar.STRING_LIT s -> Printf.sprintf "string literal %S" s
  | Grammar.CHAR_LIT c -> Printf.sprintf "char literal %C" c
  | Grammar.HEX_LIT v -> Printf.sprintf "hex literal %d" v
  | Grammar.OCT_LIT v -> Printf.sprintf "oct literal %d" v
  | Grammar.BIN_LIT v -> Printf.sprintf "binary literal %d" v
  | Grammar.NUMERIC_TYPE desc ->
      Printf.sprintf "numeric type %s" (numeric_type_to_string desc)
  | Grammar.VEC_TYPE desc ->
      Printf.sprintf "vector type %s" (vec_type_to_string desc)
  | Grammar.MAT_TYPE desc ->
      Printf.sprintf "matrix type %s" (mat_type_to_string desc)
  | Grammar.VOID_TYPE -> "void"
  | Grammar.FLOAT_TYPE -> "float"
  | Grammar.STR_TYPE -> "str"
  | Grammar.PUB -> "pub"
  | Grammar.FN -> "fn"
  | Grammar.IMPURE -> "impure"
  | Grammar.MUT -> "mut"
  | Grammar.LET -> "let"
  | Grammar.IF -> "if"
  | Grammar.ELSE -> "else"
  | Grammar.RET -> "ret"
  | Grammar.MATCH -> "match"
  | Grammar.WHILE -> "while"
  | Grammar.LPAREN -> "("
  | Grammar.RPAREN -> ")"
  | Grammar.LBRACE -> "{"
  | Grammar.RBRACE -> "}"
  | Grammar.LBRACKET -> "["
  | Grammar.RBRACKET -> "]"
  | Grammar.LT -> "<"
  | Grammar.GT -> ">"
  | Grammar.LE -> "<="
  | Grammar.GE -> ">="
  | Grammar.PLUS -> "+"
  | Grammar.MINUS -> "-"
  | Grammar.STAR -> "*"
  | Grammar.SLASH -> "/"
  | Grammar.PERCENT -> "%"
  | Grammar.EQUAL -> "="
  | Grammar.LSHIFT -> "<<"
  | Grammar.RSHIFT -> ">>"
  | Grammar.CARET -> "^"
  | Grammar.AMP -> "&"
  | Grammar.PIPE -> "|"
  | Grammar.SEMICOLON -> ";"
  | Grammar.COLON -> ":"
  | Grammar.COMMA -> ","
  | Grammar.ARROW -> "->"
  | Grammar.FATARROW -> "=>"
  | Grammar.WALRUS -> ":="
  | Grammar.LOGIC_AND -> "&&"
  | Grammar.LOGIC_OR -> "||"
  | Grammar.EQEQ -> "=="
  | Grammar.BANGEQ -> "!="
  | Grammar.BANG -> "!"
  | Grammar.TILDE -> "~"
  | Grammar.MAT -> "mat"
  | Grammar.VEC -> "vec"
  | Grammar.EOF -> "end of file"
  | _ -> "token"

let describe_token_at (tok, startp, _) =
  Printf.sprintf "%s at %s" (token_to_string tok) (string_of_position startp)

module I = Grammar.MenhirInterpreter

let rec loop st checkpoint =
  match checkpoint with
  | I.InputNeeded _env ->
      let tok, sp, ep = next_token st in
      let checkpoint = I.offer checkpoint (tok, sp, ep) in
      loop st checkpoint
  | I.Shifting _ | I.AboutToReduce _ -> loop st (I.resume checkpoint)
  | I.Accepted v -> v
  | I.HandlingError env ->
      let startp, _ = I.positions env in
      let location = string_of_position startp in
      let current =
        match st.last_token with
        | Some (tok, _, _) -> token_to_string tok
        | None -> "unknown token"
      in
      let previous =
        match st.prev_token with
        | Some tok -> Printf.sprintf " after %s" (describe_token_at tok)
        | None -> ""
      in
      let message =
        Printf.sprintf "parse error while reading %s at %s%s" current location
          previous
      in
      failwith message
  | I.Rejected -> failwith "parse rejected"

let parse_stdin =
  let raw_tokens = Haven_lexer.Lexer.tokenize_stdin in
  let st =
    { tokens = raw_tokens; i = 0; last_token = None; prev_token = None }
  in

  loop st (Grammar.Incremental.program (List.nth raw_tokens 0).startp)
