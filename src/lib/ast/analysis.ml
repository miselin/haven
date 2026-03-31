include Analysis_types

module Typing = Analysis_typing.Typing
module Verify = Analysis_verify.Verify
module Semantic = Analysis_semantic.Semantic
module Assert = Analysis_asserts.Assert
module Specialize = Analysis_specialize.Specialize
module ConstantFold = Analysis_cfold.ConstantFold
module Purity = Analysis_purity.Purity
module Cleanup = Analysis_cleanup.Cleanup
module Ownership = Analysis_ownership.Ownership

module Pipeline = struct
  type result = {
    core : Core.parsed_program;
    typing : typing_result;
    verify : verify_result;
    semantic : semantic_result;
    asserts : semantic_result;
    purity : purity_result;
    ownership : ownership_result;
    cfold : Core.parsed_program;
    cleaned : Core.parsed_program;
  }

  let run_analyses ?(check_asserts = true) core =
    let typing = Typing.run core in
    let assert_result =
      if check_asserts then Assert.run typing
      else ({ program = typing.program; diagnostics = [] } : Assert.result)
    in
    let asserted_typed = { typing with program = assert_result.program } in
    let assert_failed =
      List.exists
        (fun (diagnostic : diagnostic) -> diagnostic.level = Error)
        assert_result.diagnostics
    in
    let verify : verify_result =
      if assert_failed then { diagnostics = [] } else Verify.run asserted_typed
    in
    let semantic : semantic_result =
      if assert_failed then { diagnostics = [] } else Semantic.run asserted_typed
    in
    let purity : purity_result =
      if assert_failed then { diagnostics = [] } else Purity.run asserted_typed
    in
    let ownership : ownership_result =
      if assert_failed then
        { actions = []; index = Ownership.make_index (); diagnostics = [] }
      else Ownership.run asserted_typed
    in
    let cfold =
      if assert_failed then assert_result.program
      else ConstantFold.run ~program:assert_result.program asserted_typed
    in
    let cleaned =
      if assert_failed then assert_result.program
      else Cleanup.run ~program:cfold asserted_typed
    in
    {
      core;
      typing = asserted_typed;
      verify;
      semantic;
      asserts = { diagnostics = assert_result.diagnostics };
      purity;
      ownership;
      cfold;
      cleaned;
    }

  let has_errors diagnostics =
    List.exists (fun (diagnostic : diagnostic) -> diagnostic.level = Error) diagnostics

  let analysis_diagnostics result =
    result.typing.diagnostics @ result.verify.diagnostics @ result.semantic.diagnostics
    @ result.asserts.diagnostics @ result.purity.diagnostics @ result.ownership.diagnostics

  let run_core core =
    let initial = run_analyses ~check_asserts:false core in
    if has_errors (analysis_diagnostics initial) then initial
    else
      let specialized = Specialize.run initial.typing in
      if has_errors specialized.diagnostics then
        {
          initial with
          typing =
            {
              initial.typing with
              diagnostics = initial.typing.diagnostics @ specialized.diagnostics;
            };
        }
      else run_analyses specialized.program

  let run_cst ?(search_dirs = []) ?sysroot parsed =
    let expanded = Imports.expand_cst ~search_dirs ?sysroot parsed in
    let result = run_core (Convert.core_of_expanded_cst expanded.parsed) in
    let typing =
      {
        result.typing with
        diagnostics = expanded.diagnostics @ result.typing.diagnostics;
      }
    in
    { result with typing }
end
