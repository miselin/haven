open Linol.Lsp.Types

type state = { docs : Document_store.t }

let create_state () = { docs = Document_store.create () }

let server_capabilities () : ServerCapabilities.t =
  ServerCapabilities.create
    ~textDocumentSync:
      (`TextDocumentSyncOptions
         (TextDocumentSyncOptions.create ~openClose:true
            ~change:TextDocumentSyncKind.Full (* keep it simple for now *) ()))
    ~documentFormattingProvider:(`Bool true) ()
(* add more as you implement them:
       ~documentSymbolProvider:(`Bool true)
       ~foldingRangeProvider:(`Bool true)
       etc.
    *)

let on_initialize (_state : state) (params : InitializeParams.t) :
    InitializeResult.t =
  let _client_caps = params.capabilities in
  let capabilities = server_capabilities () in
  InitializeResult.create ~capabilities ()

let on_did_open (state : state) (doc : TextDocumentItem.t) =
  Document_store.open_doc state.docs doc

let on_did_close (state : state) (doc : TextDocumentIdentifier.t) =
  Document_store.close_doc state.docs doc.uri

let on_did_change (state : state) (doc : VersionedTextDocumentIdentifier.t)
    (evs : TextDocumentContentChangeEvent.t list) =
  Document_store.change_doc state.docs doc evs

let format_document (_uri : DocumentUri.t) (text : string) :
    TextEdit.t list option =
  let full_range =
    let start_pos = { Position.line = 0; character = 0 } in
    let end_pos = { Position.line = max_int; character = 0 } in
    { Range.start = start_pos; end_ = end_pos }
  in
  let edit = TextEdit.create ~range:full_range ~newText:text in
  Some [ edit ]
(*
  try
    (* These module names are placeholders; adjust to your real ones *)
    let ast = Haven_syntax.parse_string ~filename:(Uri.to_path uri) text in
    let formatted = Haven_format.to_string ast in
    if String.equal formatted text then Some [] (* nothing to change *)
    else
      let full_range =
        (* Replace entire document; editors handle this fine. *)
        let start_pos = { Position.line = 0; character = 0 } in
        (* Very rough; for nicer behavior you can compute proper end line/col. *)
        let end_pos = { Position.line = max_int; character = 0 } in
        { Range.start = start_pos; end_ = end_pos }
      in
      let edit = TextEdit.create ~range:full_range ~newText:formatted in
      Some [ edit ]
  with exn ->
    (* For now, swallow errors; later you can send window/showMessage. *)
    Printf.eprintf "Formatting error: %s\n%!" (Printexc.to_string exn);
    None
  *)

let on_formatting (state : state) (params : DocumentFormattingParams.t) :
    TextEdit.t list option =
  match Document_store.get_text state.docs params.textDocument.uri with
  | None -> Some []
  | Some text -> format_document params.textDocument.uri text
