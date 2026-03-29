let () =
  Test_pipeline.run ();
  Test_typing_semantics.run ();
  Test_ownership.run ();
  Test_purity.run ();
  Test_imports.run ()
