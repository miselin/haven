include Analysis_types

module Typing = Analysis_typing.Typing
module Verify = Analysis_verify.Verify
module Semantic = Analysis_semantic.Semantic
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
    purity : purity_result;
    ownership : ownership_result;
    cfold : Core.parsed_program;
    cleaned : Core.parsed_program;
  }

  let run_core core =
    let typing = Typing.run core in
    let verify : verify_result = Verify.run typing in
    let semantic : semantic_result = Semantic.run typing in
    let purity : purity_result = Purity.run typing in
    let ownership : ownership_result = Ownership.run typing in
    let cfold = ConstantFold.run typing in
    let cleaned = Cleanup.run typing in
    { core; typing; verify; semantic; purity; ownership; cfold; cleaned }

  let run_cst parsed =
    let expanded = Imports.expand_cst parsed in
    let result = run_core (Convert.core_of_expanded_cst expanded.parsed) in
    let typing =
      {
        result.typing with
        diagnostics = expanded.diagnostics @ result.typing.diagnostics;
      }
    in
    { result with typing }
end
