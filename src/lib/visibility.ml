type t = File | Module | External

let to_string = function
  | File -> "file"
  | Module -> "module"
  | External -> "external"

let is_external = function External -> true | File | Module -> false
