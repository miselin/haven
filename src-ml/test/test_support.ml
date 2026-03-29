module Analysis = Haven.Ast.Analysis
module Core = Haven.Ast.Analysis.Core

let assert_true message cond = if not cond then failwith message

let assert_no_diagnostics label diagnostics =
  assert_true (label ^ " unexpectedly produced diagnostics") (diagnostics = [])

let assert_has_diagnostics label diagnostics =
  assert_true (label ^ " unexpectedly produced no diagnostics") (diagnostics <> [])

let string_contains haystack needle =
  let haystack_len = String.length haystack in
  let needle_len = String.length needle in
  let rec loop index =
    if needle_len = 0 then true
    else if index + needle_len > haystack_len then false
    else if String.sub haystack index needle_len = needle then true
    else loop (index + 1)
  in
  loop 0

let assert_diagnostic_message_contains label needle diagnostics =
  match diagnostics with
  | [] -> failwith (label ^ " expected at least one diagnostic")
  | diagnostic :: _ ->
      assert_true (label ^ " did not include the expected diagnostic text")
        (string_contains diagnostic.Analysis.message needle)

let assert_any_diagnostic_message_contains label needle diagnostics =
  assert_true (label ^ " did not include the expected diagnostic text")
    (List.exists
       (fun (diagnostic : Analysis.diagnostic) ->
         string_contains diagnostic.Analysis.message needle)
       diagnostics)

let assert_diagnostic_category label expected_category diagnostics =
  match diagnostics with
  | [] -> failwith (label ^ " expected at least one diagnostic")
  | diagnostic :: _ ->
      assert_true (label ^ " had the wrong diagnostic category")
        (diagnostic.Analysis.category = expected_category)

let assert_has_ownership_action label predicate actions =
  assert_true (label ^ " did not include the expected ownership action")
    (List.exists predicate actions)

let rec remove_tree path =
  if Sys.file_exists path then
    if Sys.is_directory path then (
      Array.iter (fun entry -> remove_tree (Filename.concat path entry))
        (Sys.readdir path);
      Unix.rmdir path)
    else Sys.remove path

let with_temp_dir prefix f =
  let path = Filename.temp_file prefix "" in
  Sys.remove path;
  Unix.mkdir path 0o700;
  Fun.protect ~finally:(fun () -> remove_tree path) (fun () -> f path)

let write_file path contents =
  let ch = open_out path in
  Fun.protect ~finally:(fun () -> close_out_noerr ch) (fun () -> output_string ch contents)

let parse_to_core text = Haven.Ast.Convert.core_of_cst (Haven.Parser.parse_string text)

let find_first_let_binding (program : Core.parsed_program) =
  let rec collect_in_statements acc (statements : Core.statement list) =
    match statements with
    | [] -> List.rev acc
    | stmt :: rest -> (
        match stmt.value with
        | Core.Let binding -> collect_in_statements (binding :: acc) rest
        | Core.Expression _
        | Core.Return _
        | Core.Defer _
        | Core.Break
        | Core.Continue ->
            collect_in_statements acc rest
        | Core.Loop loop ->
            collect_in_statements
              (List.rev_append
                 (List.rev (collect_in_statements [] loop.value.body.value.statements))
                 acc)
              rest)
  in
  let rec collect_in_decls acc (decls : Core.top_decl list) =
    match decls with
    | [] -> List.rev acc
    | decl :: rest -> (
        match decl.value with
        | Core.FDecl fn -> (
            match fn.value.definition with
            | Some body ->
                let acc =
                  List.rev_append
                    (List.rev (collect_in_statements [] body.value.statements))
                    acc
                in
                collect_in_decls acc rest
            | None -> collect_in_decls acc rest)
        | _ -> collect_in_decls acc rest)
  in
  match collect_in_decls [] program.program.value.decls with
  | binding :: _ -> binding
  | [] -> failwith "expected to find a let binding"

let find_let_binding_at index (program : Core.parsed_program) =
  let rec collect_stmt (bindings : Core.let_stmt list) (statements : Core.statement list) =
    match statements with
    | [] -> List.rev bindings
    | stmt :: stmt_rest -> (
        match stmt.value with
        | Core.Let binding ->
            collect_stmt (binding :: bindings) stmt_rest
        | Core.Loop loop ->
            let inner = collect_stmt [] loop.value.body.value.statements in
            collect_stmt (List.rev_append inner bindings) stmt_rest
        | Core.Expression _
        | Core.Return _
        | Core.Defer _
        | Core.Break
        | Core.Continue ->
            collect_stmt bindings stmt_rest)
  in
  let rec collect (bindings : Core.let_stmt list) (decls : Core.top_decl list) =
    match decls with
    | [] -> List.rev bindings
    | decl :: rest -> (
        match decl.value with
        | Core.FDecl fn -> (
            match fn.value.definition with
            | Some body ->
                collect (List.rev_append (collect_stmt [] body.value.statements) bindings) rest
            | None -> collect bindings rest)
        | _ -> collect bindings rest)
  in
  let bindings = collect [] program.program.value.decls in
  match List.nth_opt bindings index with
  | Some binding -> binding
  | None -> failwith "expected to find a let binding"

let find_if_scrutinee (program : Core.parsed_program) =
  let rec find_in_expression (expr : Core.expression) =
    match expr.value with
    | Core.Match match_expr -> Some match_expr.value.expr
    | Core.Block block -> Option.bind block.value.result find_in_expression
    | _ -> None
  in
  let rec find_in_decls (decls : Core.top_decl list) =
    match decls with
    | [] -> None
    | decl :: rest -> (
        match decl.value with
        | Core.FDecl fn -> (
            match fn.value.definition with
            | Some body -> Option.bind body.value.result find_in_expression
            | None -> find_in_decls rest)
        | _ -> find_in_decls rest)
  in
  match find_in_decls program.program.value.decls with
  | Some expr -> expr
  | None -> failwith "expected to find lowered if scrutinee"

let find_named_function name (program : Core.parsed_program) =
  let rec loop (decls : Core.top_decl list) =
    match decls with
    | [] -> failwith ("expected to find function " ^ name)
    | (decl : Core.top_decl) :: rest -> (
        match decl.value with
        | Core.FDecl fn when fn.value.name.value = name -> fn
        | Core.Foreign foreign -> (
            match
              List.find_opt
                (fun (fn : Core.function_decl) -> fn.value.name.value = name)
                foreign.value.decls
            with
            | Some fn -> fn
            | None -> loop rest)
        | Core.FDecl _ | Core.TDecl _ | Core.VDecl _ | Core.Import _ | Core.CImport _ ->
            loop rest)
  in
  loop program.program.value.decls
