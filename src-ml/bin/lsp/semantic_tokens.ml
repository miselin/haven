open Linol.Lsp.Types

module CST = Haven.Cst.Cst
module Locate = Haven.Cst.Locate
module Lexer = Haven_lexer.Lexer

type token_info = {
  line : int;
  start_char : int;
  length : int;
  token_type : string;
  modifiers : string list;
}

let token_types =
  [
    "type";
    "struct";
    "enum";
    "function";
    "parameter";
    "variable";
    "property";
    "enumMember";
    "keyword";
    "operator";
    "number";
    "string";
    "comment";
  ]

let token_modifiers = []

let legend =
  SemanticTokensLegend.create ~tokenModifiers:token_modifiers
    ~tokenTypes:token_types

let token_type_index =
  let tbl = Hashtbl.create (List.length token_types) in
  List.iteri (fun i ty -> Hashtbl.add tbl ty i) token_types;
  fun ty -> Hashtbl.find_opt tbl ty

let modifier_index =
  let tbl = Hashtbl.create (List.length token_modifiers) in
  List.iteri (fun i m -> Hashtbl.add tbl m i) token_modifiers;
  fun m -> Hashtbl.find_opt tbl m

let modifier_mask mods =
  List.fold_left
    (fun acc m ->
      match modifier_index m with
      | None -> acc
      | Some idx -> acc lor (1 lsl idx))
    0 mods

let loc_to_token_info ~token_type (loc : CST.location) =
  let open Lexing in
  let line = loc.start_pos.pos_lnum - 1 in
  let start_char = loc.start_pos.pos_cnum - loc.start_pos.pos_bol in
  let length = loc.end_pos.pos_cnum - loc.start_pos.pos_cnum in
  if length <= 0 then None
  else Some { line; start_char; length; token_type; modifiers = [] }

let add_identifier_token acc ~token_type (id : CST.identifier) =
  match loc_to_token_info ~token_type id.loc with
  | None -> acc
  | Some tok -> tok :: acc

let predicate = function
  | Locate.TypeDecl _
  | Locate.FunctionDecl _
  | Locate.VarDecl _
  | Locate.LetStmt _
  | Locate.Param _
  | Locate.StructField _
  | Locate.EnumVariant _
  | Locate.HavenType _
  | Locate.TemplatedType _
  | Locate.EnumLiteral _
  | Locate.PatternEnum _
  | Locate.PatternBinding _
  | Locate.Expression _
  | Locate.Field _ ->
      true
  | _ -> false

(* Identifiers and types derived from the CST *)
let collect_cst_tokens (program : CST.program) =
  let nodes = Locate.nodes_matching predicate program in
  List.fold_left
    (fun acc node ->
      match node with
      | Locate.FunctionDecl fn ->
          add_identifier_token acc ~token_type:"function" fn.value.name
      | Locate.VarDecl v ->
          add_identifier_token acc ~token_type:"variable" v.value.name
      | Locate.LetStmt s ->
          add_identifier_token acc ~token_type:"variable" s.value.name
      | Locate.Param p ->
          add_identifier_token acc ~token_type:"parameter" p.value.name
      | Locate.StructField f ->
          add_identifier_token acc ~token_type:"property" f.value.name
      | Locate.EnumVariant v ->
          add_identifier_token acc ~token_type:"enumMember" v.value.name
      | Locate.TypeDecl t -> (
          match t.value.data with
          | CST.TypeDeclStruct _ ->
              add_identifier_token acc ~token_type:"struct" t.value.name
          | CST.TypeDeclEnum _ ->
              add_identifier_token acc ~token_type:"enum" t.value.name
          | CST.TypeDeclAlias _ | CST.TypeDeclForward ->
              add_identifier_token acc ~token_type:"type" t.value.name)
      | Locate.HavenType ty -> (
          match ty.value with
          | CST.CustomType { name } ->
              add_identifier_token acc ~token_type:"type" name
          | _ -> acc)
      | Locate.TemplatedType t ->
          add_identifier_token acc ~token_type:"type" t.value.outer
      | Locate.EnumLiteral e ->
          let acc =
            add_identifier_token acc ~token_type:"enumMember" e.value.enum_variant
          in
          add_identifier_token acc ~token_type:"enum" e.value.enum_name
      | Locate.PatternEnum e ->
          let acc =
            match e.value.enum_name with
            | None -> acc
            | Some name -> add_identifier_token acc ~token_type:"enum" name
          in
          add_identifier_token acc ~token_type:"enumMember" e.value.enum_variant
      | Locate.PatternBinding b -> (
          match b.value with
          | CST.BindingNamed id ->
              add_identifier_token acc ~token_type:"variable" id
          | CST.BindingIgnored -> acc)
      | Locate.Expression expr -> (
          match expr.value with
          | CST.Identifier id ->
              add_identifier_token acc ~token_type:"variable" id
          | _ -> acc)
      | Locate.Field f ->
          add_identifier_token acc ~token_type:"property" f.value.field
      | _ -> acc)
    [] nodes

let keyword_strings =
  [
    "if";
    "else";
    "let";
    "while";
    "break";
    "continue";
    "match";
    "as";
    "pub";
    "mut";
    "fn";
    "iter";
    "load";
    "ret";
    "struct";
    "float";
    "str";
    "void";
    "type";
    "nil";
    "defer";
    "impure";
    "enum";
    "import";
    "cimport";
    "size";
    "box";
    "unbox";
    "intrinsic";
    "foreign";
    "data";
    "state";
    "Vec";
    "Mat";
    "Function";
    "VAFunction";
    "Cell";
    "ref";
  ]

let keyword_table =
  let tbl = Hashtbl.create (List.length keyword_strings) in
  List.iter (fun kw -> Hashtbl.replace tbl kw ()) keyword_strings;
  tbl

let is_keyword s = Hashtbl.mem keyword_table s

let loc_of_raw_tok (tok : Lexer.Raw.tok) : CST.location =
  { start_pos = tok.startp; end_pos = tok.endp }

let add_token_for_loc acc ~token_type loc =
  match loc_to_token_info ~token_type loc with
  | None -> acc
  | Some tok -> tok :: acc

let add_comment_tokens acc trivia =
  List.fold_left
    (fun acc (tok : Lexer.Raw.tok) ->
      match tok.tok with
      | Lexer.Raw.Trivia (Lexer.Comment _) ->
          add_token_for_loc acc ~token_type:"comment" (loc_of_raw_tok tok)
      | _ -> acc)
    acc trivia

let symbol_is_operator = function
  | Lexer.Arrow
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
  | Lt
  | Gt
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
  | LParen
  | RParen
  | LBrace
  | RBrace
  | LBracket
  | RBracket
  | Comma
  | Dot
  | Semicolon
  | Colon
  | Underscore ->
      true

let collect_lexical_tokens (parsed : CST.parsed_program) =
  List.fold_left
    (fun acc (entry : Lexer.token_with_trivia) ->
      let acc = add_comment_tokens acc entry.leading_trivia in
      let acc =
        match entry.token with
        | Lexer.Raw.Ident s when is_keyword s ->
            add_token_for_loc acc ~token_type:"keyword"
              (loc_of_raw_tok
                 { tok = entry.token; startp = entry.startp; endp = entry.endp })
        | Lexer.Raw.Ident _ -> acc
        | Lexer.Raw.Numeric_type _ | Vec_type _ | Mat_type _ | Float_type
        | Void_type | Str_type ->
            add_token_for_loc acc ~token_type:"type"
              (loc_of_raw_tok
                 { tok = entry.token; startp = entry.startp; endp = entry.endp })
        | Lexer.Raw.Literal lit -> (
            let token_type =
              match lit with
              | Hex_lit _ | Oct_lit _ | Bin_lit _ | Int_lit _ | Float_lit _ ->
                  "number"
              | String_lit _ | Char_lit _ -> "string"
            in
            add_token_for_loc acc ~token_type
              (loc_of_raw_tok
                 { tok = entry.token; startp = entry.startp; endp = entry.endp }))
        | Lexer.Raw.Symbol sym when symbol_is_operator sym ->
            add_token_for_loc acc ~token_type:"operator"
              (loc_of_raw_tok
                 { tok = entry.token; startp = entry.startp; endp = entry.endp })
        | Lexer.Raw.Trivia _ | Newline _ | EOF -> acc
        | Lexer.Raw.Symbol _ -> acc
      in
      add_comment_tokens acc entry.trailing_trivia)
    [] parsed.trivia

let compare_token a b =
  match Int.compare a.line b.line with
  | 0 -> Int.compare a.start_char b.start_char
  | x -> x

let dedup_tokens tokens =
  let set = Hashtbl.create 256 in
  List.fold_left
    (fun acc t ->
      let key = (t.line, t.start_char, t.length, t.token_type) in
      if Hashtbl.mem set key then acc
      else (
        Hashtbl.add set key ();
        t :: acc))
    [] tokens
  |> List.rev

let encode_tokens tokens =
  let tokens = List.sort compare_token tokens in
  let data = Array.make (List.length tokens * 5) 0 in
  let rec encode idx prev_line prev_char = function
    | [] -> ()
    | tok :: rest -> (
        match token_type_index tok.token_type with
        | None -> encode idx prev_line prev_char rest
        | Some token_type ->
            let delta_line = tok.line - prev_line in
            let delta_start =
              if delta_line = 0 then tok.start_char - prev_char
              else tok.start_char
            in
            let modifier_mask = modifier_mask tok.modifiers in
            data.(idx) <- delta_line;
            data.(idx + 1) <- delta_start;
            data.(idx + 2) <- tok.length;
            data.(idx + 3) <- token_type;
            data.(idx + 4) <- modifier_mask;
            encode (idx + 5) tok.line tok.start_char rest)
  in
  encode 0 0 0 tokens;
  data

let tokens_in_range tokens (range : Range.t) =
  let start_line = range.start.line in
  let start_char = range.start.character in
  let end_line = range.end_.line in
  let end_char = range.end_.character in
  let within token =
    let line = token.line in
    let char_ = token.start_char in
    let starts_after_start =
      (line > start_line) || (line = start_line && char_ >= start_char)
    in
    let starts_before_end =
      (line < end_line) || (line = end_line && char_ <= end_char)
    in
    starts_after_start && starts_before_end
  in
  List.filter within tokens

let build_tokens ?range (parsed : CST.parsed_program) =
  let cst_tokens = collect_cst_tokens parsed.program in
  let lex_tokens = collect_lexical_tokens parsed in
  let merged = dedup_tokens (cst_tokens @ lex_tokens) in
  let merged = match range with None -> merged | Some r -> tokens_in_range merged r in
  encode_tokens merged

let semantic_tokens_for_program parsed =
  let data = build_tokens parsed in
  SemanticTokens.create ~data ()

let semantic_tokens_for_program_in_range parsed range =
  let data = build_tokens ~range parsed in
  SemanticTokens.create ~data ()
