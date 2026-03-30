type resolved = {
  search_dirs : string list;
  sysroot : string option;
}

let is_space = function ' ' | '\t' | '\n' | '\r' -> true | _ -> false

let is_directory path =
  if String.equal path "" then false
  else
    try (Unix.stat path).Unix.st_kind = Unix.S_DIR with Unix.Unix_error _ -> false

let existing_dir path =
  if is_directory path then
    try Some (Unix.realpath path) with Unix.Unix_error _ -> Some path
  else None

let add_dir_if_exists dirs path =
  match existing_dir path with Some dir -> dir :: dirs | None -> dirs

let append_dir_if_exists dirs path =
  match existing_dir path with Some dir -> dirs @ [ dir ] | None -> dirs

let dedupe paths =
  let seen = Hashtbl.create (List.length paths) in
  List.filter
    (fun path ->
      if Hashtbl.mem seen path then false
      else (
        Hashtbl.add seen path ();
        true))
    paths

let join_path base suffix =
  if Filename.is_relative suffix then Filename.concat base suffix else suffix

let getenv_nonempty name =
  match Sys.getenv_opt name with
  | Some value when not (String.equal (String.trim value) "") -> Some value
  | _ -> None

let next_token text index =
  let len = String.length text in
  let rec skip index =
    if index < len && is_space text.[index] then skip (index + 1) else index
  in
  let index = skip index in
  if index >= len then None
  else
    let rec take index =
      if index < len && not (is_space text.[index]) then take (index + 1) else index
    in
    let next = take index in
    Some (String.sub text index (next - index), next)

let capture_first_line prog args =
  let stdout_path = Filename.temp_file "haven-platform-defaults-stdout" ".txt" in
  let stderr_path = Filename.temp_file "haven-platform-defaults-stderr" ".txt" in
  Fun.protect
    ~finally:(fun () ->
      if Sys.file_exists stdout_path then Sys.remove stdout_path;
      if Sys.file_exists stderr_path then Sys.remove stderr_path)
    (fun () ->
      let stdout_fd =
        Unix.openfile stdout_path [ Unix.O_WRONLY; Unix.O_CREAT; Unix.O_TRUNC ] 0o600
      in
      let stderr_fd =
        Unix.openfile stderr_path [ Unix.O_WRONLY; Unix.O_CREAT; Unix.O_TRUNC ] 0o600
      in
      Fun.protect
        ~finally:(fun () ->
          Unix.close stdout_fd;
          Unix.close stderr_fd)
        (fun () ->
          try
            let argv = Array.of_list (prog :: args) in
            let pid =
              Unix.create_process_env prog argv (Unix.environment ()) Unix.stdin stdout_fd
                stderr_fd
            in
            let _, status = Unix.waitpid [] pid in
            let read_first_line path =
              let ch = open_in path in
              Fun.protect
                ~finally:(fun () -> close_in_noerr ch)
                (fun () ->
                  try input_line ch |> String.trim with End_of_file -> "")
            in
            let stdout = read_first_line stdout_path in
            match status with
            | Unix.WEXITED 0 when not (String.equal stdout "") -> Some stdout
            | _ -> None
          with Unix.Unix_error _ -> None))

let resource_include_dir resource_dir = Filename.concat resource_dir "include"

let maybe_set_sysroot current path =
  match current with Some _ -> current | None -> existing_dir path

let apply_env_cflags ~search_dirs ~sysroot flags =
  let rec loop index search_dirs sysroot =
    match next_token flags index with
    | None -> (List.rev search_dirs, sysroot)
    | Some (token, index) -> (
        match token with
        | "-I" | "-isystem" | "-idirafter" -> (
            match next_token flags index with
            | Some (path, index) ->
                loop index
                  (match existing_dir path with Some dir -> dir :: search_dirs | None -> search_dirs)
                  sysroot
            | None -> (List.rev search_dirs, sysroot))
        | "-isysroot" -> (
            match next_token flags index with
            | Some (path, index) -> loop index search_dirs (maybe_set_sysroot sysroot path)
            | None -> (List.rev search_dirs, sysroot))
        | "-resource-dir" -> (
            match next_token flags index with
            | Some (path, index) ->
                loop index
                  (add_dir_if_exists search_dirs (resource_include_dir path))
                  sysroot
            | None -> (List.rev search_dirs, sysroot))
        | _ ->
            let search_dirs =
              if String.length token > 2 && String.sub token 0 2 = "-I" then
                add_dir_if_exists search_dirs (String.sub token 2 (String.length token - 2))
              else if String.length token > 14 && String.sub token 0 14 = "-resource-dir=" then
                add_dir_if_exists search_dirs
                  (resource_include_dir (String.sub token 14 (String.length token - 14)))
              else search_dirs
            in
            let sysroot =
              if String.length token > 9 && String.sub token 0 9 = "-isysroot" then
                maybe_set_sysroot sysroot (String.sub token 9 (String.length token - 9))
              else sysroot
            in
            loop index search_dirs sysroot)
  in
  loop 0 [] sysroot
  |> fun (env_search_dirs, sysroot) -> (search_dirs @ env_search_dirs, sysroot)

let resolve ?(search_dirs = []) ?sysroot () =
  let search_dirs = List.filter_map existing_dir search_dirs in
  let search_dirs, sysroot =
    match getenv_nonempty "NIX_CFLAGS_COMPILE" with
    | Some flags -> apply_env_cflags ~search_dirs ~sysroot flags
    | None -> (search_dirs, sysroot)
  in
  let sysroot =
    match sysroot with
    | Some _ -> sysroot
    | None -> (
        match getenv_nonempty "SDKROOT" with
        | Some path -> maybe_set_sysroot sysroot path
        | None -> sysroot)
  in
  let sysroot =
    match sysroot with
    | Some _ -> sysroot
    | None -> (
        match capture_first_line "xcrun" [ "--show-sdk-path" ] with
        | Some path -> maybe_set_sysroot sysroot path
        | None -> None)
  in
  let search_dirs =
    match sysroot with
    | Some root -> append_dir_if_exists search_dirs (join_path root "usr/include")
    | None -> append_dir_if_exists search_dirs "/usr/include"
  in
  let search_dirs =
    match capture_first_line "clang" [ "-print-resource-dir" ] with
    | Some path -> append_dir_if_exists search_dirs (resource_include_dir path)
    | None -> search_dirs
  in
  { search_dirs = dedupe search_dirs; sysroot }
