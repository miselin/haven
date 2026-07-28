open Haven_core

module Cst = Haven_cst.Cst
module Surface = Surface_ast
module Core = Core_ast
module String_set = Set.Make (String)

type block_context = [ `Statement | `Value ]

type lowered_block_item =
  | LoweredStatement of Surface.statement
  | LoweredExpression of Surface.expression

type lowering_state = {
  mutable next_fresh : int;
  mutable known_types : String_set.t;
}

let fresh_state () = { next_fresh = 0; known_types = String_set.empty }

let fresh_name st prefix =
  st.next_fresh <- st.next_fresh + 1;
  Printf.sprintf "$%s.%d" prefix st.next_fresh

let mk_surface loc value : _ Surface.node = { value; loc }
let mk_surface_ident loc value : Surface.identifier = { value; loc }
let mk_surface_expr loc value : Surface.expression = { value; loc }
let mk_surface_stmt loc value : Surface.statement = { value; loc }
let mk_surface_block loc value : Surface.block = { value; loc }
let mk_surface_type loc value : Surface.haven_type = { value; loc }
let mk_surface_literal loc value : Surface.literal = { value; loc }
let mk_surface_pattern loc value : Surface.match_pattern = { value; loc }
let mk_surface_binding loc value : Surface.pattern_binding = { value; loc }
let mk_surface_arm loc value : Surface.match_arm = { value; loc }
let mk_surface_vec loc value : Surface.vec_literal = { value; loc }
let mk_surface_mat loc value : Surface.mat_literal = { value; loc }
let mk_surface_enum loc value : Surface.enum_literal = { value; loc }

let mk_core loc value : _ Core.node = { value; loc }
let mk_core_ident loc value : Core.identifier = { value; loc }
let mk_core_expr loc value : Core.expression = { value; loc }
let mk_core_stmt loc value : Core.statement = { value; loc }
let mk_core_block loc value : Core.block = { value; loc }
let mk_core_type loc value : Core.haven_type = { value; loc }
let mk_core_literal loc value : Core.literal = { value; loc }
let mk_core_pattern loc value : Core.match_pattern = { value; loc }
let mk_core_binding loc value : Core.pattern_binding = { value; loc }
let mk_core_arm loc value : Core.match_arm = { value; loc }
let mk_core_vec loc value : Core.vec_literal = { value; loc }
let mk_core_mat loc value : Core.mat_literal = { value; loc }
let mk_core_enum loc value : Core.enum_literal = { value; loc }
let mk_core_iteration_hint loc value : Core.iteration_hint = { value; loc }

type surface_extension_hooks = {
  construct : Surface.lifecycle_construct option;
  destruct : Surface.block option;
  loc : Loc.t;
}

let default_iter_type loc =
  mk_core_type loc
    (Core.NumericType { Haven_token.Token.signedness = Haven_token.Token.Signed; bits = 32 })

let string_of_loc (loc : Loc.t) =
  let pos = loc.start_pos in
  let col = pos.pos_cnum - pos.pos_bol + 1 in
  if pos.pos_fname = "" then Printf.sprintf "%d:%d" pos.pos_lnum col
  else Printf.sprintf "%s:%d:%d" pos.pos_fname pos.pos_lnum col

let rec surface_type_has_specialization_hole (ty : Surface.haven_type) =
  match ty.value with
  | Surface.VecHoleType | Surface.MatrixHoleType -> true
  | Surface.CellType inner
  | Surface.PointerType inner
  | Surface.BoxType inner ->
      surface_type_has_specialization_hole inner
  | Surface.ArrayType arr -> surface_type_has_specialization_hole arr.value.element
  | Surface.FunctionType fn ->
      surface_type_has_specialization_hole fn.value.return_type
      || List.exists surface_type_has_specialization_hole fn.value.param_types
  | Surface.TemplatedType templ ->
      List.exists surface_type_has_specialization_hole templ.value.inner
  | Surface.NumericType _
  | Surface.VecType _
  | Surface.MatrixType _
  | Surface.FloatType
  | Surface.VoidType
  | Surface.StringType
  | Surface.CustomType _ ->
      false

let function_has_specialization_param (fn : Surface.function_decl) =
  List.exists
    (fun (param : Surface.param) ->
      surface_type_has_specialization_hole param.value.ty)
    fn.value.params.value.params

let validate_surface_function_decl (fn : Surface.function_decl) =
  if fn.value.return_type = None && not (function_has_specialization_param fn) then
    failwith
      (Printf.sprintf
         "function %s omits its return type, but only specialization functions may infer returns (%s)"
         fn.value.name.value (string_of_loc fn.loc))

let rec cst_program_to_surface (program : Cst.program) : Surface.program =
  let decls = List.map cst_top_decl_to_surface program.value.decls in
  mk_surface program.loc { Surface.decls }

and cst_top_decl_to_surface (decl : Cst.top_decl) : Surface.top_decl =
  let value =
    match decl.value with
    | Cst.FDecl fn -> Surface.FDecl (cst_function_decl_to_surface fn)
    | Cst.TDecl ty -> Surface.TDecl (cst_type_decl_to_surface ty)
    | Cst.VDecl v -> Surface.VDecl (cst_var_decl_to_surface v)
    | Cst.Import i -> Surface.Import { value = i.value; loc = i.loc }
    | Cst.CImport i -> Surface.CImport { value = i.value; loc = i.loc }
    | Cst.Foreign f -> Surface.Foreign (cst_foreign_to_surface f)
    | Cst.Extend e -> Surface.Extend (cst_type_extend_to_surface e)
  in
  mk_surface decl.loc value

and cst_function_decl_to_surface (fn : Cst.function_decl) : Surface.function_decl =
  let value =
    {
      Surface.public = fn.value.public;
      impure = fn.value.impure;
      name = cst_identifier_to_surface fn.value.name;
      definition = Option.map cst_block_to_surface fn.value.definition;
      intrinsic = Option.map cst_intrinsic_to_surface fn.value.intrinsic;
      params = cst_param_list_to_surface fn.value.params;
      return_type = Option.map cst_type_to_surface fn.value.return_type;
      vararg = fn.value.vararg;
    }
  in
  let decl = mk_surface fn.loc value in
  validate_surface_function_decl decl;
  decl

and cst_intrinsic_to_surface (intr : Cst.intrinsic) : Surface.intrinsic =
  let value =
    {
      Surface.name = { value = intr.value.name.value; loc = intr.value.name.loc };
      types = List.map cst_type_to_surface intr.value.types;
    }
  in
  mk_surface intr.loc value

and cst_param_list_to_surface (params : Cst.param_list) : Surface.param_list =
  let value =
    {
      Surface.params = List.map cst_param_to_surface params.value.params;
      vararg = params.value.vararg;
    }
  in
  mk_surface params.loc value

and cst_param_to_surface (param : Cst.param) : Surface.param =
  let value : Surface.param_desc =
    {
      Surface.name = cst_identifier_to_surface param.value.name;
      ty = cst_type_to_surface param.value.ty;
    }
  in
  mk_surface param.loc value

and cst_var_decl_to_surface (decl : Cst.var_decl) : Surface.var_decl =
  let value =
    {
      Surface.name = cst_identifier_to_surface decl.value.name;
      public = decl.value.public;
      is_mutable = decl.value.is_mutable;
      ty = cst_type_to_surface decl.value.ty;
      init_expr = Option.map cst_expr_to_surface decl.value.init_expr;
    }
  in
  mk_surface decl.loc value

and cst_type_extend_to_surface (ext : Cst.type_extend) : Surface.type_extend =
  let hooks =
    List.fold_left
      (fun hooks (item : Cst.extend_item) ->
        match item.value with
        | Cst.ExtendConstruct construct ->
            if Option.is_some hooks.construct then
              failwith
                (Printf.sprintf "duplicate construct block in extend %s"
                   ext.value.target.value);
            {
              hooks with
              construct =
                Some
                  (mk_surface construct.loc
                     {
                       Surface.params =
                         List.map cst_param_to_surface construct.value.params;
                       body = cst_block_to_surface construct.value.body;
                     });
            }
        | Cst.ExtendDestruct block ->
            if Option.is_some hooks.destruct then
              failwith
                (Printf.sprintf "duplicate destruct block in extend %s"
                   ext.value.target.value);
            { hooks with destruct = Some (cst_block_to_surface block) })
      { construct = None; destruct = None; loc = ext.loc }
      ext.value.items
  in
  mk_surface ext.loc
    {
      Surface.target = cst_identifier_to_surface ext.value.target;
      construct = hooks.construct;
      destruct = hooks.destruct;
    }

and cst_type_decl_to_surface (decl : Cst.type_decl) : Surface.type_decl =
  let value =
    {
      Surface.name = cst_identifier_to_surface decl.value.name;
      data = cst_type_decl_data_to_surface decl.value.data;
      construct = None;
      destruct = None;
    }
  in
  mk_surface decl.loc value

and cst_type_decl_data_to_surface = function
  | Cst.TypeDeclAlias ty -> Surface.TypeDeclAlias (cst_type_to_surface ty)
  | Cst.TypeDeclStruct s -> Surface.TypeDeclStruct (cst_struct_decl_to_surface s)
  | Cst.TypeDeclEnum e -> Surface.TypeDeclEnum (cst_enum_decl_to_surface e)
  | Cst.TypeDeclForward -> Surface.TypeDeclForward

and cst_struct_decl_to_surface (decl : Cst.struct_decl) : Surface.struct_decl =
  let value =
    {
      Surface.fields = List.map cst_struct_field_to_surface decl.value.fields;
      lifecycle = None;
    }
  in
  mk_surface decl.loc value

and cst_struct_field_to_surface (field : Cst.struct_field) : Surface.struct_field =
  let value : Surface.struct_field_desc =
    {
      Surface.name = cst_identifier_to_surface field.value.name;
      ty = cst_type_to_surface field.value.ty;
    }
  in
  ({ value; loc = field.loc } : Surface.struct_field)

and cst_enum_decl_to_surface (decl : Cst.enum_decl) : Surface.enum_decl =
  let value =
    {
      Surface.generics = List.map cst_identifier_to_surface decl.value.generics;
      variants = List.map cst_enum_variant_to_surface decl.value.variants;
    }
  in
  mk_surface decl.loc value

and cst_enum_variant_to_surface (variant : Cst.enum_variant) : Surface.enum_variant =
  let value =
    {
      Surface.name = cst_identifier_to_surface variant.value.name;
      inner_tys = List.map cst_type_to_surface variant.value.inner_tys;
    }
  in
  mk_surface variant.loc value

and cst_foreign_to_surface (foreign : Cst.foreign) : Surface.foreign =
  let value =
    {
      Surface.lib = { value = foreign.value.lib.value; loc = foreign.value.lib.loc };
      decls = List.map cst_function_decl_to_surface foreign.value.decls;
    }
  in
  mk_surface foreign.loc value

and cst_block_to_surface (block : Cst.block) : Surface.block =
  let lowered_items =
    List.filter_map cst_block_item_to_surface block.value.items
  in
  let statements, result =
    match List.rev lowered_items with
    | LoweredExpression expr :: rev_prefix ->
        ( List.rev_map lowered_item_to_statement rev_prefix,
          Some expr )
    | rev_items -> (List.rev_map lowered_item_to_statement rev_items, None)
  in
  mk_surface_block block.loc { statements; result }

and cst_block_item_to_surface (item : Cst.block_item) : lowered_block_item option =
  match item.value with
  | Cst.BlockStatement stmt ->
      Option.map
        (fun stmt -> LoweredStatement stmt)
        (cst_statement_to_surface stmt)
  | Cst.BlockExpression expr -> Some (LoweredExpression (cst_expr_to_surface expr))

and lowered_item_to_statement = function
  | LoweredStatement stmt -> stmt
  | LoweredExpression expr ->
      mk_surface_stmt expr.loc (Surface.Expression expr)

and cst_statement_to_surface (stmt : Cst.statement) : Surface.statement option =
  let value =
    match stmt.value with
    | Cst.Expression expr -> Some (Surface.Expression (cst_expr_to_surface expr))
    | Cst.Let binding ->
        Some
          (Surface.Let
             (mk_surface binding.loc
                {
                  Surface.mut = binding.value.mut;
                  ty = Option.map cst_type_to_surface binding.value.ty;
                  name = cst_identifier_to_surface binding.value.name;
                  init_expr = cst_expr_to_surface binding.value.init_expr;
                }))
    | Cst.CompileAssert compile_assert ->
        Some
          (Surface.CompileAssert
             (mk_surface compile_assert.loc
                {
                  Surface.cond = cst_expr_to_surface compile_assert.value.cond;
                  message =
                    {
                      value = compile_assert.value.message.value;
                      loc = compile_assert.value.message.loc;
                    };
                }))
    | Cst.Return expr -> Some (Surface.Return (Option.map cst_expr_to_surface expr))
    | Cst.Defer expr -> Some (Surface.Defer (cst_expr_to_surface expr))
    | Cst.Iter iter ->
        Some
          (Surface.Iter
             (mk_surface iter.loc
                {
                  Surface.range = cst_iter_range_to_surface iter.value.range;
                  var = cst_identifier_to_surface iter.value.var;
                  body = cst_block_to_surface iter.value.body;
                }))
    | Cst.While while_stmt ->
        Some
          (Surface.While
             (mk_surface while_stmt.loc
                {
                  Surface.cond = cst_expr_to_surface while_stmt.value.cond;
                  body = cst_block_to_surface while_stmt.value.body;
                }))
    | Cst.Break -> Some Surface.Break
    | Cst.Continue -> Some Surface.Continue
    | Cst.Empty -> None
  in
  Option.map (mk_surface_stmt stmt.loc) value

and cst_iter_range_to_surface (range : Cst.iter_range) : Surface.iter_range =
  let value =
    {
      Surface.range_start = cst_expr_to_surface range.value.range_start;
      range_end = cst_expr_to_surface range.value.range_end;
      range_incr = Option.map cst_expr_to_surface range.value.range_incr;
    }
  in
  mk_surface range.loc value

and cst_expr_to_surface (expr : Cst.expression) : Surface.expression =
  match expr.value with
  | Cst.ParenthesizedExpression inner -> cst_expr_to_surface inner
  | _ ->
      let value =
        match expr.value with
        | Cst.Binary b -> Surface.Binary (cst_binary_to_surface b)
        | Cst.Unary u -> Surface.Unary (cst_unary_to_surface u)
        | Cst.Literal lit -> Surface.Literal (cst_literal_to_surface lit)
        | Cst.Block block -> Surface.Block (cst_block_to_surface block)
        | Cst.ParenthesizedExpression _ -> assert false
        | Cst.Identifier id -> Surface.Identifier (cst_identifier_to_surface id)
        | Cst.Initializer init -> Surface.Initializer (cst_init_to_surface init)
        | Cst.As cast -> Surface.As (cst_as_expr_to_surface cast)
        | Cst.SizeExpr inner -> Surface.SizeExpr (cst_expr_to_surface inner)
        | Cst.SizeType ty -> Surface.SizeType (cst_type_to_surface ty)
        | Cst.Nil -> Surface.Nil
        | Cst.Zero -> Surface.Zero
        | Cst.If ifx -> Surface.If (cst_if_expr_to_surface ifx)
        | Cst.Match m -> Surface.Match (cst_match_expr_to_surface m)
        | Cst.BoxExpr inner -> Surface.BoxExpr (cst_expr_to_surface inner)
        | Cst.BoxType ty -> Surface.BoxType (cst_type_to_surface ty)
        | Cst.Unbox inner -> Surface.Unbox (cst_expr_to_surface inner)
        | Cst.Ref inner -> Surface.Ref (cst_expr_to_surface inner)
        | Cst.Load inner -> Surface.Load (cst_expr_to_surface inner)
        | Cst.Call call -> Surface.Call (cst_call_to_surface call)
        | Cst.Index idx -> Surface.Index (cst_index_to_surface idx)
        | Cst.Field field -> Surface.Field (cst_field_to_surface field)
      in
      mk_surface_expr expr.loc value

and cst_binary_to_surface (binary : Cst.binary) : Surface.binary =
  let value =
    {
      Surface.left = cst_expr_to_surface binary.value.left;
      right = cst_expr_to_surface binary.value.right;
      op = cst_binary_op_to_surface binary.value.op;
    }
  in
  mk_surface binary.loc value

and cst_binary_op_to_surface = function
  | Cst.Add -> Surface.Add
  | Cst.Subtract -> Surface.Subtract
  | Cst.Multiply -> Surface.Multiply
  | Cst.Divide -> Surface.Divide
  | Cst.Modulo -> Surface.Modulo
  | Cst.LeftShift -> Surface.LeftShift
  | Cst.RightShift -> Surface.RightShift
  | Cst.IsEqual -> Surface.IsEqual
  | Cst.NotEqual -> Surface.NotEqual
  | Cst.LessThan -> Surface.LessThan
  | Cst.LessThanOrEqual -> Surface.LessThanOrEqual
  | Cst.GreaterThan -> Surface.GreaterThan
  | Cst.GreaterThanOrEqual -> Surface.GreaterThanOrEqual
  | Cst.BitwiseAnd -> Surface.BitwiseAnd
  | Cst.BitwiseXor -> Surface.BitwiseXor
  | Cst.BitwiseOr -> Surface.BitwiseOr
  | Cst.LogicAnd -> Surface.LogicAnd
  | Cst.LogicOr -> Surface.LogicOr
  | Cst.Assign -> Surface.Assign
  | Cst.Mutate -> Surface.Mutate

and cst_unary_to_surface (unary : Cst.unary) : Surface.unary =
  let value =
    {
      Surface.inner = cst_expr_to_surface unary.value.inner;
      op = cst_unary_op_to_surface unary.value.op;
    }
  in
  mk_surface unary.loc value

and cst_unary_op_to_surface = function
  | Cst.Not -> Surface.Not
  | Cst.Negate -> Surface.Negate
  | Cst.Complement -> Surface.Complement

and cst_literal_to_surface (lit : Cst.literal) : Surface.literal =
  let value =
    match lit.value with
    | Cst.HexInt i | OctInt i | BinInt i | DecInt i -> Surface.Integer i
    | Cst.Float f -> Surface.Float f
    | Cst.String s -> Surface.String s
    | Cst.Char c -> Surface.Char c
    | Cst.Matrix mat -> Surface.Matrix (cst_mat_to_surface mat)
    | Cst.Vector vec -> Surface.Vector (cst_vec_to_surface vec)
    | Cst.Enum enum -> Surface.Enum (cst_enum_literal_to_surface enum)
  in
  mk_surface_literal lit.loc value

and cst_vec_to_surface (vec : Cst.vec_literal) : Surface.vec_literal =
  mk_surface_vec vec.loc
    { Surface.elements = List.map cst_expr_to_surface vec.value.elements }

and cst_mat_to_surface (mat : Cst.mat_literal) : Surface.mat_literal =
  mk_surface_mat mat.loc
    { Surface.rows = List.map cst_expr_to_surface mat.value.rows }

and cst_enum_literal_to_surface (enum : Cst.enum_literal) : Surface.enum_literal =
  mk_surface_enum enum.loc
    {
      Surface.enum_name = cst_identifier_to_surface enum.value.enum_name;
      enum_variant = cst_identifier_to_surface enum.value.enum_variant;
      types = List.map cst_type_to_surface enum.value.types;
      wrapped = [];
    }

and cst_type_to_surface (ty : Cst.haven_type) : Surface.haven_type =
  let value =
    match ty.value with
    | Cst.NumericType n -> Surface.NumericType n
    | Cst.VecType v -> Surface.VecType v
    | Cst.MatrixType m -> Surface.MatrixType m
    | Cst.VecHoleType -> Surface.VecHoleType
    | Cst.MatrixHoleType -> Surface.MatrixHoleType
    | Cst.FloatType -> Surface.FloatType
    | Cst.VoidType -> Surface.VoidType
    | Cst.StringType -> Surface.StringType
    | Cst.CustomType c ->
        Surface.CustomType { Surface.name = cst_identifier_to_surface c.name }
    | Cst.CellType inner -> Surface.CellType (cst_type_to_surface inner)
    | Cst.FunctionType fn ->
        Surface.FunctionType
          (mk_surface fn.loc
             {
               Surface.param_types = List.map cst_type_to_surface fn.value.param_types;
               return_type = cst_type_to_surface fn.value.return_type;
               vararg = fn.value.vararg;
             })
    | Cst.PointerType inner -> Surface.PointerType (cst_type_to_surface inner)
    | Cst.BoxType inner -> Surface.BoxType (cst_type_to_surface inner)
    | Cst.ArrayType arr ->
        Surface.ArrayType
          (mk_surface arr.loc
             {
               Surface.element = cst_type_to_surface arr.value.element;
               count = cst_literal_to_surface arr.value.count;
             })
    | Cst.TemplatedType templ ->
        Surface.TemplatedType
          (mk_surface templ.loc
             {
               Surface.outer = cst_identifier_to_surface templ.value.outer;
               inner = List.map cst_type_to_surface templ.value.inner;
             })
  in
  mk_surface_type ty.loc value

and cst_identifier_to_surface (id : Cst.identifier) : Surface.identifier =
  mk_surface_ident id.loc id.value

and cst_init_to_surface (init : Cst.init_list) : Surface.init_list =
  mk_surface init.loc { Surface.exprs = List.map cst_expr_to_surface init.value.exprs }

and cst_as_expr_to_surface (cast : Cst.as_expr) : Surface.as_expr =
  mk_surface cast.loc
    {
      Surface.target_type = cst_type_to_surface cast.value.target_type;
      inner = cst_expr_to_surface cast.value.inner;
    }

and cst_if_expr_to_surface (ifx : Cst.if_expr) : Surface.if_expr =
  let else_branch =
    match ifx.value.else_block with
    | None -> None
    | Some (Cst.Else block) -> Some (cst_block_to_surface block)
    | Some (Cst.ElseIf nested_if) ->
        let nested_expr = mk_surface_expr nested_if.loc (Surface.If (cst_if_expr_to_surface nested_if)) in
        Some (mk_surface_block nested_if.loc { statements = []; result = Some nested_expr })
  in
  mk_surface ifx.loc
    {
      Surface.cond = cst_expr_to_surface ifx.value.cond;
      then_branch = cst_block_to_surface ifx.value.then_block;
      else_branch;
    }

and cst_match_expr_to_surface (m : Cst.match_expr) : Surface.match_expr =
  mk_surface m.loc
    {
      Surface.expr = cst_expr_to_surface m.value.expr;
      arms = List.map cst_match_arm_to_surface m.value.arms;
    }

and cst_match_arm_to_surface (arm : Cst.match_arm) : Surface.match_arm =
  mk_surface_arm arm.loc
    {
      Surface.pattern = cst_match_pattern_to_surface arm.value.pattern;
      expr = cst_expr_to_surface arm.value.expr;
    }

and cst_match_pattern_to_surface (pat : Cst.match_pattern) : Surface.match_pattern =
  let value =
    match pat.value with
    | Cst.PatternDefault -> Surface.PatternDefault
    | Cst.PatternLiteral lit -> Surface.PatternLiteral (cst_literal_to_surface lit)
    | Cst.PatternEnum enum -> Surface.PatternEnum (cst_pattern_enum_to_surface enum)
  in
  mk_surface_pattern pat.loc value

and cst_pattern_enum_to_surface (enum : Cst.pattern_enum) : Surface.pattern_enum =
  mk_surface enum.loc
    {
      Surface.enum_name = Option.map cst_identifier_to_surface enum.value.enum_name;
      enum_variant = cst_identifier_to_surface enum.value.enum_variant;
      binding = List.map cst_pattern_binding_to_surface enum.value.binding;
    }

and cst_pattern_binding_to_surface (binding : Cst.pattern_binding) :
    Surface.pattern_binding =
  let value =
    match binding.value with
    | Cst.BindingIgnored -> Surface.BindingIgnored
    | Cst.BindingNamed id -> Surface.BindingNamed (cst_identifier_to_surface id)
  in
  mk_surface_binding binding.loc value

and cst_call_to_surface (call : Cst.call) : Surface.call =
  mk_surface call.loc
    {
      Surface.target = cst_expr_to_surface call.value.target;
      params = List.map cst_expr_to_surface call.value.params;
    }

and cst_index_to_surface (index : Cst.index) : Surface.index =
  mk_surface index.loc
    {
      Surface.target = cst_expr_to_surface index.value.target;
      index = cst_expr_to_surface index.value.index;
    }

and cst_field_to_surface (field : Cst.field) : Surface.field =
  mk_surface field.loc
    {
      Surface.target = cst_expr_to_surface field.value.target;
      arrow = field.value.arrow;
      field = cst_identifier_to_surface field.value.field;
    }

let surface_of_cst (parsed : Cst.parsed_program) : Surface.parsed_program =
  { Surface.program = cst_program_to_surface parsed.program }

let core_binary_op_of_surface = function
  | Surface.Add -> Core.Add
  | Surface.Subtract -> Core.Subtract
  | Surface.Multiply -> Core.Multiply
  | Surface.Divide -> Core.Divide
  | Surface.Modulo -> Core.Modulo
  | Surface.LeftShift -> Core.LeftShift
  | Surface.RightShift -> Core.RightShift
  | Surface.IsEqual -> Core.IsEqual
  | Surface.NotEqual -> Core.NotEqual
  | Surface.LessThan -> Core.LessThan
  | Surface.LessThanOrEqual -> Core.LessThanOrEqual
  | Surface.GreaterThan -> Core.GreaterThan
  | Surface.GreaterThanOrEqual -> Core.GreaterThanOrEqual
  | Surface.BitwiseAnd -> Core.BitwiseAnd
  | Surface.BitwiseXor -> Core.BitwiseXor
  | Surface.BitwiseOr -> Core.BitwiseOr
  | Surface.LogicAnd -> Core.LogicAnd
  | Surface.LogicOr -> Core.LogicOr
  | Surface.Assign | Surface.Mutate ->
      invalid_arg "assignment-like operators are lowered separately"

let core_unary_op_of_surface = function
  | Surface.Not -> Core.Not
  | Surface.Negate -> Core.Negate
  | Surface.Complement -> Core.Complement

let rec surface_type_to_core (ty : Surface.haven_type) : Core.haven_type =
  let value =
    match ty.value with
    | Surface.NumericType n -> Core.NumericType n
    | Surface.VecType v -> Core.VecType v
    | Surface.MatrixType m -> Core.MatrixType m
    | Surface.VecHoleType -> Core.VecHoleType
    | Surface.MatrixHoleType -> Core.MatrixHoleType
    | Surface.FloatType -> Core.FloatType
    | Surface.VoidType -> Core.VoidType
    | Surface.StringType -> Core.StringType
    | Surface.CustomType c ->
        Core.CustomType { Core.name = mk_core_ident c.name.loc c.name.value }
    | Surface.CellType inner -> Core.CellType (surface_type_to_core inner)
    | Surface.FunctionType fn ->
        Core.FunctionType
          (mk_core fn.loc
             {
               Core.param_types = List.map surface_type_to_core fn.value.param_types;
               return_type = surface_type_to_core fn.value.return_type;
               vararg = fn.value.vararg;
             })
    | Surface.PointerType inner -> Core.PointerType (surface_type_to_core inner)
    | Surface.BoxType inner -> Core.BoxType (surface_type_to_core inner)
    | Surface.ArrayType arr ->
        Core.ArrayType
          (mk_core arr.loc
             {
               Core.element = surface_type_to_core arr.value.element;
               count =
                 surface_literal_to_core
                   (fun _ -> invalid_arg "array count literals do not contain expressions")
                   arr.value.count;
             })
    | Surface.TemplatedType templ ->
        Core.TemplatedType
          (mk_core templ.loc
             {
               Core.outer = mk_core_ident templ.value.outer.loc templ.value.outer.value;
               inner = List.map surface_type_to_core templ.value.inner;
             })
  in
  mk_core_type ty.loc value

and surface_identifier_to_core (id : Surface.identifier) : Core.identifier =
  mk_core_ident id.loc id.value

and surface_literal_to_core map_expr (lit : Surface.literal) : Core.literal =
  let value =
    match lit.value with
    | Surface.Integer i -> Core.Integer i
    | Surface.Float f -> Core.Float f
    | Surface.String s -> Core.String s
    | Surface.Char c -> Core.Char c
    | Surface.Matrix mat -> Core.Matrix (surface_mat_to_core map_expr mat)
    | Surface.Vector vec -> Core.Vector (surface_vec_to_core map_expr vec)
    | Surface.Enum enum -> Core.Enum (surface_enum_literal_to_core map_expr enum)
  in
  mk_core_literal lit.loc value

and core_bool_literal_expr loc value =
  mk_core_expr loc (Core.Literal (mk_core_literal loc (Core.Bool value)))

and core_false_expr loc = core_bool_literal_expr loc false

and core_true_expr loc = core_bool_literal_expr loc true

and surface_vec_to_core map_expr (vec : Surface.vec_literal) : Core.vec_literal =
  mk_core_vec vec.loc
    { Core.elements = List.map map_expr vec.value.elements }

and surface_mat_to_core map_expr (mat : Surface.mat_literal) : Core.mat_literal =
  mk_core_mat mat.loc
    { Core.rows = List.map map_expr mat.value.rows }

and surface_enum_literal_to_core map_expr (enum : Surface.enum_literal) : Core.enum_literal =
  mk_core_enum enum.loc
    {
      Core.enum_name = surface_identifier_to_core enum.value.enum_name;
      enum_variant = surface_identifier_to_core enum.value.enum_variant;
      types = List.map surface_type_to_core enum.value.types;
      wrapped = List.map map_expr enum.value.wrapped;
    }

and core_int_literal_expr loc value =
  mk_core_expr loc (Core.Literal (mk_core_literal loc (Core.Integer value)))

and core_identifier_expr (id : Core.identifier) =
  mk_core_expr id.loc (Core.Identifier id)

and core_binary_expr loc op left right =
  mk_core_expr loc (Core.Binary (mk_core loc { Core.left; right; op }))

and core_match_arm loc pattern expr = mk_core_arm loc { Core.pattern = pattern; expr }

let rec const_int_of_surface_expr (expr : Surface.expression) =
  match expr.value with
  | Surface.Literal lit -> const_int_of_surface_literal lit
  | Surface.Unary unary -> (
      match (unary.value.op, const_int_of_surface_expr unary.value.inner) with
      | Surface.Negate, Some i -> Some (-i)
      | Surface.Complement, Some i -> Some (lnot i)
      | _ -> None)
  | Surface.Binary binary -> (
      match
        ( const_int_of_surface_expr binary.value.left,
          const_int_of_surface_expr binary.value.right )
      with
      | Some left, Some right -> (
          match binary.value.op with
          | Surface.Add -> Some (left + right)
          | Surface.Subtract -> Some (left - right)
          | Surface.Multiply -> Some (left * right)
          | Surface.Divide -> if right = 0 then None else Some (left / right)
          | Surface.Modulo -> if right = 0 then None else Some (left mod right)
          | Surface.LeftShift -> Some (left lsl right)
          | Surface.RightShift -> Some (left asr right)
          | Surface.BitwiseAnd -> Some (left land right)
          | Surface.BitwiseXor -> Some (left lxor right)
          | Surface.BitwiseOr -> Some (left lor right)
          | _ -> None)
      | _ -> None)
  | _ -> None

and const_int_of_surface_literal (lit : Surface.literal) =
  match lit.value with Surface.Integer i -> Some i | _ -> None

let trip_count_of_range (range : Surface.iter_range) =
  match
    ( const_int_of_surface_expr range.value.range_start,
      const_int_of_surface_expr range.value.range_end,
      match range.value.range_incr with
      | None -> Some 1
      | Some incr -> const_int_of_surface_expr incr )
  with
  | Some start, Some finish, Some step ->
      if step = 0 then None
      else if (step > 0 && start > finish) || (step < 0 && start < finish) then
        Some 0
      else
        let distance = abs (finish - start) in
        let trips = (distance / abs step) + 1 in
        Some trips
  | _ -> None

let fresh_identifier st prefix loc = mk_core_ident loc (fresh_name st prefix)

let lifecycle_function_name (target : Surface.identifier) kind =
  Printf.sprintf "__haven_%s_%s" kind target.value

let mk_surface_pointer_type loc inner =
  mk_surface_type loc (Surface.PointerType inner)

let mk_surface_void_type loc = mk_surface_type loc Surface.VoidType

let synthesize_surface_lifecycle_fn
    (target : Surface.identifier)
    kind loc
    (user_params : Surface.param list)
    body =
  let self_ident = mk_surface_ident loc "self" in
  let target_ty =
    mk_surface_type loc (Surface.CustomType { name = mk_surface_ident target.loc target.value })
  in
  let self_ty = mk_surface_pointer_type loc target_ty in
  mk_surface loc
    {
      Surface.public = false;
      impure = true;
      name = mk_surface_ident loc (lifecycle_function_name target kind);
      definition = Some body;
      intrinsic = None;
      params =
        mk_surface loc
          {
            Surface.params =
              (mk_surface loc { Surface.name = self_ident; ty = self_ty }) :: user_params;
            vararg = false;
          };
      return_type = Some (mk_surface_void_type loc);
      vararg = false;
    }

let merge_surface_extension
    (target : Surface.identifier)
    (existing : surface_extension_hooks)
    (ext : Surface.type_extend) =
  let construct =
    match (existing.construct, ext.value.construct) with
    | Some _, Some _ ->
        failwith
          (Printf.sprintf "duplicate construct block for type %s" target.value)
    | Some body, None | None, Some body -> Some body
    | None, None -> None
  in
  let destruct =
    match (existing.destruct, ext.value.destruct) with
    | Some _, Some _ ->
        failwith
          (Printf.sprintf "duplicate destruct block for type %s" target.value)
    | Some body, None | None, Some body -> Some body
    | None, None -> None
  in
  { construct; destruct; loc = existing.loc }

let rec surface_program_to_core st (program : Surface.program) : Core.program =
  let extensions, decls_rev =
    List.fold_left
      (fun (extensions, decls_rev) (decl : Surface.top_decl) ->
        match decl.value with
        | Surface.Extend ext ->
            let existing =
              match List.assoc_opt ext.value.target.value extensions with
              | Some hooks -> hooks
              | None -> { construct = None; destruct = None; loc = ext.loc }
            in
            let hooks = merge_surface_extension ext.value.target existing ext in
            ((ext.value.target.value, hooks) :: List.remove_assoc ext.value.target.value extensions, decls_rev)
        | _ -> (extensions, decl :: decls_rev))
      ([], [])
      program.value.decls
  in
  let seen_types =
    List.filter_map
      (fun (decl : Surface.top_decl) ->
        match decl.value with
        | Surface.TDecl ty -> Some ty.value.name.value
        | _ -> None)
      program.value.decls
  in
  st.known_types <-
    List.fold_left (fun acc name -> String_set.add name acc) String_set.empty seen_types;
  let decls =
    List.rev decls_rev
    |> List.concat_map (fun (decl : Surface.top_decl) ->
           match decl.value with
           | Surface.TDecl ty -> (
               let hooks = List.assoc_opt ty.value.name.value extensions in
               (match (hooks, ty.value.data) with
               | ( Some { construct; destruct; _ },
                   Surface.TypeDeclStruct _ )
                 when Option.is_some construct || Option.is_some destruct ->
                   ()
               | Some _, _ ->
                   failwith
                     (Printf.sprintf
                        "extend target %s must be a struct in the first-pass lifecycle model"
                        ty.value.name.value)
               | None, _ -> ());
               let construct : Surface.function_decl option =
                 Option.bind hooks (fun hooks ->
                     Option.map
                       (fun (construct : Surface.lifecycle_construct) ->
                         synthesize_surface_lifecycle_fn ty.value.name "construct" ty.loc
                           construct.value.params construct.value.body)
                       hooks.construct)
               in
               let destruct : Surface.function_decl option =
                 Option.bind hooks (fun hooks ->
                     Option.map
                       (synthesize_surface_lifecycle_fn ty.value.name "destruct" ty.loc [])
                       hooks.destruct)
               in
               let ty =
                 {
                   ty with
                   value = { ty.value with construct; destruct };
                 }
               in
               let core_ty = surface_top_decl_to_core st { decl with value = Surface.TDecl ty } in
               let lifecycle_fns : Surface.function_decl option list = [ construct; destruct ] in
               let extra_fdecls =
                 List.filter_map
                   (fun (hook : Surface.function_decl option) ->
                     Option.map
                       (fun (fn : Surface.function_decl) ->
                         mk_core fn.loc (Core.FDecl (surface_function_decl_to_core st fn)))
                       hook)
                   lifecycle_fns
               in
               core_ty :: extra_fdecls)
           | Surface.Extend _ -> []
           | _ -> [ surface_top_decl_to_core st decl ])
  in
  List.iter
    (fun (name, _) ->
      if not (List.mem name seen_types) then
        failwith (Printf.sprintf "extend target %s does not match any declared type" name))
    extensions;
  mk_core program.loc { Core.decls = decls }

and surface_top_decl_to_core st (decl : Surface.top_decl) : Core.top_decl =
  let value =
    match decl.value with
    | Surface.FDecl fn -> Core.FDecl (surface_function_decl_to_core st fn)
    | Surface.TDecl ty -> Core.TDecl (surface_type_decl_to_core st ty)
    | Surface.VDecl v -> Core.VDecl (surface_var_decl_to_core st v)
    | Surface.Import i -> Core.Import { value = i.value; loc = i.loc }
    | Surface.CImport i -> Core.CImport { value = i.value; loc = i.loc }
    | Surface.Foreign f -> Core.Foreign (surface_foreign_to_core st f)
    | Surface.Extend _ ->
        failwith "surface extend declarations must be merged before core lowering"
  in
  mk_core decl.loc value

and surface_function_decl_to_core st (fn : Surface.function_decl) : Core.function_decl =
  let value =
    {
      Core.public = fn.value.public;
      impure = fn.value.impure;
      name = surface_identifier_to_core fn.value.name;
      definition =
        Option.map (surface_block_to_core st ~context:`Value) fn.value.definition;
      intrinsic = Option.map (surface_intrinsic_to_core st) fn.value.intrinsic;
      params = surface_param_list_to_core st fn.value.params;
      return_type = Option.map surface_type_to_core fn.value.return_type;
      vararg = fn.value.vararg;
    }
  in
  mk_core fn.loc value

and surface_intrinsic_to_core _st (intr : Surface.intrinsic) : Core.intrinsic =
  mk_core intr.loc
    {
      Core.name = { value = intr.value.name.value; loc = intr.value.name.loc };
      types = List.map surface_type_to_core intr.value.types;
    }

and surface_param_list_to_core st (params : Surface.param_list) : Core.param_list =
  mk_core params.loc
    {
      Core.params = List.map (surface_param_to_core st) params.value.params;
      vararg = params.value.vararg;
    }

and surface_param_to_core _st (param : Surface.param) : Core.param =
  let value : Core.param_desc =
    {
      Core.name = surface_identifier_to_core param.value.name;
      ty = surface_type_to_core param.value.ty;
    }
  in
  mk_core param.loc value

and surface_var_decl_to_core st (decl : Surface.var_decl) : Core.var_decl =
  mk_core decl.loc
    {
      Core.name = surface_identifier_to_core decl.value.name;
      public = decl.value.public;
      is_mutable = decl.value.is_mutable;
      ty = surface_type_to_core decl.value.ty;
      init_expr = Option.map (surface_expr_to_core st) decl.value.init_expr;
    }

and surface_type_decl_to_core st (decl : Surface.type_decl) : Core.type_decl =
  mk_core decl.loc
    {
      Core.name = surface_identifier_to_core decl.value.name;
      data = surface_type_decl_data_to_core st decl.value.data;
      construct = Option.map (surface_function_decl_to_core st) decl.value.construct;
      destruct = Option.map (surface_function_decl_to_core st) decl.value.destruct;
    }

and surface_type_decl_data_to_core st = function
  | Surface.TypeDeclAlias ty -> Core.TypeDeclAlias (surface_type_to_core ty)
  | Surface.TypeDeclStruct s -> Core.TypeDeclStruct (surface_struct_decl_to_core st s)
  | Surface.TypeDeclEnum e -> Core.TypeDeclEnum (surface_enum_decl_to_core st e)
  | Surface.TypeDeclForward -> Core.TypeDeclForward

and surface_struct_decl_to_core st (decl : Surface.struct_decl) : Core.struct_decl =
  mk_core decl.loc
    {
      Core.fields = List.map (surface_struct_field_to_core st) decl.value.fields;
      lifecycle =
        Option.map
          (fun (lifecycle : Surface.struct_lifecycle) ->
            mk_core lifecycle.loc
              {
                Core.constructor = Option.map surface_identifier_to_core lifecycle.value.constructor;
                destructor = Option.map surface_identifier_to_core lifecycle.value.destructor;
              })
          decl.value.lifecycle;
    }

and surface_struct_field_to_core _st (field : Surface.struct_field) :
    Core.struct_field =
  let value : Core.struct_field_desc =
    {
      Core.name = surface_identifier_to_core field.value.name;
      ty = surface_type_to_core field.value.ty;
    }
  in
  mk_core field.loc value

and surface_enum_decl_to_core st (decl : Surface.enum_decl) : Core.enum_decl =
  mk_core decl.loc
    {
      Core.generics = List.map surface_identifier_to_core decl.value.generics;
      variants = List.map (surface_enum_variant_to_core st) decl.value.variants;
    }

and surface_enum_variant_to_core _st (variant : Surface.enum_variant) :
    Core.enum_variant =
  mk_core variant.loc
    {
      Core.name = surface_identifier_to_core variant.value.name;
      inner_tys = List.map surface_type_to_core variant.value.inner_tys;
    }

and surface_foreign_to_core st (foreign : Surface.foreign) : Core.foreign =
  let normalize_foreign_decl (fn : Surface.function_decl) =
    {
      fn with
      value =
        {
          fn.value with
          public = true;
          impure = true;
        };
    }
  in
  mk_core foreign.loc
    {
      Core.lib = { value = foreign.value.lib.value; loc = foreign.value.lib.loc };
      decls =
        List.map
          (fun fn -> surface_function_decl_to_core st (normalize_foreign_decl fn))
          foreign.value.decls;
    }

and surface_block_to_core st ~context (block : Surface.block) : Core.block =
  let statements =
    List.concat_map (surface_statement_to_core st) block.value.statements
  in
  match (context, block.value.result) with
  | `Value, result ->
      mk_core_block block.loc
        { Core.statements = statements; result = Option.map (surface_expr_to_core st) result }
  | `Statement, None ->
      mk_core_block block.loc { Core.statements = statements; result = None }
  | `Statement, Some expr ->
      let expr_stmt =
        mk_core_stmt expr.loc (Core.Expression (surface_expr_to_core st expr))
      in
      mk_core_block block.loc
        { Core.statements = statements @ [ expr_stmt ]; result = None }

and surface_statement_to_core st (stmt : Surface.statement) : Core.statement list =
  match stmt.value with
  | Surface.Expression expr ->
      [ mk_core_stmt stmt.loc (Core.Expression (surface_expr_to_core st expr)) ]
  | Surface.Let binding ->
      [
        mk_core_stmt stmt.loc
          (Core.Let
             (mk_core binding.loc
                {
                  Core.mut = binding.value.mut;
                  ty = Option.map surface_type_to_core binding.value.ty;
                  name = surface_identifier_to_core binding.value.name;
                  init_expr = surface_expr_to_core st binding.value.init_expr;
                }));
      ]
  | Surface.CompileAssert compile_assert ->
      [
        mk_core_stmt stmt.loc
          (Core.CompileAssert
             (mk_core compile_assert.loc
                {
                  Core.cond = surface_expr_to_core st compile_assert.value.cond;
                  message =
                    {
                      value = compile_assert.value.message.value;
                      loc = compile_assert.value.message.loc;
                    };
                }));
      ]
  | Surface.Return expr ->
      [ mk_core_stmt stmt.loc (Core.Return (Option.map (surface_expr_to_core st) expr)) ]
  | Surface.Defer expr ->
      [ mk_core_stmt stmt.loc (Core.Defer (surface_expr_to_core st expr)) ]
  | Surface.While while_stmt ->
      [
        mk_core_stmt stmt.loc
          (Core.Loop
             (mk_core while_stmt.loc
                {
                  Core.init = [];
                  cond = surface_expr_to_core st while_stmt.value.cond;
                  body =
                    surface_block_to_core st ~context:`Statement
                      while_stmt.value.body;
                  step = [];
                  iteration_hint = None;
                }));
      ]
  | Surface.Break -> [ mk_core_stmt stmt.loc Core.Break ]
  | Surface.Continue -> [ mk_core_stmt stmt.loc Core.Continue ]
  | Surface.Iter iter -> [ lower_iter_statement st stmt.loc iter ]

and lower_iter_statement st loc (iter : Surface.iter_stmt) : Core.statement =
  let end_name = fresh_identifier st "iter.end" iter.loc in
  let step_name = fresh_identifier st "iter.step" iter.loc in
  let index_name = surface_identifier_to_core iter.value.var in
  let start_expr = surface_expr_to_core st iter.value.range.value.range_start in
  let end_expr = surface_expr_to_core st iter.value.range.value.range_end in
  let step_expr =
    match iter.value.range.value.range_incr with
    | Some expr -> surface_expr_to_core st expr
    | None -> core_int_literal_expr iter.loc 1
  in
  let end_stmt =
    mk_core_stmt loc
      (Core.Let
         (mk_core loc
            {
              Core.mut = false;
              ty = Some (default_iter_type end_name.loc);
              name = end_name;
              init_expr = end_expr;
            }))
  in
  let step_stmt_binding =
    mk_core_stmt loc
      (Core.Let
         (mk_core loc
            {
              Core.mut = false;
              ty = Some (default_iter_type step_name.loc);
              name = step_name;
              init_expr = step_expr;
            }))
  in
  let index_stmt =
    mk_core_stmt loc
      (Core.Let
         (mk_core iter.value.var.loc
            {
              Core.mut = true;
              ty = Some (default_iter_type index_name.loc);
              name = index_name;
              init_expr = start_expr;
            }))
  in
  let index_expr = core_identifier_expr index_name in
  let end_ref = core_identifier_expr end_name in
  let step_ref = core_identifier_expr step_name in
  let cond =
    match Option.bind iter.value.range.value.range_incr const_int_of_surface_expr with
    | Some step when step < 0 ->
        core_binary_expr loc Core.GreaterThanOrEqual index_expr end_ref
    | Some _ | None when Option.is_none iter.value.range.value.range_incr ->
        core_binary_expr loc Core.LessThanOrEqual index_expr end_ref
    | Some _ ->
        core_binary_expr loc Core.LessThanOrEqual index_expr end_ref
    | None ->
        let direction =
          core_binary_expr loc Core.LessThan step_ref (core_int_literal_expr loc 0)
        in
        let non_negative_arm =
          core_match_arm loc
            (mk_core_pattern loc (Core.PatternLiteral (mk_core_literal loc (Core.Integer 0))))
            (core_binary_expr loc Core.LessThanOrEqual index_expr end_ref)
        in
        let negative_arm =
          core_match_arm loc (mk_core_pattern loc Core.PatternDefault)
            (core_binary_expr loc Core.GreaterThanOrEqual index_expr end_ref)
        in
        mk_core_expr loc
          (Core.Match (mk_core loc { Core.expr = direction; arms = [ non_negative_arm; negative_arm ] }))
  in
  let next_value = core_binary_expr loc Core.Add index_expr step_ref in
  let increment =
    mk_core_stmt loc
      (Core.Expression
         (mk_core_expr loc
            (Core.Assign (mk_core loc { Core.target = index_expr; value = next_value }))))
  in
  let body = surface_block_to_core st ~context:`Statement iter.value.body in
  let iteration_hint =
    Option.map
      (fun n -> mk_core_iteration_hint iter.loc (Core.KnownTripCount n))
      (trip_count_of_range iter.value.range)
  in
  mk_core_stmt loc
    (Core.Loop
       (mk_core iter.loc
          {
            Core.init = [ end_stmt; step_stmt_binding; index_stmt ];
            cond;
            body;
            step = [ increment ];
            iteration_hint;
          }))

and surface_expr_to_core st (expr : Surface.expression) : Core.expression =
  let value =
    match expr.value with
    | Surface.Binary binary -> (
        match binary.value.op with
        | Surface.Assign ->
            Core.Assign
              (mk_core binary.loc
                 {
                   Core.target = surface_expr_to_core st binary.value.left;
                   value = surface_expr_to_core st binary.value.right;
                 })
        | Surface.Mutate ->
            Core.Mutate
              (mk_core binary.loc
                 {
                   Core.target = surface_expr_to_core st binary.value.left;
                   value = surface_expr_to_core st binary.value.right;
                 })
        | op ->
            Core.Binary
              (mk_core binary.loc
                 {
                   Core.left = surface_expr_to_core st binary.value.left;
                   right = surface_expr_to_core st binary.value.right;
                   op = core_binary_op_of_surface op;
                 }))
    | Surface.Unary unary ->
        Core.Unary
          (mk_core unary.loc
             {
               Core.inner = surface_expr_to_core st unary.value.inner;
               op = core_unary_op_of_surface unary.value.op;
             })
    | Surface.Literal lit ->
        Core.Literal (surface_literal_to_core (surface_expr_to_core st) lit)
    | Surface.Block block -> Core.Block (surface_block_to_core st ~context:`Value block)
    | Surface.Identifier id -> Core.Identifier (surface_identifier_to_core id)
    | Surface.Initializer init ->
        Core.Initializer
          (mk_core init.loc
             {
               Core.exprs = List.map (surface_expr_to_core st) init.value.exprs;
             })
    | Surface.As cast ->
        Core.As
          (mk_core cast.loc
             {
               Core.target_type = surface_type_to_core cast.value.target_type;
               inner = surface_expr_to_core st cast.value.inner;
             })
    | Surface.SizeExpr inner -> Core.SizeExpr (surface_expr_to_core st inner)
    | Surface.SizeType ty -> Core.SizeType (surface_type_to_core ty)
    | Surface.Nil -> Core.Nil
    | Surface.Zero -> Core.Zero
    | Surface.If ifx -> lower_if_expr st expr.loc ifx
    | Surface.Match m ->
        Core.Match
          (mk_core m.loc
             {
               Core.expr = surface_expr_to_core st m.value.expr;
               arms = List.map (surface_match_arm_to_core st) m.value.arms;
             })
    | Surface.BoxExpr ({ value = Surface.Identifier id; _ } as inner)
      when String_set.mem id.value st.known_types ->
        Core.BoxType
          (mk_core_type inner.loc
             (Core.CustomType { name = surface_identifier_to_core id }))
    | Surface.BoxExpr { value = Surface.Call call; loc }
      when (match call.value.target.value with
           | Surface.Identifier id -> String_set.mem id.value st.known_types
           | _ -> false) ->
        let ty =
          match call.value.target.value with
          | Surface.Identifier id ->
              mk_core_type call.value.target.loc
                (Core.CustomType { name = surface_identifier_to_core id })
          | _ -> assert false
        in
        Core.BoxConstruct
          (mk_core loc
             { Core.ty; args = List.map (surface_expr_to_core st) call.value.params })
    | Surface.BoxExpr inner -> Core.BoxExpr (surface_expr_to_core st inner)
    | Surface.BoxType ty -> Core.BoxType (surface_type_to_core ty)
    | Surface.Unbox inner -> Core.Unbox (surface_expr_to_core st inner)
    | Surface.Ref inner -> Core.Ref (surface_expr_to_core st inner)
    | Surface.Load inner -> Core.Load (surface_expr_to_core st inner)
    | Surface.Call call ->
        Core.Call
          (mk_core call.loc
             {
               Core.target = surface_expr_to_core st call.value.target;
               params = List.map (surface_expr_to_core st) call.value.params;
             })
    | Surface.Index index ->
        Core.Index
          (mk_core index.loc
             {
               Core.target = surface_expr_to_core st index.value.target;
               index = surface_expr_to_core st index.value.index;
             })
    | Surface.Field field ->
        Core.Field
          (mk_core field.loc
             {
               Core.target = surface_expr_to_core st field.value.target;
               arrow = field.value.arrow;
               field = surface_identifier_to_core field.value.field;
             })
  in
  mk_core_expr expr.loc value

and lower_if_expr st loc (ifx : Surface.if_expr) : Core.expression_desc =
  let false_arm =
    core_match_arm loc
      (mk_core_pattern loc (Core.PatternLiteral (mk_core_literal loc (Core.Bool false))))
      (match ifx.value.else_branch with
      | None -> mk_core_expr loc (Core.Block (mk_core_block loc { statements = []; result = None }))
      | Some block ->
          mk_core_expr block.loc
            (Core.Block (surface_block_to_core st ~context:`Value block)))
  in
  let true_arm =
    core_match_arm loc
      (mk_core_pattern loc (Core.PatternLiteral (mk_core_literal loc (Core.Bool true))))
      (mk_core_expr ifx.value.then_branch.loc
         (Core.Block
            (surface_block_to_core st ~context:`Value ifx.value.then_branch)))
  in
  Core.Match
    (mk_core loc
       {
         Core.expr =
           mk_core_expr ifx.value.cond.loc
             (Core.ToBool (surface_expr_to_core st ifx.value.cond));
         arms = [ true_arm; false_arm ];
       })

and surface_match_arm_to_core st (arm : Surface.match_arm) : Core.match_arm =
  mk_core_arm arm.loc
    {
      Core.pattern = surface_match_pattern_to_core st arm.value.pattern;
      expr = surface_expr_to_core st arm.value.expr;
    }

and surface_match_pattern_to_core st (pat : Surface.match_pattern) :
    Core.match_pattern =
  let value =
    match pat.value with
    | Surface.PatternDefault -> Core.PatternDefault
    | Surface.PatternLiteral lit ->
        Core.PatternLiteral (surface_literal_to_core (surface_expr_to_core st) lit)
    | Surface.PatternEnum enum -> Core.PatternEnum (surface_pattern_enum_to_core enum)
  in
  mk_core_pattern pat.loc value

and surface_pattern_enum_to_core (enum : Surface.pattern_enum) : Core.pattern_enum =
  mk_core enum.loc
    {
      Core.enum_name = Option.map surface_identifier_to_core enum.value.enum_name;
      enum_variant = surface_identifier_to_core enum.value.enum_variant;
      binding = List.map surface_pattern_binding_to_core enum.value.binding;
    }

and surface_pattern_binding_to_core (binding : Surface.pattern_binding) :
    Core.pattern_binding =
  let value =
    match binding.value with
    | Surface.BindingIgnored -> Core.BindingIgnored
    | Surface.BindingNamed id -> Core.BindingNamed (surface_identifier_to_core id)
  in
  mk_core_binding binding.loc value

let core_of_surface (parsed : Surface.parsed_program) : Core.parsed_program =
  let st = fresh_state () in
  { Core.program = surface_program_to_core st parsed.program }

let core_of_expanded_cst parsed =
  core_of_surface (surface_of_cst parsed)

let core_of_cst parsed =
  let expanded = Imports.expand_cst parsed in
  core_of_expanded_cst expanded.parsed
