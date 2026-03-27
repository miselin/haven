open Format
open Haven_token.Token

module Surface = Surface_ast
module Core = Core_ast

let pp_sep fmt () = fprintf fmt ",@ "

let pp_surface_identifier fmt (id : Surface.identifier) = fprintf fmt "%s" id.value
let pp_core_identifier fmt (id : Core.identifier) = fprintf fmt "%s" id.value

let pp_surface_unary_op fmt = function
  | Surface.Not -> fprintf fmt "!"
  | Negate -> fprintf fmt "-"
  | Complement -> fprintf fmt "~"

let pp_core_unary_op fmt = function
  | Core.Not -> fprintf fmt "!"
  | Negate -> fprintf fmt "-"
  | Complement -> fprintf fmt "~"

let pp_surface_binary_op fmt = function
  | Surface.Add -> fprintf fmt "+"
  | Subtract -> fprintf fmt "-"
  | Multiply -> fprintf fmt "*"
  | Divide -> fprintf fmt "/"
  | Modulo -> fprintf fmt "%%"
  | LeftShift -> fprintf fmt "<<"
  | RightShift -> fprintf fmt ">>"
  | IsEqual -> fprintf fmt "=="
  | NotEqual -> fprintf fmt "!="
  | LessThan -> fprintf fmt "<"
  | LessThanOrEqual -> fprintf fmt "<="
  | GreaterThan -> fprintf fmt ">"
  | GreaterThanOrEqual -> fprintf fmt ">="
  | BitwiseAnd -> fprintf fmt "&"
  | BitwiseXor -> fprintf fmt "^"
  | BitwiseOr -> fprintf fmt "|"
  | LogicAnd -> fprintf fmt "&&"
  | LogicOr -> fprintf fmt "||"
  | Assign -> fprintf fmt "="
  | Mutate -> fprintf fmt ":="

let pp_core_binary_op fmt = function
  | Core.Add -> fprintf fmt "+"
  | Subtract -> fprintf fmt "-"
  | Multiply -> fprintf fmt "*"
  | Divide -> fprintf fmt "/"
  | Modulo -> fprintf fmt "%%"
  | LeftShift -> fprintf fmt "<<"
  | RightShift -> fprintf fmt ">>"
  | IsEqual -> fprintf fmt "=="
  | NotEqual -> fprintf fmt "!="
  | LessThan -> fprintf fmt "<"
  | LessThanOrEqual -> fprintf fmt "<="
  | GreaterThan -> fprintf fmt ">"
  | GreaterThanOrEqual -> fprintf fmt ">="
  | BitwiseAnd -> fprintf fmt "&"
  | BitwiseXor -> fprintf fmt "^"
  | BitwiseOr -> fprintf fmt "|"
  | LogicAnd -> fprintf fmt "&&"
  | LogicOr -> fprintf fmt "||"

let rec pp_surface_type fmt (ty : Surface.haven_type) =
  match ty.value with
  | Surface.NumericType n -> fprintf fmt "%s" (numeric_type_to_string n)
  | VecType v -> fprintf fmt "%s" (vec_type_to_string v)
  | MatrixType m -> fprintf fmt "%s" (mat_type_to_string m)
  | FloatType -> fprintf fmt "float"
  | VoidType -> fprintf fmt "void"
  | StringType -> fprintf fmt "str"
  | CustomType c -> pp_surface_identifier fmt c.name
  | CellType inner -> fprintf fmt "Cell<%a>" pp_surface_type inner
  | FunctionType fn ->
      fprintf fmt "%sFunction<(%a) -> %a>"
        (if fn.value.vararg then "VA" else "")
        (pp_print_list ~pp_sep pp_surface_type) fn.value.param_types pp_surface_type
        fn.value.return_type
  | PointerType inner -> fprintf fmt "Pointer<%a>" pp_surface_type inner
  | BoxType inner -> fprintf fmt "Box<%a>" pp_surface_type inner
  | ArrayType arr ->
      fprintf fmt "Array<%a, %a>" pp_surface_literal arr.value.count
        pp_surface_type arr.value.element
  | TemplatedType templ ->
      fprintf fmt "%a<%a>" pp_surface_identifier templ.value.outer
        (pp_print_list ~pp_sep pp_surface_type) templ.value.inner

and pp_surface_literal fmt (lit : Surface.literal) =
  match lit.value with
  | Surface.Integer i -> fprintf fmt "%d" i
  | Float f -> fprintf fmt "%f" f
  | String s -> fprintf fmt "%S" s
  | Char c -> fprintf fmt "%C" c
  | Matrix mat -> pp_surface_mat fmt mat
  | Vector vec -> pp_surface_vec fmt vec
  | Enum enum ->
      fprintf fmt "Enum(%a, %a, types=%a, wrapped=%a)" pp_surface_identifier
        enum.value.enum_name pp_surface_identifier enum.value.enum_variant
        (pp_print_list ~pp_sep pp_surface_type)
        enum.value.types
        (pp_print_list ~pp_sep pp_surface_expression)
        enum.value.wrapped

and pp_surface_vec fmt (vec : Surface.vec_literal) =
  fprintf fmt "Vec(%a)"
    (pp_print_list ~pp_sep pp_surface_expression)
    vec.value.elements

and pp_surface_mat fmt (mat : Surface.mat_literal) =
  fprintf fmt "Mat(%a)"
    (pp_print_list ~pp_sep pp_surface_vec)
    mat.value.rows

and pp_surface_expression fmt (expr : Surface.expression) =
  match expr.value with
  | Surface.Binary binary ->
      fprintf fmt "@[<hv 2>Binary(@,%a,@ %a,@ %a@,)@]" pp_surface_expression
        binary.value.left pp_surface_binary_op binary.value.op pp_surface_expression
        binary.value.right
  | Unary unary ->
      fprintf fmt "Unary(%a, %a)" pp_surface_unary_op unary.value.op
        pp_surface_expression unary.value.inner
  | Literal lit -> fprintf fmt "Literal(%a)" pp_surface_literal lit
  | Block block -> pp_surface_block fmt block
  | Identifier id -> fprintf fmt "Ident(%a)" pp_surface_identifier id
  | Initializer init ->
      fprintf fmt "Initializer(%a)"
        (pp_print_list ~pp_sep pp_surface_expression)
        init.value.exprs
  | As cast ->
      fprintf fmt "As<%a>(%a)" pp_surface_type cast.value.target_type
        pp_surface_expression cast.value.inner
  | SizeExpr inner -> fprintf fmt "SizeExpr(%a)" pp_surface_expression inner
  | SizeType ty -> fprintf fmt "SizeType(%a)" pp_surface_type ty
  | Nil -> fprintf fmt "Nil"
  | If ifx ->
      fprintf fmt "@[<hv 2>If(@,cond=%a,@ then=%a,@ else=%a@,)@]"
        pp_surface_expression ifx.value.cond pp_surface_block ifx.value.then_branch
        (pp_print_option pp_surface_block)
        ifx.value.else_branch
  | Match m ->
      fprintf fmt "@[<hv 2>Match(@,expr=%a,@ arms=%a@,)@]"
        pp_surface_expression m.value.expr
        (pp_print_list ~pp_sep pp_surface_match_arm)
        m.value.arms
  | BoxExpr inner -> fprintf fmt "BoxExpr(%a)" pp_surface_expression inner
  | BoxType ty -> fprintf fmt "BoxType(%a)" pp_surface_type ty
  | Unbox inner -> fprintf fmt "Unbox(%a)" pp_surface_expression inner
  | Ref inner -> fprintf fmt "Ref(%a)" pp_surface_expression inner
  | Load inner -> fprintf fmt "Load(%a)" pp_surface_expression inner
  | Call call ->
      fprintf fmt "Call(%a, %a)" pp_surface_expression call.value.target
        (pp_print_list ~pp_sep pp_surface_expression)
        call.value.params
  | Index idx ->
      fprintf fmt "Index(%a, %a)" pp_surface_expression idx.value.target
        pp_surface_expression idx.value.index
  | Field field ->
      fprintf fmt "Field(%a, arrow=%a, field=%a)" pp_surface_expression
        field.value.target pp_print_bool field.value.arrow pp_surface_identifier
        field.value.field

and pp_surface_match_arm fmt (arm : Surface.match_arm) =
  fprintf fmt "Arm(%a => %a)" pp_surface_match_pattern arm.value.pattern
    pp_surface_expression arm.value.expr

and pp_surface_match_pattern fmt (pat : Surface.match_pattern) =
  match pat.value with
  | Surface.PatternDefault -> fprintf fmt "_"
  | PatternLiteral lit -> pp_surface_literal fmt lit
  | PatternEnum enum ->
      fprintf fmt "Enum(%a, %a, bindings=%a)"
        (pp_print_option pp_surface_identifier)
        enum.value.enum_name pp_surface_identifier enum.value.enum_variant
        (pp_print_list ~pp_sep pp_surface_binding)
        enum.value.binding

and pp_surface_binding fmt (binding : Surface.pattern_binding) =
  match binding.value with
  | Surface.BindingIgnored -> fprintf fmt "_"
  | BindingNamed id -> pp_surface_identifier fmt id

and pp_surface_statement fmt (stmt : Surface.statement) =
  match stmt.value with
  | Surface.Expression expr ->
      fprintf fmt "Expr(%a)" pp_surface_expression expr
  | Let binding ->
      fprintf fmt "Let(mut=%a, name=%a, ty=%a, init=%a)" pp_print_bool
        binding.value.mut pp_surface_identifier binding.value.name
        (pp_print_option pp_surface_type)
        binding.value.ty pp_surface_expression binding.value.init_expr
  | Return expr ->
      fprintf fmt "Return(%a)" (pp_print_option pp_surface_expression) expr
  | Defer expr -> fprintf fmt "Defer(%a)" pp_surface_expression expr
  | Iter iter ->
      fprintf fmt
        "@[<hv 2>Iter(@,var=%a,@ start=%a,@ end=%a,@ step=%a,@ body=%a@,)@]"
        pp_surface_identifier iter.value.var pp_surface_expression
        iter.value.range.value.range_start pp_surface_expression
        iter.value.range.value.range_end
        (pp_print_option pp_surface_expression)
        iter.value.range.value.range_incr pp_surface_block iter.value.body
  | While while_stmt ->
      fprintf fmt "While(cond=%a, body=%a)" pp_surface_expression
        while_stmt.value.cond pp_surface_block while_stmt.value.body
  | Break -> fprintf fmt "Break"
  | Continue -> fprintf fmt "Continue"

and pp_surface_block fmt (block : Surface.block) =
  fprintf fmt "@[<hv 2>Block(@,statements=%a,@ result=%a@,)@]"
    (pp_print_list ~pp_sep pp_surface_statement)
    block.value.statements
    (pp_print_option pp_surface_expression)
    block.value.result

let pp_surface_param fmt (param : Surface.param) =
  fprintf fmt "Param(%a, %a)" pp_surface_type param.value.ty pp_surface_identifier
    param.value.name

let rec pp_surface_function fmt (fn : Surface.function_decl) =
  fprintf fmt
    "@[<hv 2>Function(@,pub=%a,@ impure=%a,@ name=%a,@ params=%a,@ return=%a,@ intrinsic=%a,@ body=%a@,)@]"
    pp_print_bool fn.value.public pp_print_bool fn.value.impure
    pp_surface_identifier fn.value.name
    (pp_print_list ~pp_sep pp_surface_param)
    fn.value.params.value.params
    (pp_print_option pp_surface_type)
    fn.value.return_type
    (pp_print_option pp_surface_intrinsic)
    fn.value.intrinsic
    (pp_print_option pp_surface_block)
    fn.value.definition

and pp_surface_intrinsic fmt (intr : Surface.intrinsic) =
  fprintf fmt "Intrinsic(%s, types=%a)" intr.value.name.value
    (pp_print_list ~pp_sep pp_surface_type)
    intr.value.types

let pp_surface_var_decl fmt (decl : Surface.var_decl) =
  fprintf fmt "Var(%a, pub=%a, mutable=%a, ty=%a, init=%a)"
    pp_surface_identifier decl.value.name pp_print_bool decl.value.public
    pp_print_bool decl.value.is_mutable pp_surface_type decl.value.ty
    (pp_print_option pp_surface_expression)
    decl.value.init_expr

let pp_surface_struct_field fmt (field : Surface.struct_field) =
  fprintf fmt "Field(%a, %a)" pp_surface_type field.value.ty pp_surface_identifier
    field.value.name

let pp_surface_enum_variant fmt (variant : Surface.enum_variant) =
  fprintf fmt "Variant(%a, inner=%a)" pp_surface_identifier variant.value.name
    (pp_print_option pp_surface_type) variant.value.inner_ty

let pp_surface_type_decl_data fmt = function
  | Surface.TypeDeclAlias ty -> fprintf fmt "Alias(%a)" pp_surface_type ty
  | TypeDeclStruct decl ->
      fprintf fmt "Struct(%a)"
        (pp_print_list ~pp_sep pp_surface_struct_field)
        decl.value.fields
  | TypeDeclEnum decl ->
      fprintf fmt "Enum(%a)"
        (pp_print_list ~pp_sep pp_surface_enum_variant)
        decl.value.variants
  | TypeDeclForward -> fprintf fmt "Forward"

let pp_surface_decl fmt (decl : Surface.top_decl) =
  match decl.value with
  | Surface.FDecl fn -> fprintf fmt "FDecl(%a)" pp_surface_function fn
  | TDecl ty ->
      fprintf fmt "TypeDecl(%a, %a)" pp_surface_identifier ty.value.name
        pp_surface_type_decl_data ty.value.data
  | VDecl v -> pp_surface_var_decl fmt v
  | Import i -> fprintf fmt "Import(%s)" i.value
  | CImport i -> fprintf fmt "CImport(%s)" i.value
  | Foreign f ->
      fprintf fmt "Foreign(%s, decls=%a)" f.value.lib.value
        (pp_print_list ~pp_sep pp_surface_function)
        f.value.decls

let pp_surface_program fmt (parsed : Surface.parsed_program) =
  pp_set_margin fmt 100;
  pp_set_max_indent fmt 80;
  fprintf fmt "@[<v 2>SurfaceProgram(@,%a@,)@]"
    (pp_print_list ~pp_sep pp_surface_decl)
    parsed.program.value.decls;
  pp_print_newline fmt ()

let surface_program_to_string parsed = asprintf "%a" pp_surface_program parsed

let rec pp_core_type fmt (ty : Core.haven_type) =
  match ty.value with
  | Core.NumericType n -> fprintf fmt "%s" (numeric_type_to_string n)
  | VecType v -> fprintf fmt "%s" (vec_type_to_string v)
  | MatrixType m -> fprintf fmt "%s" (mat_type_to_string m)
  | FloatType -> fprintf fmt "float"
  | VoidType -> fprintf fmt "void"
  | StringType -> fprintf fmt "str"
  | CustomType c -> pp_core_identifier fmt c.name
  | CellType inner -> fprintf fmt "Cell<%a>" pp_core_type inner
  | FunctionType fn ->
      fprintf fmt "%sFunction<(%a) -> %a>"
        (if fn.value.vararg then "VA" else "")
        (pp_print_list ~pp_sep pp_core_type) fn.value.param_types pp_core_type
        fn.value.return_type
  | PointerType inner -> fprintf fmt "Pointer<%a>" pp_core_type inner
  | BoxType inner -> fprintf fmt "Box<%a>" pp_core_type inner
  | ArrayType arr ->
      fprintf fmt "Array<%a, %a>" pp_core_literal arr.value.count pp_core_type
        arr.value.element
  | TemplatedType templ ->
      fprintf fmt "%a<%a>" pp_core_identifier templ.value.outer
        (pp_print_list ~pp_sep pp_core_type) templ.value.inner

and pp_core_literal fmt (lit : Core.literal) =
  match lit.value with
  | Core.Integer i -> fprintf fmt "%d" i
  | Bool b -> fprintf fmt "%b" b
  | Float f -> fprintf fmt "%f" f
  | String s -> fprintf fmt "%S" s
  | Char c -> fprintf fmt "%C" c
  | Matrix mat -> pp_core_mat fmt mat
  | Vector vec -> pp_core_vec fmt vec
  | Enum enum ->
      fprintf fmt "Enum(%a, %a, types=%a, wrapped=%a)" pp_core_identifier
        enum.value.enum_name pp_core_identifier enum.value.enum_variant
        (pp_print_list ~pp_sep pp_core_type)
        enum.value.types
        (pp_print_list ~pp_sep pp_core_expression)
        enum.value.wrapped

and pp_core_vec fmt (vec : Core.vec_literal) =
  fprintf fmt "Vec(%a)"
    (pp_print_list ~pp_sep pp_core_expression)
    vec.value.elements

and pp_core_mat fmt (mat : Core.mat_literal) =
  fprintf fmt "Mat(%a)"
    (pp_print_list ~pp_sep pp_core_vec)
    mat.value.rows

and pp_core_expression fmt (expr : Core.expression) =
  match expr.value with
  | Core.Binary binary ->
      fprintf fmt "@[<hv 2>Binary(@,%a,@ %a,@ %a@,)@]" pp_core_expression
        binary.value.left pp_core_binary_op binary.value.op pp_core_expression
        binary.value.right
  | Unary unary ->
      fprintf fmt "Unary(%a, %a)" pp_core_unary_op unary.value.op
        pp_core_expression unary.value.inner
  | Literal lit -> fprintf fmt "Literal(%a)" pp_core_literal lit
  | Block block -> pp_core_block fmt block
  | Identifier id -> fprintf fmt "Ident(%a)" pp_core_identifier id
  | ToBool inner -> fprintf fmt "ToBool(%a)" pp_core_expression inner
  | Initializer init ->
      fprintf fmt "Initializer(%a)"
        (pp_print_list ~pp_sep pp_core_expression)
        init.value.exprs
  | As cast ->
      fprintf fmt "As<%a>(%a)" pp_core_type cast.value.target_type
        pp_core_expression cast.value.inner
  | SizeExpr inner -> fprintf fmt "SizeExpr(%a)" pp_core_expression inner
  | SizeType ty -> fprintf fmt "SizeType(%a)" pp_core_type ty
  | Nil -> fprintf fmt "Nil"
  | Match m ->
      fprintf fmt "@[<hv 2>Match(@,expr=%a,@ arms=%a@,)@]" pp_core_expression
        m.value.expr
        (pp_print_list ~pp_sep pp_core_match_arm)
        m.value.arms
  | BoxExpr inner -> fprintf fmt "BoxExpr(%a)" pp_core_expression inner
  | BoxType ty -> fprintf fmt "BoxType(%a)" pp_core_type ty
  | Unbox inner -> fprintf fmt "Unbox(%a)" pp_core_expression inner
  | Ref inner -> fprintf fmt "Ref(%a)" pp_core_expression inner
  | Load inner -> fprintf fmt "Load(%a)" pp_core_expression inner
  | Call call ->
      fprintf fmt "Call(%a, %a)" pp_core_expression call.value.target
        (pp_print_list ~pp_sep pp_core_expression)
        call.value.params
  | Index idx ->
      fprintf fmt "Index(%a, %a)" pp_core_expression idx.value.target
        pp_core_expression idx.value.index
  | Field field ->
      fprintf fmt "Field(%a, arrow=%a, field=%a)" pp_core_expression
        field.value.target pp_print_bool field.value.arrow pp_core_identifier
        field.value.field
  | Assign write ->
      fprintf fmt "Assign(%a, %a)" pp_core_expression write.value.target
        pp_core_expression write.value.value
  | Mutate write ->
      fprintf fmt "Mutate(%a, %a)" pp_core_expression write.value.target
        pp_core_expression write.value.value

and pp_core_match_arm fmt (arm : Core.match_arm) =
  fprintf fmt "Arm(%a => %a)" pp_core_match_pattern arm.value.pattern
    pp_core_expression arm.value.expr

and pp_core_match_pattern fmt (pat : Core.match_pattern) =
  match pat.value with
  | Core.PatternDefault -> fprintf fmt "_"
  | PatternLiteral lit -> pp_core_literal fmt lit
  | PatternEnum enum ->
      fprintf fmt "Enum(%a, %a, bindings=%a)"
        (pp_print_option pp_core_identifier)
        enum.value.enum_name pp_core_identifier enum.value.enum_variant
        (pp_print_list ~pp_sep pp_core_binding)
        enum.value.binding

and pp_core_binding fmt (binding : Core.pattern_binding) =
  match binding.value with
  | Core.BindingIgnored -> fprintf fmt "_"
  | BindingNamed id -> pp_core_identifier fmt id

and pp_core_statement fmt (stmt : Core.statement) =
  match stmt.value with
  | Core.Expression expr -> fprintf fmt "Expr(%a)" pp_core_expression expr
  | Let binding ->
      fprintf fmt "Let(mut=%a, name=%a, ty=%a, init=%a)" pp_print_bool
        binding.value.mut pp_core_identifier binding.value.name
        (pp_print_option pp_core_type)
        binding.value.ty pp_core_expression binding.value.init_expr
  | Return expr -> fprintf fmt "Return(%a)" (pp_print_option pp_core_expression) expr
  | Defer expr -> fprintf fmt "Defer(%a)" pp_core_expression expr
  | Loop loop ->
      fprintf fmt "Loop(init=%a, cond=%a, step=%a, hint=%a, body=%a)"
        (pp_print_list ~pp_sep pp_core_statement)
        loop.value.init pp_core_expression loop.value.cond
        (pp_print_list ~pp_sep pp_core_statement)
        loop.value.step
        (pp_print_option pp_core_iteration_hint)
        loop.value.iteration_hint pp_core_block loop.value.body
  | Break -> fprintf fmt "Break"
  | Continue -> fprintf fmt "Continue"

and pp_core_iteration_hint fmt (hint : Core.iteration_hint) =
  match hint.value with
  | Core.KnownTripCount n -> fprintf fmt "KnownTripCount(%d)" n

and pp_core_block fmt (block : Core.block) =
  fprintf fmt "@[<hv 2>Block(@,statements=%a,@ result=%a@,)@]"
    (pp_print_list ~pp_sep pp_core_statement)
    block.value.statements
    (pp_print_option pp_core_expression)
    block.value.result

let pp_core_param fmt (param : Core.param) =
  fprintf fmt "Param(%a, %a)" pp_core_type param.value.ty pp_core_identifier
    param.value.name

let rec pp_core_function fmt (fn : Core.function_decl) =
  fprintf fmt
    "@[<hv 2>Function(@,pub=%a,@ impure=%a,@ name=%a,@ params=%a,@ return=%a,@ intrinsic=%a,@ body=%a@,)@]"
    pp_print_bool fn.value.public pp_print_bool fn.value.impure
    pp_core_identifier fn.value.name
    (pp_print_list ~pp_sep pp_core_param)
    fn.value.params.value.params
    (pp_print_option pp_core_type)
    fn.value.return_type
    (pp_print_option pp_core_intrinsic)
    fn.value.intrinsic
    (pp_print_option pp_core_block)
    fn.value.definition

and pp_core_intrinsic fmt (intr : Core.intrinsic) =
  fprintf fmt "Intrinsic(%s, types=%a)" intr.value.name.value
    (pp_print_list ~pp_sep pp_core_type)
    intr.value.types

let pp_core_var_decl fmt (decl : Core.var_decl) =
  fprintf fmt "Var(%a, pub=%a, mutable=%a, ty=%a, init=%a)"
    pp_core_identifier decl.value.name pp_print_bool decl.value.public
    pp_print_bool decl.value.is_mutable pp_core_type decl.value.ty
    (pp_print_option pp_core_expression)
    decl.value.init_expr

let pp_core_struct_field fmt (field : Core.struct_field) =
  fprintf fmt "Field(%a, %a)" pp_core_type field.value.ty pp_core_identifier
    field.value.name

let pp_core_enum_variant fmt (variant : Core.enum_variant) =
  fprintf fmt "Variant(%a, inner=%a)" pp_core_identifier variant.value.name
    (pp_print_option pp_core_type) variant.value.inner_ty

let pp_core_type_decl_data fmt = function
  | Core.TypeDeclAlias ty -> fprintf fmt "Alias(%a)" pp_core_type ty
  | TypeDeclStruct decl ->
      fprintf fmt "Struct(%a)"
        (pp_print_list ~pp_sep pp_core_struct_field)
        decl.value.fields
  | TypeDeclEnum decl ->
      fprintf fmt "Enum(%a)"
        (pp_print_list ~pp_sep pp_core_enum_variant)
        decl.value.variants
  | TypeDeclForward -> fprintf fmt "Forward"

let pp_core_decl fmt (decl : Core.top_decl) =
  match decl.value with
  | Core.FDecl fn -> fprintf fmt "FDecl(%a)" pp_core_function fn
  | TDecl ty ->
      fprintf fmt "TypeDecl(%a, %a)" pp_core_identifier ty.value.name
        pp_core_type_decl_data ty.value.data
  | VDecl v -> pp_core_var_decl fmt v
  | Import i -> fprintf fmt "Import(%s)" i.value
  | CImport i -> fprintf fmt "CImport(%s)" i.value
  | Foreign f ->
      fprintf fmt "Foreign(%s, decls=%a)" f.value.lib.value
        (pp_print_list ~pp_sep pp_core_function)
        f.value.decls

let pp_core_program fmt (parsed : Core.parsed_program) =
  pp_set_margin fmt 100;
  pp_set_max_indent fmt 80;
  fprintf fmt "@[<v 2>CoreProgram(@,%a@,)@]"
    (pp_print_list ~pp_sep pp_core_decl)
    parsed.program.value.decls;
  pp_print_newline fmt ()

let core_program_to_string parsed = asprintf "%a" pp_core_program parsed
