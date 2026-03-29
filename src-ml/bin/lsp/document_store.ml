open Linol.Lsp.Types

module Analysis = Haven.Ast.Analysis

type parse_error = {
  loc : Haven_core.Loc.t option;
  message : string;
}

type document = {
  uri : DocumentUri.t;
  mutable version : int option;
  mutable text : string;
  (* None until the program has been parsed *)
  mutable cst : Haven.Cst.Cst.parsed_program option;
  mutable pipeline : Analysis.Pipeline.result option;
  mutable parse_error : parse_error option;
}

type t = (DocumentUri.t, document) Hashtbl.t

let create () : t = Hashtbl.create 16

let loc_of_line_col ~filename ~line ~col =
  let line = max 1 line in
  let col = max 1 col in
  let start_pos =
    {
      Lexing.pos_fname = filename;
      pos_lnum = line;
      pos_bol = 0;
      pos_cnum = col - 1;
    }
  in
  let end_pos = { start_pos with pos_cnum = start_pos.pos_cnum + 1 } in
  { Haven_core.Loc.start_pos; end_pos }

let location_from_error_message ~filename message =
  let tail =
    match String.split_on_char '\n' message with
    | head :: _ -> head
    | [] -> message
  in
  let tail =
    match String.split_on_char '|' tail with
    | head :: _ -> head
    | [] -> tail
  in
  let tail =
    match String.split_on_char '\r' tail with
    | head :: _ -> head
    | [] -> tail
  in
  let location_text =
    match String.split_on_char ' ' tail |> List.rev with
    | [] -> None
    | token :: _ when String.contains token ':' -> Some token
    | _ -> None
  in
  let parse_line_col text =
    match String.rindex_opt text ':' with
    | None -> None
    | Some last_colon -> (
        match String.rindex_from_opt text (last_colon - 1) ':' with
        | None -> None
        | Some second_last_colon -> (
            let line_text =
              String.sub text (second_last_colon + 1)
                (last_colon - second_last_colon - 1)
            in
            let col_text =
              String.sub text (last_colon + 1)
                (String.length text - last_colon - 1)
            in
            match (int_of_string_opt line_text, int_of_string_opt col_text) with
            | Some line, Some col ->
                let path =
                  String.sub text 0 second_last_colon
                in
                Some
                  (loc_of_line_col
                     ~filename:(if String.length path = 0 then filename else path)
                     ~line ~col)
            | _ -> None))
  in
  Option.bind location_text parse_line_col

let analyze_document uri text =
  let filename = DocumentUri.to_path uri in
  try
    let cst = Haven.Parser.parse_string ~filename text in
    let pipeline = Analysis.Pipeline.run_cst cst in
    (Some cst, Some pipeline, None)
  with
  | Failure message
  | Sys_error message ->
      let parse_error =
        {
          loc = location_from_error_message ~filename message;
          message;
        }
      in
      (None, None, Some parse_error)
  | exn ->
      let message = Printexc.to_string exn in
      let parse_error =
        {
          loc = location_from_error_message ~filename message;
          message;
        }
      in
      (None, None, Some parse_error)

let reanalyze doc =
  let cst, pipeline, parse_error = analyze_document doc.uri doc.text in
  doc.cst <- cst;
  doc.pipeline <- pipeline;
  doc.parse_error <- parse_error

let open_doc (store : t) (td : TextDocumentItem.t) =
  let uri = td.uri in
  let doc =
    {
      uri;
      version = Some td.version;
      text = td.text;
      cst = None;
      pipeline = None;
      parse_error = None;
    }
  in
  reanalyze doc;
  Hashtbl.replace store uri doc

let close_doc (store : t) (uri : DocumentUri.t) = Hashtbl.remove store uri

let apply_change doc (change : TextDocumentContentChangeEvent.t) =
  match change.range with
  | None ->
      (* Full text replacement *)
      doc.text <- change.text
  | Some _range ->
      (* SyncKind = Full so we'll just do full text replacement. This isn't right long-term. *)
      doc.text <- change.text

let change_doc (store : t) (d : VersionedTextDocumentIdentifier.t)
    (evs : TextDocumentContentChangeEvent.t list) =
  match Hashtbl.find_opt store d.uri with
  | None -> ()
  | Some doc ->
      doc.version <- Some d.version;
      List.iter (apply_change doc) evs;
      reanalyze doc

let get_text (store : t) (uri : DocumentUri.t) : string option =
  Hashtbl.find_opt store uri |> Option.map (fun d -> d.text)

let get_cst (store : t) (uri : DocumentUri.t) =
  match Hashtbl.find_opt store uri with None -> None | Some doc -> doc.cst

let get_pipeline (store : t) (uri : DocumentUri.t) =
  match Hashtbl.find_opt store uri with None -> None | Some doc -> doc.pipeline

let get_parse_error (store : t) (uri : DocumentUri.t) =
  match Hashtbl.find_opt store uri with None -> None | Some doc -> doc.parse_error

let get_doc (store : t) (uri : DocumentUri.t) = Hashtbl.find_opt store uri
