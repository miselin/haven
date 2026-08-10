open Linol.Lsp.Types

module Analysis = Haven.Ast.Analysis
module Core = Analysis.Core

let command_name = "haven.show-code-lens"

let lens_command title = Command.create ~command:command_name ~title ()

let lens range title =
  CodeLens.create ~range ~command:(lens_command title) ()

let function_lenses (pipeline : Analysis.Pipeline.result) (fn : Core.function_decl) =
  let range = Lsp_helpers.loc_to_range fn.value.name.loc in
  let titles =
    List.filter_map
      (fun title -> title)
      [
        Some (if fn.value.impure then "impure" else "pure");
        (match fn.value.visibility with
        | Haven_core.Visibility.File -> None
        | Module -> Some "module"
        | External -> Some "public");
        Option.map
          (fun (intrinsic : Core.intrinsic) ->
            "intrinsic " ^ intrinsic.value.name.value)
          fn.value.intrinsic;
        let exit_actions =
          List.length
            (Analysis.Ownership.actions_on_function_exit pipeline.ownership fn)
        in
        if exit_actions = 0 then None
        else Some (Printf.sprintf "exit ownership %d" exit_actions);
      ]
  in
  List.map (lens range) titles

let code_lenses current_file (pipeline : Analysis.Pipeline.result) =
  List.concat_map
    (fun (decl : Core.top_decl) ->
      match decl.value with
      | Core.FDecl fn ->
          if String.equal fn.value.name.loc.start_pos.pos_fname current_file then
            function_lenses pipeline fn
          else []
      | Core.Foreign foreign ->
          List.concat_map
            (fun (fn : Core.function_decl) ->
              if String.equal fn.value.name.loc.start_pos.pos_fname current_file then
                function_lenses pipeline fn
              else [])
            foreign.value.decls
      | Core.TDecl _ | Core.VDecl _ | Core.Import _ | Core.CImport _ ->
          [])
    pipeline.typing.program.program.value.decls
