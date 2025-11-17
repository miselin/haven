open Linol.Lsp.Types

type state = { docs : Document_store.t }

let create_state () = { docs = Document_store.create () }

let server_capabilities () : ServerCapabilities.t =
  ServerCapabilities.create
    ~textDocumentSync:
      (`TextDocumentSyncOptions
         (TextDocumentSyncOptions.create ~openClose:true
            ~change:TextDocumentSyncKind.Full ()))
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

let format_document (state : state) (uri : DocumentUri.t) :
    TextEdit.t list option =
  let full_range =
    let start_pos = { Position.line = 0; character = 0 } in
    let end_pos = { Position.line = max_int; character = 0 } in
    { Range.start = start_pos; end_ = end_pos }
  in
  match Document_store.get_cst state.docs uri with
  | None -> None
  | Some cst ->
      (* TODO: this is a full-document rewrite, emit smaller edits? *)
      let newText = Haven.Cst.Emit.emit_program_to_string cst in
      let edit = TextEdit.create ~range:full_range ~newText in
      Some [ edit ]

let on_formatting (state : state) (params : DocumentFormattingParams.t) :
    TextEdit.t list option =
  format_document state params.textDocument.uri
