module Lsp = Linol.Lsp

module Server = struct
  class haven_lsp_server =
    object (_self)
      inherit Linol_lwt.Jsonrpc2.server as super
      val state = Haven_lsp.create_state ()

      method! on_req_initialize ~notify_back:_
          (params : Lsp.Types.InitializeParams.t) =
        Logs.info (fun m ->
            m "initialize: rootUri=%s"
              (match params.rootUri with
              | None -> "<none>"
              | Some u -> Lsp.Types.DocumentUri.to_string u));

        let result = Haven_lsp.on_initialize state params in
        Lwt.return result

      method on_notif_doc_did_open ~notify_back
          (doc : Lsp.Types.TextDocumentItem.t) ~content:_ =
        Logs.info (fun m ->
            m "didOpen: %s (version=%d)" (Lsp.Uri.to_string doc.uri) doc.version);
        Haven_lsp.on_did_open state doc;
        (match Haven_lsp.publish_diagnostics_params state doc.uri with
        | None -> Lwt.return_unit
        | Some params ->
            notify_back#send_notification
              (Lsp.Server_notification.PublishDiagnostics params))

      method on_notif_doc_did_close ~notify_back
          (id : Lsp.Types.TextDocumentIdentifier.t) =
        Logs.info (fun m -> m "didClose: %s" (Lsp.Uri.to_string id.uri));
        Haven_lsp.on_did_close state id;
        notify_back#send_notification
          (Lsp.Server_notification.PublishDiagnostics
             (Lsp.Types.PublishDiagnosticsParams.create ~uri:id.uri ~diagnostics:[]
                ()))

      method on_notif_doc_did_change ~notify_back
          (id : Lsp.Types.VersionedTextDocumentIdentifier.t)
          (changes : Lsp.Types.TextDocumentContentChangeEvent.t list)
          ~old_content:_ ~new_content:_ =
        Logs.info (fun m ->
            m "didChange: %s (version=%d, %d changes)"
              (Lsp.Uri.to_string id.uri) id.version (List.length changes));
        Haven_lsp.on_did_change state id changes;
        (match Haven_lsp.publish_diagnostics_params state id.uri with
        | None -> Lwt.return_unit
        | Some params ->
            notify_back#send_notification
              (Lsp.Server_notification.PublishDiagnostics params))

      method! on_req_hover ~notify_back:_ ~id:_ ~uri ~pos ~workDoneToken
          (_doc_state : Linol_lwt.Jsonrpc2.doc_state) =
        let params =
          Lsp.Types.HoverParams.create ~position:pos
            ~textDocument:(Lsp.Types.TextDocumentIdentifier.create ~uri)
            ?workDoneToken ()
        in
        Lwt.return (Haven_lsp.on_hover state params)

      method! on_request_unhandled : type r.
          notify_back:Linol_lwt.Jsonrpc2.notify_back ->
          id:Linol_lwt.Jsonrpc2.Req_id.t ->
          r Lsp.Client_request.t ->
          r Linol_lwt.IO_lwt.t =
        fun ~notify_back ~id req ->
          match req with
          | Lsp.Client_request.SemanticTokensFull params ->
              Linol_lwt.IO_lwt.return
                (Haven_lsp.on_semantic_tokens_full state params)
          | Lsp.Client_request.SemanticTokensRange params ->
              Linol_lwt.IO_lwt.return
                (Haven_lsp.on_semantic_tokens_range state params)
          | Lsp.Client_request.SemanticTokensDelta _params ->
              Linol_lwt.IO_lwt.return None
          | Lsp.Client_request.TextDocumentFormatting params ->
              Linol_lwt.IO_lwt.return (Haven_lsp.on_formatting state params)
          | _ -> super#on_request_unhandled ~notify_back ~id req

      method spawn_query_handler f = Linol_lwt.spawn f
    end
end

let () =
  Format.set_formatter_out_channel stderr;
  Fmt_tty.setup_std_outputs ();
  Logs.set_reporter (Logs_fmt.reporter ());
  Logs.set_level (Some Logs.Info);

  Logs.info (fun m -> m "Welcome to the Haven LSP!");

  let s = new Server.haven_lsp_server in
  let server = Linol_lwt.Jsonrpc2.create_stdio ~env:() s in
  let task =
    let shutdown () = s#get_status = `ReceivedExit in
    Linol_lwt.Jsonrpc2.run ~shutdown server
  in
  match Linol_lwt.run task with
  | () -> ()
  | exception e ->
      let e = Printexc.to_string e in
      Printf.eprintf "error: %s\n%!" e;
      exit 1
