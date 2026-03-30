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

let loc_to_range (loc : Haven_core.Loc.t) =
  let start_pos = loc.start_pos in
  let end_pos = loc.end_pos in
  let start =
    Position.create ~line:(start_pos.pos_lnum - 1)
      ~character:(start_pos.pos_cnum - start_pos.pos_bol)
  in
  let end_ =
    Position.create ~line:(end_pos.pos_lnum - 1)
      ~character:(end_pos.pos_cnum - end_pos.pos_bol)
  in
  Range.create ~start ~end_

let zero_range =
  let pos = Position.create ~line:0 ~character:0 in
  Range.create ~start:pos ~end_:pos

let severity_of_level = function
  | Analysis.Error -> DiagnosticSeverity.Error
  | Analysis.Warning -> DiagnosticSeverity.Warning

let collect_pipeline_diagnostics (pipeline : Analysis.Pipeline.result) =
  pipeline.typing.diagnostics
  @ pipeline.verify.diagnostics
  @ pipeline.semantic.diagnostics
  @ pipeline.purity.diagnostics
  @ pipeline.ownership.diagnostics

let diagnostics_for_doc (doc : Document_store.document) =
  let path = DocumentUri.to_path doc.uri in
  match (doc.parse_error, doc.pipeline) with
  | Some parse_error, _ ->
      let range =
        match parse_error.loc with
        | Some loc -> loc_to_range loc
        | None -> zero_range
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
               ~range:(loc_to_range diagnostic.loc)
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

let line_offsets text =
  let offsets = ref [ 0 ] in
  String.iteri
    (fun index ch ->
      if ch = '\n' then offsets := (index + 1) :: !offsets)
    text;
  Array.of_list (List.rev !offsets)

let lex_position_of_lsp_position ~filename ~text (position : Position.t) =
  let offsets = line_offsets text in
  let max_line = max 0 (Array.length offsets - 1) in
  let line = min position.line max_line in
  let bol = offsets.(line) in
  let next_bol =
    if line + 1 < Array.length offsets then offsets.(line + 1) else String.length text
  in
  let line_limit =
    if next_bol > bol && text.[next_bol - 1] = '\n' then next_bol - 1 else next_bol
  in
  let character = min position.character (line_limit - bol) in
  {
    Lexing.pos_fname = filename;
    pos_lnum = line + 1;
    pos_bol = bol;
    pos_cnum = bol + character;
  }

let server_capabilities () : ServerCapabilities.t =
  ServerCapabilities.create
    ~textDocumentSync:
      (`TextDocumentSyncOptions
         (TextDocumentSyncOptions.create ~openClose:true
            ~change:TextDocumentSyncKind.Incremental ()))
    ~documentFormattingProvider:(`Bool true)
    ~hoverProvider:(`Bool true)
    ~semanticTokensProvider:
      (`SemanticTokensOptions
         (SemanticTokensOptions.create ~full:(`Bool true) ~range:true
            ~legend:Semantic_tokens.legend ()))
    ()
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

let on_hover (state : state) (params : HoverParams.t) =
  match Document_store.get_doc state.docs params.textDocument.uri with
  | None -> None
  | Some doc -> (
      match doc.pipeline with
      | None -> None
      | Some pipeline -> (
          let filename = DocumentUri.to_path doc.uri in
          let position =
            lex_position_of_lsp_position ~filename ~text:doc.text params.position
          in
          match Hover_info.hover_text_at pipeline.typing position with
          | None -> None
          | Some (loc, contents) ->
              let markup =
                MarkupContent.create ~kind:MarkupKind.Markdown ~value:contents
              in
              Some
                (Hover.create ~contents:(`MarkupContent markup)
                   ~range:(loc_to_range loc) ())))

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
