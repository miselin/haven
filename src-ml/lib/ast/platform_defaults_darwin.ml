let is_host () =
  match Platform_defaults_common.capture_first_line "uname" [ "-s" ] with
  | Some "Darwin" -> true
  | _ -> false

let resolve_sysroot sysroot =
  let sysroot =
    match sysroot with
    | Some _ -> sysroot
    | None -> (
        match Platform_defaults_common.getenv_nonempty "SDKROOT" with
        | Some path -> Platform_defaults_common.maybe_set_sysroot sysroot path
        | None -> sysroot)
  in
  match sysroot with
  | Some _ -> sysroot
  | None -> (
      match Platform_defaults_common.capture_first_line "xcrun" [ "--show-sdk-path" ] with
      | Some path -> Platform_defaults_common.maybe_set_sysroot sysroot path
      | None -> None)
