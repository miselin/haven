type resolved = {
  search_dirs : string list;
  sysroot : string option;
}

let resolve ?(search_dirs = []) ?sysroot () =
  let search_dirs = List.filter_map Platform_defaults_common.existing_dir search_dirs in
  let search_dirs, sysroot =
    match Platform_defaults_common.getenv_nonempty "NIX_CFLAGS_COMPILE" with
    | Some flags -> Platform_defaults_common.apply_env_cflags ~search_dirs ~sysroot flags
    | None -> (search_dirs, sysroot)
  in
  let sysroot =
    if Platform_defaults_darwin.is_host () then Platform_defaults_darwin.resolve_sysroot sysroot
    else sysroot
  in
  let search_dirs = Platform_defaults_unix.append_default_include ~search_dirs ~sysroot in
  let search_dirs =
    match Platform_defaults_common.capture_first_line "clang" [ "-print-resource-dir" ] with
    | Some path ->
        Platform_defaults_common.append_dir_if_exists search_dirs
          (Platform_defaults_common.resource_include_dir path)
    | None -> search_dirs
  in
  { search_dirs = Platform_defaults_common.dedupe search_dirs; sysroot }
