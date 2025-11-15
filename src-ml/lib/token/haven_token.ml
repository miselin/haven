type signedness = Signed | Unsigned
type numeric_type = { signedness : signedness; bits : int }
type vec_kind = FloatVec

let starts_with ~prefix text =
  let prefix_len = String.length prefix in
  String.length text >= prefix_len
  && String.equal (String.sub text 0 prefix_len) prefix

type vec_type = { kind : vec_kind; dimension : int }
type mat_kind = FloatMat | GenericMat
type mat_type = { kind : mat_kind; rows : int; columns : int }

let invalid msg value = invalid_arg (Printf.sprintf "%s: %s" msg value)

let numeric_type_of_string text =
  if String.length text < 2 then invalid "numeric type too short" text
  else
    let signedness =
      match String.get text 0 with
      | 'i' -> Signed
      | 'u' -> Unsigned
      | _ -> invalid "numeric type must start with i or u" text
    in
    let bits = int_of_string (String.sub text 1 (String.length text - 1)) in
    { signedness; bits }

let vec_type_of_string text =
  let prefix = "fvec" in
  if starts_with ~prefix text then
    let dimension =
      int_of_string
        (String.sub text (String.length prefix)
           (String.length text - String.length prefix))
    in
    { kind = FloatVec; dimension }
  else invalid "unsupported vector type" text

let mat_type_of_string text =
  let kind, prefix_len =
    if starts_with ~prefix:"fmat" text then (FloatMat, 4)
    else if starts_with ~prefix:"mat" text then (GenericMat, 3)
    else invalid "unsupported matrix type" text
  in
  let dims = String.sub text prefix_len (String.length text - prefix_len) in
  match String.split_on_char 'x' dims with
  | [ rows; columns ] ->
      { kind; rows = int_of_string rows; columns = int_of_string columns }
  | _ -> invalid "matrix type must contain dimensions" text

let decode_escape = function
  | '\\' -> '\\'
  | '"' -> '"'
  | '\'' -> '\''
  | 'n' -> '\n'
  | 'r' -> '\r'
  | 't' -> '\t'
  | '0' -> '\000'
  | c -> c

let decode_literal_body text quote_char =
  let len = String.length text in
  if
    len < 2
    || String.get text 0 <> quote_char
    || String.get text (len - 1) <> quote_char
  then invalid "literal missing quotes" text;
  let buf = Buffer.create (len - 2) in
  let rec loop i =
    if i >= len - 1 then Buffer.contents buf
    else
      let c = String.get text i in
      if c = '\\' then (
        if i + 1 >= len - 1 then invalid "unterminated escape sequence" text;
        let escaped = decode_escape (String.get text (i + 1)) in
        Buffer.add_char buf escaped;
        loop (i + 2))
      else (
        Buffer.add_char buf c;
        loop (i + 1))
  in
  loop 1

let string_literal_of_lexeme text = decode_literal_body text '"'

let char_literal_of_lexeme text =
  let decoded = decode_literal_body text '\'' in
  if String.length decoded <> 1 then
    invalid "character literal must contain exactly one character" text;
  String.get decoded 0

let int_literal_of_lexeme text = int_of_string text
let float_literal_of_lexeme text = float_of_string text

let numeric_type_to_string { signedness; bits } =
  let prefix = match signedness with Signed -> "i" | Unsigned -> "u" in
  prefix ^ string_of_int bits

let vec_type_to_string { kind; dimension } =
  let prefix = match kind with FloatVec -> "fvec" in
  prefix ^ string_of_int dimension

let mat_type_to_string { kind; rows; columns } =
  let prefix = match kind with FloatMat -> "fmat" | GenericMat -> "mat" in
  Printf.sprintf "%s%dx%d" prefix rows columns
