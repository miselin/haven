include Analysis_types

module Typing = Analysis_typing.Typing
module Semantic = Analysis_semantic.Semantic
module Cleanup = Analysis_cleanup.Cleanup
module Ownership = Analysis_ownership.Ownership

module Pipeline = struct
  type result = {
    core : Core.parsed_program;
    typing : typing_result;
    semantic : semantic_result;
    ownership : ownership_result;
    cleaned : Core.parsed_program;
  }

  let run_core core =
    let typing = Typing.run core in
    let semantic = Semantic.run typing in
    let ownership = Ownership.run typing in
    let cleaned = Cleanup.run typing in
    { core; typing; semantic; ownership; cleaned }

  let run_cst parsed = run_core (Convert.core_of_cst parsed)
end
