open Linol.Lsp.Types

type document = {
  uri : DocumentUri.t;
  mutable version : int option;
  mutable text : string;
}

type t = (DocumentUri.t, document) Hashtbl.t

let create () : t = Hashtbl.create 16

let open_doc (store : t) (td : TextDocumentItem.t) =
  let uri = td.uri in
  let doc = { uri; version = Some td.version; text = td.text } in
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
      List.iter (apply_change doc) evs

let get_text (store : t) (uri : DocumentUri.t) : string option =
  Hashtbl.find_opt store uri |> Option.map (fun d -> d.text)
