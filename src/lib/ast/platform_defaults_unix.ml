let append_default_include ~search_dirs ~sysroot =
  match sysroot with
  | Some root ->
      Platform_defaults_common.append_dir_if_exists search_dirs
        (Platform_defaults_common.join_path root "usr/include")
  | None -> Platform_defaults_common.append_dir_if_exists search_dirs "/usr/include"
