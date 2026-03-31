open Linol.Lsp.Types

module Analysis = Haven.Ast.Analysis

type state = { docs : Document_store.t }

let create_state () = { docs = Document_store.create () }

let string_of_category = function
  | Analysis.Import -> "import"
  | Analysis.TypeCheck -> "typecheck"
  | Analysis.TypeVerify -> "typeverify"
  | Analysis.Semantic -> "semantic"
  | Analysis.Purity -> "purity"
  | Analysis.Cleanup -> "cleanup"
  | Analysis.Ownership -> "ownership"

let severity_of_level = function
  | Analysis.Error -> DiagnosticSeverity.Error
  | Analysis.Warning -> DiagnosticSeverity.Warning

let collect_pipeline_diagnostics (pipeline : Analysis.Pipeline.result) =
  pipeline.typing.diagnostics
  @ pipeline.verify.diagnostics
  @ pipeline.semantic.diagnostics
  @ pipeline.asserts.diagnostics
  @ pipeline.purity.diagnostics
  @ pipeline.ownership.diagnostics

let diagnostics_for_doc (doc : Document_store.document) =
  let path = DocumentUri.to_path doc.uri in
  match (doc.parse_error, doc.pipeline) with
  | Some parse_error, _ ->
      let range =
        match parse_error.loc with
        | Some loc -> Lsp_helpers.loc_to_range loc
        | None -> Lsp_helpers.zero_range
      in
      [
        Diagnostic.create ~message:(`String parse_error.message) ~range
          ~severity:DiagnosticSeverity.Error ~source:"haven/parser" ();
      ]
  | None, Some pipeline ->
      collect_pipeline_diagnostics pipeline
      |> List.filter (fun (diagnostic : Analysis.diagnostic) ->
             String.equal diagnostic.loc.start_pos.pos_fname path)
      |> List.map (fun (diagnostic : Analysis.diagnostic) ->
             Diagnostic.create ~message:(`String diagnostic.message)
               ~range:(Lsp_helpers.loc_to_range diagnostic.loc)
               ~severity:(severity_of_level diagnostic.level)
               ~source:("haven/" ^ string_of_category diagnostic.category)
               ())
  | None, None -> []

let publish_diagnostics_params (state : state) (uri : DocumentUri.t) =
  match Document_store.get_doc state.docs uri with
  | None -> None
  | Some doc ->
      Some
        (PublishDiagnosticsParams.create
           ~diagnostics:(diagnostics_for_doc doc) ~uri ?version:doc.version ())

let publish_all_diagnostics_params (state : state) =
  Hashtbl.fold
    (fun uri _ acc ->
      match publish_diagnostics_params state uri with
      | None -> acc
      | Some params -> params :: acc)
    state.docs []

let server_capabilities () : ServerCapabilities.t =
  ServerCapabilities.create
    ~textDocumentSync:
      (`TextDocumentSyncOptions
         (TextDocumentSyncOptions.create ~openClose:true
            ~change:TextDocumentSyncKind.Incremental
            ~save:(`SaveOptions (SaveOptions.create ~includeText:true ())) ()))
    ~definitionProvider:(`Bool true)
    ~documentHighlightProvider:(`Bool true)
    ~documentFormattingProvider:(`Bool true)
    ~documentSymbolProvider:(`Bool true)
    ~foldingRangeProvider:(`Bool true)
    ~hoverProvider:(`Bool true)
    ~inlayHintProvider:(`Bool true)
    ~selectionRangeProvider:(`Bool true)
    ~codeLensProvider:(CodeLensOptions.create ())
    ~executeCommandProvider:
      (ExecuteCommandOptions.create ~commands:[ Code_lenses.command_name ] ())
    ~semanticTokensProvider:
      (`SemanticTokensOptions
         (SemanticTokensOptions.create ~full:(`Bool true) ~range:true
            ~legend:Semantic_tokens.legend ()))
    ()

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

let on_did_save (state : state) (params : DidSaveTextDocumentParams.t) =
  Document_store.save_doc state.docs params.textDocument params.text

let with_doc state uri f =
  Option.bind (Document_store.get_doc state.docs uri) f

let with_doc_pipeline state uri f =
  with_doc state uri (fun (doc : Document_store.document) ->
      Option.bind doc.pipeline (fun pipeline -> f doc pipeline))

let lex_position_for_doc (doc : Document_store.document) position =
  let filename = DocumentUri.to_path doc.uri in
  Lsp_helpers.lex_position_of_lsp_position ~filename ~text:doc.text position

let on_hover (state : state) (params : HoverParams.t) =
  with_doc_pipeline state params.textDocument.uri (fun doc pipeline ->
      let position = lex_position_for_doc doc params.position in
      match Hover_info.hover_text_at pipeline.typing position with
      | None -> None
      | Some (loc, contents) ->
          let markup =
            MarkupContent.create ~kind:MarkupKind.Markdown ~value:contents
          in
          Some
            (Hover.create ~contents:(`MarkupContent markup)
               ~range:(Lsp_helpers.loc_to_range loc) ()))

let on_definition (state : state) (uri : DocumentUri.t) (position : Position.t) =
  with_doc_pipeline state uri (fun doc pipeline ->
      let position = lex_position_for_doc doc position in
      Option.map
        (fun loc ->
          let uri = DocumentUri.of_path loc.Haven_core.Loc.start_pos.pos_fname in
          `Location
            [
              Location.create ~uri ~range:(Lsp_helpers.loc_to_range loc);
            ])
        (Hover_info.definition_at pipeline.typing position))

let on_document_symbols (state : state) (uri : DocumentUri.t) =
  match Document_store.get_cst state.docs uri with
  | None -> None
  | Some parsed ->
      Some (`DocumentSymbol (Document_symbols.symbols_for_program parsed))

let on_folding_ranges (state : state) (uri : DocumentUri.t) =
  match Document_store.get_cst state.docs uri with
  | None -> None
  | Some parsed ->
      Some (Document_structure.folding_ranges parsed)

let on_inlay_hints (state : state) (uri : DocumentUri.t) (range : Range.t) =
  with_doc_pipeline state uri (fun doc pipeline ->
      let filename = DocumentUri.to_path doc.uri in
      let query_range =
        Lsp_helpers.lex_range_of_lsp_range ~filename ~text:doc.text range
      in
      Some (Inlay_hints.hints_for_range pipeline.typing query_range))

let document_highlight_kind = function
  | `Read -> DocumentHighlightKind.Read
  | `Write -> DocumentHighlightKind.Write
  | `Text -> DocumentHighlightKind.Text

let on_document_highlights (state : state) (uri : DocumentUri.t)
    (position : Position.t) =
  with_doc_pipeline state uri (fun doc pipeline ->
      let position = lex_position_for_doc doc position in
      Some
        (List.map
           (fun (highlight : Symbol_resolution.highlight) ->
             DocumentHighlight.create
               ~kind:(document_highlight_kind highlight.kind)
               ~range:(Lsp_helpers.loc_to_range highlight.loc) ())
           (Symbol_resolution.highlights_at pipeline.typing position)))

let on_selection_ranges (state : state) (uri : DocumentUri.t)
    (positions : Position.t list) =
  with_doc state uri (fun doc ->
      match doc.cst with
      | None -> None
      | Some parsed ->
          let positions = List.map (lex_position_for_doc doc) positions in
          Some (Document_structure.selection_ranges_at_positions parsed positions))

let on_code_lenses (state : state) (uri : DocumentUri.t) =
  with_doc_pipeline state uri (fun doc pipeline ->
      Some (Code_lenses.code_lenses (DocumentUri.to_path doc.uri) pipeline))

let on_execute_command (_state : state) command =
  if String.equal command Code_lenses.command_name then Some `Null else None

let parse_cst text uri =
  let filename = DocumentUri.to_path uri in
  try Some (Haven.Parser.parse_string ~filename text) with _ -> None

let read_file_text path =
  try
    let ch = open_in_bin path in
    Fun.protect
      ~finally:(fun () -> close_in_noerr ch)
      (fun () ->
        let len = in_channel_length ch in
        really_input_string ch len)
    |> Option.some
  with _ -> None

let format_document (state : state) (uri : DocumentUri.t) :
    TextEdit.t list option =
  match Document_store.get_doc state.docs uri with
  | None ->
      let path = DocumentUri.to_path uri in
      Option.bind (read_file_text path) (fun text ->
          Option.bind (parse_cst text uri) (fun cst ->
              let newText = Haven.Cst.Emit.emit_program_to_string cst in
              let edit =
                TextEdit.create ~range:(Lsp_helpers.full_document_range text)
                  ~newText
              in
              Some [ edit ]))
  | Some doc -> (
      match doc.cst with
      | Some cst ->
          let newText = Haven.Cst.Emit.emit_program_to_string cst in
          let edit =
            TextEdit.create ~range:(Lsp_helpers.full_document_range doc.text)
              ~newText
          in
          Some [ edit ]
      | None ->
          Option.bind (parse_cst doc.text uri) (fun cst ->
              let newText = Haven.Cst.Emit.emit_program_to_string cst in
              let edit =
                TextEdit.create ~range:(Lsp_helpers.full_document_range doc.text)
                  ~newText
              in
              Some [ edit ]))

let on_formatting (state : state) (params : DocumentFormattingParams.t) :
    TextEdit.t list option =
  format_document state params.textDocument.uri

let semantic_tokens_for_uri (state : state) (uri : DocumentUri.t) =
  match Document_store.get_cst state.docs uri with
  | None -> None
  | Some cst -> Some (Semantic_tokens.semantic_tokens_for_program cst)

let on_semantic_tokens_full (state : state) (params : SemanticTokensParams.t) =
  semantic_tokens_for_uri state params.textDocument.uri

let on_semantic_tokens_range (state : state)
    (params : SemanticTokensRangeParams.t) =
  match Document_store.get_cst state.docs params.textDocument.uri with
  | None -> None
  | Some cst ->
      Some
        (Semantic_tokens.semantic_tokens_for_program_in_range cst params.range)
