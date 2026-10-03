open Test_support

let run () =
  List.iter
    (fun (label, prefix, operand, expect_type) ->
      let source =
        prefix
        ^ "pub impure fn boundary(fvec2 vector) -> float = fold each value of \
           unbox box " ^ operand ^ " with acc = 0.0 { acc + value };"
      in
      let core = parse_to_core source in
      let fn = find_named_function "boundary" core in
      (match
         (Option.get (Option.get fn.value.definition).value.result).value
       with
      | Core.Block
          {
            value = { statements = { value = Core.Let snapshot; _ } :: _; _ };
            _;
          } -> (
          match snapshot.value.init_expr.value with
          | Core.Unbox boxed -> (
              match (boxed.value, expect_type) with
              | Core.BoxExpr { value = Core.Identifier id; _ }, false ->
                  assert_true
                    (label ^ " boxes the variable value")
                    (id.value = "vector")
              | Core.BoxType { value = Core.CustomType custom; _ }, true ->
                  assert_true
                    (label ^ " resolves the registered type after parsing")
                    (custom.name.value = "Vector")
              | _ -> failwith (label ^ " incorrect boxing lowering"))
          | _ -> failwith "expected unboxed snapshot")
      | _ -> failwith "expected fold source snapshot");
      let pipeline = Analysis.Pipeline.run_core core in
      List.iter
        (assert_no_diagnostics label)
        [
          pipeline.typing.diagnostics;
          pipeline.semantic.diagnostics;
          pipeline.verify.diagnostics;
          pipeline.ownership.diagnostics;
        ])
    [
      ("fold WITH/value boundary", "", "vector", false);
      ("fold WITH/type boundary", "type Vector = fvec2;\n", "Vector", true);
    ];
  let fold_source =
    "fn fold_sum(fvec3 v) -> float = fold each value of v with acc = 0.0 { acc \
     + value };"
  in
  let fold_core = parse_to_core fold_source in
  let fold_fn = find_named_function "fold_sum" fold_core in
  (match
     (Option.get (Option.get fold_fn.value.definition).value.result).value
   with
  | Core.Block outer -> (
      match (outer.value.statements, outer.value.result) with
      | ( [
            { value = Core.Let source; _ };
            { value = Core.Let seed; _ };
            { value = Core.Loop loop; _ };
          ],
          Some result ) -> (
          assert_true "fold snapshots source before seed outside the loop"
            (source.value.name.value <> seed.value.name.value
            && seed.value.mut && (not source.value.mut)
            && List.length loop.value.init = 1);
          (match result.value with
          | Core.Identifier name ->
              assert_true "fold returns its final private accumulator"
                (name.value = seed.value.name.value)
          | _ -> failwith "expected fold final accumulator reference");
          match loop.value.body.value.statements with
          | [
           { value = Core.Let value; _ };
           { value = Core.Let accumulator; _ };
           { value = Core.Expression update; _ };
          ] -> (
              assert_true
                "fold value and current accumulator are immutable copies"
                ((not value.value.mut) && not accumulator.value.mut);
              assert_true
                "fold generated bindings have distinct analysis identities"
                (Analysis.binding_id source <> Analysis.binding_id seed
                && Analysis.binding_id value <> Analysis.binding_id accumulator
                );
              match update.value with
              | Core.Assign
                  { value = { value = { value = Core.Block body; _ }; _ }; _ }
                ->
                  assert_true
                    "fold control region metadata marks only the original body"
                    (body.value.fold_body
                    && (not loop.value.body.value.fold_body)
                    && not outer.value.fold_body);
                  assert_true "fold body preserves its source braces"
                    (body.loc.start_pos.pos_cnum = String.index fold_source '{'
                    && body.loc.end_pos.pos_cnum
                       = String.index fold_source '}' + 1)
              | _ -> failwith "expected ordinary fold accumulator assignment")
          | _ -> failwith "expected immutable fold bindings and one update")
      | _ -> failwith "expected source, seed, loop and result")
  | _ -> failwith "fold must lower to an ordinary Core value block");
  let fold_generic =
    parse_to_core
      {|fn total(mat? m) -> float = fold each row of m with acc = 0.0 {
    acc + (fold each value of row with subtotal = 0.0 { subtotal + value })
  };
  pub fn first() -> float = total(Mat<Vec<1.0, 2.0, 3.0>, Vec<4.0, 5.0, 6.0>>);
  pub fn second() -> float = total(Mat<Vec<1.0, 2.0>, Vec<3.0, 4.0>, Vec<5.0, 6.0>>);|}
    |> Analysis.Pipeline.run_core
  in
  List.iter
    (assert_no_diagnostics "generic nested fold")
    [
      fold_generic.typing.diagnostics;
      fold_generic.semantic.diagnostics;
      fold_generic.verify.diagnostics;
      fold_generic.purity.diagnostics;
      fold_generic.ownership.diagnostics;
    ];
  let fold_printed =
    Haven.Ast.Pretty.core_program_to_string fold_generic.cleaned
  in
  assert_true
    "nested folds instantiate both matrix shapes without internal cardinality \
     fields"
    (string_contains fold_printed "total__spec__mat2x3"
    && string_contains fold_printed "total__spec__mat3x2"
    && not (string_contains fold_printed "$iter.count"));
  let truthiness =
    parse_to_core "fn choose(i32 value) -> i32 = if value { 1 } else { 0 };"
  in
  let choose = find_named_function "choose" truthiness in
  (match
     (Option.get (Option.get choose.value.definition).value.result).value
   with
  | Core.Match m -> (
      match m.value.expr.value with
      | Core.ToBool inner ->
          assert_true
            "Boolean conversion and scalar keep distinct analysis identities"
            (Analysis.expr_id m.value.expr <> Analysis.expr_id inner);
          assert_true "Boolean conversion keeps the condition diagnostic span"
            (m.value.expr.loc = inner.loc)
      | _ -> failwith "expected Boolean conversion")
  | _ -> failwith "expected conditional lowering");

  let foreach_core =
    parse_to_core
      "fn walk(fvec3 v) -> void { iter each value of v indexed by index {}; }"
  in
  let walk = find_named_function "walk" foreach_core in
  (match (Option.get walk.value.definition).value.statements with
  | [ { value = Core.Loop loop; _ } ] ->
      assert_true "iteration source and private counter are initialized once"
        (List.length loop.value.init = 2);
      (match loop.value.cond.value with
      | Core.Binary comparison ->
          assert_true
            "value iteration uses a strict bound, safe for an empty array"
            (comparison.value.op = Core.LessThan)
      | _ -> failwith "expected cardinality comparison");
      assert_true "loop body binds a copied value and immutable ordinal"
        (List.for_all
           (fun (stmt : Core.statement) ->
             match stmt.value with
             | Core.Let binding -> not binding.value.mut
             | _ -> false)
           loop.value.body.value.statements)
  | _ -> failwith "expected ordinary Core loop lowering");
  let foreach_generic =
    parse_to_core
      {|fn total(mat? m) -> float {
        let mut result = 0.0;
        iter each row of m { iter each value of row { result = result + value; }; };
        result
      }
      pub fn first() -> float = total(Mat<Vec<1.0, 2.0, 3.0>, Vec<4.0, 5.0, 6.0>>);
      pub fn second() -> float = total(Mat<Vec<1.0, 2.0>, Vec<3.0, 4.0>, Vec<5.0, 6.0>>);|}
    |> Analysis.Pipeline.run_core
  in
  List.iter
    (assert_no_diagnostics "generic nested value iteration")
    [
      foreach_generic.typing.diagnostics;
      foreach_generic.verify.diagnostics;
      foreach_generic.semantic.diagnostics;
      foreach_generic.purity.diagnostics;
      foreach_generic.ownership.diagnostics;
    ];
  let snapshots =
    List.filter_map
      (fun (decl : Core.top_decl) ->
        match decl.value with
        | Core.FDecl fn when fn.analysis_scope <> None -> (
            let body = Option.get fn.value.definition in
            match body.value.statements with
            | _ :: { value = Core.Loop loop; _ } :: _ -> (
                match loop.value.init with
                | { value = Core.Let binding; _ } :: _ -> Some binding
                | _ -> None)
            | _ -> None)
        | _ -> None)
      foreach_generic.core.program.value.decls
  in
  (match snapshots with
  | [ a; b ] ->
      assert_true
        "generated snapshot source spans remain unchanged across clones"
        (a.loc = b.loc);
      assert_true "generated snapshot IDs distinguish clones"
        (Analysis.binding_id a <> Analysis.binding_id b);
      List.iter
        (fun (binding : Core.let_stmt) ->
          let scope = Option.get binding.analysis_scope in
          assert_true "local generated identity survives specialization"
            (string_contains scope "/$foreach.node."))
        snapshots
  | _ -> failwith "expected two matrix iteration specializations");

  let typed_program =
    parse_to_core "pub fn main() -> void { let x = 5; }" |> Analysis.Typing.run
  in
  assert_no_diagnostics "typing integer inference" typed_program.diagnostics;
  let binding = find_first_let_binding typed_program.program in
  let binding_ann =
    Hashtbl.find typed_program.annotations.bindings
      (Analysis.binding_id binding)
  in
  (match binding_ann.inferred_type with
  | Some ty -> (
      match ty.value with
      | Core.NumericType { signedness = Haven.Token.Signed; bits = native_bits }
        when native_bits = Sys.word_size ->
          ()
      | _ -> failwith "expected let x = 5 to infer the signed native word type")
  | None -> failwith "expected inferred type for let binding");
  (match binding_ann.metavar.integer with
  | Some { exact_value = Some 5; minimum_bits = Some 3; _ } -> ()
  | _ -> failwith "expected integer metavar to record exact value and width");

  let target_profile : Analysis.target_profile =
    { native_integer_bits = 16; c_integer_bits = 16 }
  in
  let typed_16 =
    parse_to_core "pub fn main() -> void { let x = 5; }"
    |> Analysis.Typing.run ~target_profile
  in
  let binding_16 = find_first_let_binding typed_16.program in
  let binding_16_ann =
    Hashtbl.find typed_16.annotations.bindings (Analysis.binding_id binding_16)
  in
  (match binding_16_ann.resolved_type with
  | Some (Analysis.ResolvedInt (Haven.Token.Signed, 16)) -> ()
  | _ -> failwith "expected target profile to select a signed 16-bit default");

  let pipeline =
    Haven.Parser.parse_string "pub fn main() -> i32 { if !0 { 1 } else { 0 } }"
    |> Analysis.Pipeline.run_cst
  in
  assert_no_diagnostics "semantic cleanup input typing"
    pipeline.typing.diagnostics;
  assert_no_diagnostics "semantic cleanup input verify"
    pipeline.verify.diagnostics;
  assert_no_diagnostics "semantic cleanup input semantic"
    pipeline.semantic.diagnostics;
  let scrutinee_before = find_if_scrutinee pipeline.core in
  let scrutinee_folded = find_if_scrutinee pipeline.cfold in
  let scrutinee_after = find_if_scrutinee pipeline.cleaned in
  (match scrutinee_before.value with
  | Core.ToBool _ -> ()
  | _ ->
      failwith "expected lowered if condition to contain ToBool before cleanup");
  (match scrutinee_folded.value with
  | Core.Literal lit -> (
      match lit.value with
      | Core.Bool true -> ()
      | _ ->
          failwith
            "expected constant folding to reduce the if condition to true")
  | _ ->
      failwith
        "expected constant folding to reduce the if condition to a literal");
  (match scrutinee_after.value with
  | Core.Literal lit -> (
      match lit.value with
      | Core.Bool true -> ()
      | _ ->
          failwith "expected cleanup to preserve the folded boolean condition")
  | _ -> failwith "expected cleanup to preserve the folded boolean condition");

  let folded_pipeline =
    parse_to_core "pub fn main() -> i32 { let x = 1 + 2 * 3; x }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "constant folding typing"
    folded_pipeline.typing.diagnostics;
  assert_no_diagnostics "constant folding semantic"
    folded_pipeline.semantic.diagnostics;
  let folded_binding = find_first_let_binding folded_pipeline.cfold in
  (match folded_binding.value.init_expr.value with
  | Core.Literal lit -> (
      match lit.value with
      | Core.Integer 7 -> ()
      | _ -> failwith "expected arithmetic constant folding to produce 7")
  | _ -> failwith "expected arithmetic constant folding to produce a literal");

  let specialization_pipeline =
    parse_to_core
      "fn vadd(fvec? a, fvec? b) { a + b }\n\
       fn width(mat? m) { m.cols }\n\
       fn main() -> i32 { 0 }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "specialization typing"
    specialization_pipeline.typing.diagnostics;
  assert_no_diagnostics "specialization verify"
    specialization_pipeline.verify.diagnostics;
  assert_no_diagnostics "specialization semantic"
    specialization_pipeline.semantic.diagnostics;

  let specialization_call_pipeline =
    parse_to_core
      "fn vadd(fvec? a, fvec? b) { a + b }\n\
       fn main() -> fvec3 {\n\
      \  vadd(Vec<1.0, 2.0, 3.0>, Vec<4.0, 5.0, 6.0>)\n\
       }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "specialization call typing"
    specialization_call_pipeline.typing.diagnostics;
  assert_no_diagnostics "specialization call verify"
    specialization_call_pipeline.verify.diagnostics;
  assert_no_diagnostics "specialization call semantic"
    specialization_call_pipeline.semantic.diagnostics;
  let specialization_core =
    Haven.Ast.Pretty.core_program_to_string specialization_call_pipeline.cleaned
  in
  assert_true "specialized pipeline should emit a concrete clone"
    (string_contains specialization_core "vadd__spec__fvec3__fvec3");
  assert_true
    "specialized pipeline should erase hole types from the lowered program"
    (not (string_contains specialization_core "fvec?"));

  let shape_property_pipeline =
    parse_to_core
      "fn width(mat? m) { m.cols }\n\
       pub fn main() -> u32 {\n\
      \  width(Mat<Vec<1.0, 2.0>, Vec<3.0, 4.0>>)\n\
       }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "shape property typing"
    shape_property_pipeline.typing.diagnostics;
  assert_no_diagnostics "shape property verify"
    shape_property_pipeline.verify.diagnostics;
  assert_no_diagnostics "shape property semantic"
    shape_property_pipeline.semantic.diagnostics;
  let shape_property_core =
    Haven.Ast.Pretty.core_program_to_string shape_property_pipeline.cleaned
  in
  assert_true "shape property specialization should clone the function"
    (string_contains shape_property_core "width__spec__mat2x2");
  assert_true "shape properties should lower to integer literals before LLVM"
    (string_contains shape_property_core "Literal(2)");

  let get_mat_row_pipeline =
    parse_to_core
      "fn get_mat_row(mat? m, u32 row) { m[row] }\n\
       pub fn main() -> fvec3 {\n\
      \  get_mat_row(Mat<Vec<1.0, 2.0, 3.0>, Vec<4.0, 5.0, 6.0>>, 1)\n\
       }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "get_mat_row typing"
    get_mat_row_pipeline.typing.diagnostics;
  assert_no_diagnostics "get_mat_row verify"
    get_mat_row_pipeline.verify.diagnostics;
  assert_no_diagnostics "get_mat_row semantic"
    get_mat_row_pipeline.semantic.diagnostics;
  let get_mat_row_core =
    Haven.Ast.Pretty.core_program_to_string get_mat_row_pipeline.cleaned
  in
  assert_true "get_mat_row should specialize to a concrete matrix helper"
    (string_contains get_mat_row_core "get_mat_row__spec__mat2x3");
  assert_true "get_mat_row specialization should infer a concrete vector return"
    (string_contains get_mat_row_core "return=fvec3");

  let specialization_dedup_pipeline =
    parse_to_core
      "fn get_mat_row(mat? m, u32 row) { m[row] }\n\
       pub fn main() -> fvec3 {\n\
      \  let u32 row = 1;\n\
      \  get_mat_row(Mat<Vec<1.0, 2.0, 3.0>, Vec<4.0, 5.0, 6.0>>, row)\n\
       }\n\
       pub fn other() -> fvec3 {\n\
      \  get_mat_row(Mat<Vec<7.0, 8.0, 9.0>, Vec<10.0, 11.0, 12.0>>, 1)\n\
       }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "specialization dedup typing"
    specialization_dedup_pipeline.typing.diagnostics;
  let specialization_dedup_core =
    Haven.Ast.Pretty.core_program_to_string
      specialization_dedup_pipeline.cleaned
  in
  assert_true "equivalent concrete signatures should reuse one specialization"
    (count_occurrences specialization_dedup_core
       "name=get_mat_row__spec__mat2x3__u32"
    = 1);

  let compile_assert_pipeline =
    parse_to_core
      "fn mat_width_eq(mat? a, mat? b) {\n\
      \  @assert a.cols == b.cols, \"matrix widths must match\";\n\
      \  a.cols\n\
       }\n\
       pub fn main() -> u32 {\n\
      \  mat_width_eq(Mat<Vec<1.0, 2.0>, Vec<3.0, 4.0>>, Mat<Vec<5.0, 6.0>, \
       Vec<7.0, 8.0>>)\n\
       }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "compile assert typing"
    compile_assert_pipeline.typing.diagnostics;
  assert_no_diagnostics "compile assert verify"
    compile_assert_pipeline.verify.diagnostics;
  assert_no_diagnostics "compile assert semantic"
    compile_assert_pipeline.semantic.diagnostics;
  assert_no_diagnostics "compile assert pass"
    compile_assert_pipeline.asserts.diagnostics;
  let compile_assert_core =
    Haven.Ast.Pretty.core_program_to_string compile_assert_pipeline.cleaned
  in
  assert_true
    "successful compile asserts should be erased before the cleaned AST"
    (not (string_contains compile_assert_core "CompileAssert"));

  let compile_assert_fail_pipeline =
    parse_to_core
      "fn mat_width_eq(mat? a, mat? b) {\n\
      \  @assert a.cols == b.cols, \"matrix widths must match\";\n\
      \  a.cols\n\
       }\n\
       pub fn main() -> u32 {\n\
      \  mat_width_eq(Mat<Vec<1.0, 2.0>, Vec<3.0, 4.0>>, Mat<Vec<5.0>, \
       Vec<6.0>>)\n\
       }"
    |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "failing compile assert should produce diagnostics"
    compile_assert_fail_pipeline.asserts.diagnostics;
  assert_any_diagnostic_message_contains
    "failing compile assert should preserve the user message"
    "matrix widths must match" compile_assert_fail_pipeline.asserts.diagnostics;
  assert_any_diagnostic_message_contains
    "failing compile assert should include the rendered condition"
    "compile-time assertion failed: a.cols == b.cols"
    compile_assert_fail_pipeline.asserts.diagnostics;
  assert_any_diagnostic_message_contains
    "failing compile assert should include the specialized condition"
    "specialized as: 2 == 1" compile_assert_fail_pipeline.asserts.diagnostics;

  let compile_assert_short_circuit_pipeline =
    parse_to_core
      "fn vadd(fvec? a, fvec? b) {\n\
      \  @assert a.dim == b.dim, \"vector dimensions must match\";\n\
      \  a + b\n\
       }\n\
       pub fn main() -> fvec3 {\n\
      \  vadd(Vec<1.0, 2.0, 3.0>, Vec<4.0, 5.0>)\n\
       }"
    |> Analysis.Pipeline.run_core
  in
  assert_has_diagnostics "failing compile assert should still report the assert"
    compile_assert_short_circuit_pipeline.asserts.diagnostics;
  assert_any_diagnostic_message_contains
    "failing compile assert should keep the user-facing message"
    "vector dimensions must match"
    compile_assert_short_circuit_pipeline.asserts.diagnostics;
  assert_any_diagnostic_message_contains
    "failing compile assert should include the rendered vector condition"
    "compile-time assertion failed: a.dim == b.dim"
    compile_assert_short_circuit_pipeline.asserts.diagnostics;
  assert_any_diagnostic_message_contains
    "failing compile assert should include the specialized vector condition"
    "specialized as: 3 == 2"
    compile_assert_short_circuit_pipeline.asserts.diagnostics;
  assert_no_diagnostics
    "failing compile assert should short-circuit later semantic analysis"
    compile_assert_short_circuit_pipeline.semantic.diagnostics;

  let identity_pipeline =
    parse_to_core
      "fn product(fvec? a, fvec? b) { let p = a * b; p }\n\
       pub fn main() -> float {\n\
      \  let small = product(Vec<1.0, 2.0>, Vec<3.0, 4.0>);\n\
      \  let large = product(Vec<1.0, 2.0, 3.0>, Vec<4.0, 5.0, 6.0>);\n\
      \  small.y + large.z\n\
       }"
    |> Analysis.Pipeline.run_core
  in
  assert_no_diagnostics "multi-shape pipeline"
    (Analysis.Pipeline.analysis_diagnostics identity_pipeline);
  let specialized_functions (program : Core.parsed_program) =
    List.filter_map
      (fun (decl : Core.top_decl) ->
        match decl.value with
        | Core.FDecl fn when fn.analysis_scope <> None -> Some fn
        | _ -> None)
      program.program.value.decls
  in
  let small, large =
    match specialized_functions identity_pipeline.core with
    | [ small; large ] ->
        if small.value.name.value = "product__spec__fvec2__fvec2" then
          (small, large)
        else (large, small)
    | _ -> failwith "expected two product specializations"
  in
  let body_binding (fn : Core.function_decl) =
    let body = Option.get fn.value.definition in
    match body.value.statements with
    | [ stmt ] -> (
        match stmt.value with
        | Core.Let binding -> (body, stmt, binding)
        | _ -> failwith "expected product local binding")
    | _ -> failwith "expected one product statement"
  in
  let small_body, small_stmt, small_binding = body_binding small in
  let large_body, large_stmt, large_binding = body_binding large in
  assert_true "clone source spans remain unchanged"
    (small.loc = large.loc
    && small_body.loc = large_body.loc
    && small_stmt.loc = large_stmt.loc
    && small_binding.loc = large_binding.loc
    && small_binding.value.init_expr.loc = large_binding.value.init_expr.loc);
  assert_true "cloned analysis and ownership identities must be distinct"
    (Analysis.function_id small <> Analysis.function_id large
    && Analysis.block_id small_body <> Analysis.block_id large_body
    && Analysis.statement_id small_stmt <> Analysis.statement_id large_stmt
    && Analysis.binding_id small_binding <> Analysis.binding_id large_binding
    && Analysis.expr_id small_binding.value.init_expr
       <> Analysis.expr_id large_binding.value.init_expr);
  let assert_width label expected binding =
    let ann =
      Hashtbl.find identity_pipeline.typing.annotations.bindings
        (Analysis.binding_id binding)
    in
    match ann.resolved_type with
    | Some (Analysis.ResolvedVec vec) when vec.dimension = expected -> ()
    | _ -> failwith label
  in
  assert_width "small local type must remain fvec2" 2 small_binding;
  assert_width "large local type must remain fvec3" 3 large_binding;
  List.iter
    (fun (program : Core.parsed_program) ->
      List.iter
        (fun (fn : Core.function_decl) ->
          let body, _, binding = body_binding fn in
          assert_true "folding and cleanup preserve scoped identities"
            (body.analysis_scope = fn.analysis_scope
            && binding.analysis_scope = fn.analysis_scope
            && binding.value.init_expr.analysis_scope = fn.analysis_scope))
        (specialized_functions program))
    [ identity_pipeline.cfold; identity_pipeline.cleaned ]
