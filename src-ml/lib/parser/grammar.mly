%{
    open Haven_cst.Cst

    let mk_loc start_pos end_pos value = with_location ~start_pos ~end_pos value
    let mk_id text start_pos end_pos = mk_loc start_pos end_pos text
    let mk_expr start_pos end_pos desc = mk_loc start_pos end_pos desc
    let mk_binary start_pos end_pos op left right =
      let bin = mk_expr start_pos end_pos { left; right; op } in
      mk_expr start_pos end_pos (Binary bin)
    let mk_unary start_pos end_pos op inner =
      mk_expr start_pos end_pos (Unary (mk_expr start_pos end_pos { inner; op }))
%}

%token <string> IDENT
%token <Haven_token.Token.numeric_type> NUMERIC_TYPE
%token <Haven_token.Token.vec_type> VEC_TYPE
%token <Haven_token.Token.mat_type> MAT_TYPE
%token FLOAT_TYPE VOID_TYPE STR_TYPE
%token <int> INT_LIT
%token <float> FLOAT_LIT
%token <int> HEX_LIT OCT_LIT BIN_LIT
%token <string> STRING_LIT
%token <char> CHAR_LIT
%token ARROW FATARROW SCOPE WALRUS
%token LOGIC_AND LOGIC_OR EQEQ BANGEQ LE GE
%token LSHIFT RSHIFT LPAREN RPAREN LBRACE RBRACE LBRACKET RBRACKET
%token LT GT COMMA DOT SEMICOLON COLON STAR CARET
%token PLUS MINUS SLASH PERCENT EQUAL AMP PIPE BANG TILDE UNDERSCORE
%token EOF

(* Main keywords *)
%token PUB FN MUT IF ELSE LET WHILE BREAK CONTINUE MATCH AS ITER
%token LOAD RET STRUCT TYPE NIL DEFER IMPURE ENUM IMPORT CIMPORT SIZE
%token BOX UNBOX INTRINSIC FOREIGN DATA STATE VEC MAT FUNCTION
%token VAFUNCTION CELL REF

(* Operator precedence table *)
%left LOGIC_OR
%left LOGIC_AND
%left BITWISE_OR
%left BITWISE_XOR
%left BITWISE_AND
%left EQEQ BANGEQ
%left LT GT LE GE
%left LSHIFT RSHIFT
%left PLUS MINUS
%left MULITPLY DIVIDE MODULO
%nonassoc UMINUS BANG TILDE

%start <program> program
%type <block> block
%type <block_item list> block_items
%type <struct_field> struct_field
%%

program: decls=top_decl+ EOF { mk_loc $startpos $endpos { decls } } ;

(** TOP-LEVEL CONSTRUCTS **)

top_decl:
  | d=fn_definition { mk_loc $startpos $endpos (FDecl d) }
  | d=fn_forward_decl { mk_loc $startpos $endpos (FDecl d) }
  | i=import_decl { mk_loc $startpos $endpos (Import i) }
  | i=cimport_decl { mk_loc $startpos $endpos (CImport i) }
  | f=foreign_decl { mk_loc $startpos $endpos (Foreign f) }
  | t=type_decl { mk_loc $startpos $endpos (TDecl t) }
  | v=global_decl { mk_loc $startpos $endpos (VDecl v) }
  ;

import_decl: IMPORT i=STRING_LIT SEMICOLON { mk_id i $startpos(i) $endpos(i) } ;
cimport_decl: CIMPORT i=STRING_LIT SEMICOLON { mk_id i $startpos(i) $endpos(i) } ;

foreign_decl:
  FOREIGN l=STRING_LIT LBRACE d=fn_forward_decl* RBRACE
    { mk_loc $startpos $endpos { lib = mk_id l $startpos(l) $endpos(l); decls = d } }
  ;

fn_definition:
  f=fn_header b=block
    { let header = f.value in mk_loc $startpos $endpos { header with definition = Some b } }
  ;

fn_forward_decl:
  f=fn_header i=option(fn_intrinsic) SEMICOLON
    { let header = f.value in mk_loc $startpos $endpos { header with intrinsic = i } }
  ;

fn_header:
  pub=boption(PUB) impure=boption(IMPURE) FN name=identifier LPAREN p=params RPAREN rt=return_type?
    { mk_loc $startpos $endpos { public = pub; impure = impure; name; definition = None; intrinsic = None; params = p; return_type = rt; vararg = p.value.vararg } }
  ;
return_type: ARROW t=haven_type { t } ;

fn_intrinsic:
  INTRINSIC n=STRING_LIT t=separated_list(COMMA, haven_type)
    { mk_loc $startpos $endpos { name = mk_id n $startpos(n) $endpos(n); types = t } }
  ;

params:
  | p=separated_nonempty_list(COMMA, param) va=boption(pair(COMMA, STAR)) { mk_loc $startpos $endpos { params = p; vararg = va } }
  | STAR { mk_loc $startpos $endpos { params = []; vararg = true } }
  | { mk_loc $startpos $endpos { params = []; vararg = false } }
  ;

param: t=haven_type n=identifier { mk_loc $startpos $endpos { name = n; ty = t } } ;

type_decl:
  | TYPE i=identifier EQUAL t=type_defn SEMICOLON { mk_loc $startpos $endpos { name = i; data = t } }
  | TYPE i=identifier SEMICOLON { mk_loc $startpos $endpos { name = i; data = TypeDeclForward } }
  ;
type_defn:
  | t=haven_type { TypeDeclAlias t }
  | s=struct_decl { TypeDeclStruct s }
  | e=enum_decl { TypeDeclEnum e }
  ;

struct_decl: STRUCT LBRACE f=list(struct_field) RBRACE { mk_loc $startpos $endpos { fields = f } } ;
struct_field: t=haven_type n=identifier SEMICOLON {
    let field : struct_field_desc = { name = n; ty = t } in
    mk_loc $startpos $endpos field
} ;

enum_decl: ENUM enum_generics? LBRACE v=separated_nonempty_list(COMMA, enum_variant) RBRACE { mk_loc $startpos $endpos { variants = v } } ;
enum_generics: separated_list(COMMA, IDENT) {} ;
enum_variant: i=identifier t=option(enum_wrapped_type) { mk_loc $startpos $endpos { name = i; inner_ty = t }} ;
enum_wrapped_type: LPAREN t=haven_type RPAREN { t } ;

global_decl: p=boption(PUB) d=global_decl_inner SEMICOLON { mk_loc $startpos $endpos { d.value with public = p } } ;
global_decl_inner:
  | DATA b=global_decl_binding { b }
  | STATE b=global_decl_binding { mk_loc $startpos $endpos { b.value with is_mutable = true } }
  ;
global_decl_binding: t=haven_type n=identifier e=option(bind_expr) {
    mk_loc $startpos $endpos { name = n; public = false; is_mutable = false; ty = t; init_expr = e }
} ;
bind_expr: EQUAL e=expr { e } ;

block: LBRACE b=block_items { mk_loc $startpos $endpos { items = b } } ;
block_items:
  | RBRACE { [] }
  | s=stmt b=block_items { mk_loc $startpos(s) $endpos(s) (BlockStatement s) :: b }
  | e=expr RBRACE { [mk_loc $startpos(e) $endpos(e) (BlockExpression e)] }
  ;

(** STATEMENTS **)

stmt: s=stmt_inner SEMICOLON { mk_loc $startpos $endpos s } ;
stmt_inner:
  | LET m=boption(MUT) n=identifier EQUAL e=expr { Let (mk_loc $startpos $endpos { mut = m; name = n; ty = None; init_expr = e; }) }
  | LET m=boption(MUT) t=haven_type n=identifier EQUAL e=expr { Let (mk_loc $startpos $endpos { mut = m; name = n; ty = Some t; init_expr = e; }) }
  | RET e=option(expr) { Return e }
  | DEFER e=expr { Defer e }
  | ITER r=iter_range v=identifier b=block { Iter (mk_loc $startpos $endpos { range = r; var = v; body = b }) }
  | WHILE c=expr b=block { While (mk_loc $startpos $endpos { cond = c; body = b }) }
  | BREAK { Break }
  | CONTINUE { Continue }
  | e=expr { Expression e }
  | { Empty }
  ;

iter_range: s=expr COLON e=expr i=option(iter_incr) { mk_loc $startpos $endpos { range_start = s; range_end = e; range_incr = i } } ;
iter_incr: COLON e=expr { e }

(** EXPRESSIONS **)

(*
 * Splitting the expr grammar is a bit ugly but it enables expressions in contexts where a Boolean operator
 * doesn't make as much sense. For example, Vec<1.0, 2.0, 3.0>. The trailing > is never going to be relational,
 * and it's rare to need something like Vec<1 > 2> - which can be simply written Vec<(1 < > 2)>.
 *)
expr:
  | l=expr EQUAL r=expr { mk_binary $startpos $endpos Assign l r }
  | l=expr WALRUS r=expr { mk_binary $startpos $endpos Mutate l r }
  | l=expr PLUS r=expr { mk_binary $startpos $endpos Add l r }
  | l=expr MINUS r=expr { mk_binary $startpos $endpos Subtract l r }
  | l=expr STAR r=expr %prec MULITPLY { mk_binary $startpos $endpos Multiply l r }
  | l=expr SLASH r=expr %prec DIVIDE { mk_binary $startpos $endpos Divide l r }
  | l=expr PERCENT r=expr %prec MODULO { mk_binary $startpos $endpos Modulo l r }
  | l=expr AMP r=expr %prec BITWISE_AND { mk_binary $startpos $endpos BitwiseAnd l r }
  | l=expr CARET r=expr %prec BITWISE_XOR { mk_binary $startpos $endpos BitwiseXor l r }
  | l=expr PIPE r=expr %prec BITWISE_OR { mk_binary $startpos $endpos BitwiseOr l r }
  | l=expr LSHIFT r=expr { mk_binary $startpos $endpos LeftShift l r }
  | l=expr RSHIFT r=expr { mk_binary $startpos $endpos RightShift l r }
  | l=expr EQEQ r=expr { mk_binary $startpos $endpos IsEqual l r }
  | l=expr BANGEQ r=expr { mk_binary $startpos $endpos NotEqual l r }
  | l=expr LOGIC_AND r=expr { mk_binary $startpos $endpos LogicAnd l r }
  | l=expr LOGIC_OR r=expr { mk_binary $startpos $endpos LogicOr l r }
  | l=expr LT r=expr { mk_binary $startpos $endpos LessThan l r }
  | l=expr LE r=expr { mk_binary $startpos $endpos LessThanOrEqual l r }
  | l=expr GT r=expr { mk_binary $startpos $endpos GreaterThan l r }
  | l=expr GE r=expr { mk_binary $startpos $endpos GreaterThanOrEqual l r }
  | u=unary { u }

unary:
  | BANG i=unary { mk_unary $startpos $endpos Not i }
  | MINUS i=unary %prec UMINUS { mk_unary $startpos $endpos Negate i }
  | TILDE i=unary { mk_unary $startpos $endpos Complement i }
  | REF e=unary { mk_expr $startpos $endpos (Ref e) }
  | LOAD e=unary { mk_expr $startpos $endpos (Load e) }
  | BOX e=unary { mk_expr $startpos $endpos (BoxExpr e) }
  | BOX t=haven_type { mk_expr $startpos $endpos (BoxType t) }
  | UNBOX e=unary { mk_expr $startpos $endpos (Unbox e) }
  | p=postfix { p }
  ;

postfix:
  | p=postfix LPAREN e=expr_seq RPAREN { mk_expr $startpos $endpos (Call (mk_loc $startpos $endpos { target = p; params = e })) }
  | p=postfix LBRACKET e=expr RBRACKET { mk_expr $startpos $endpos (Index (mk_loc $startpos $endpos { target = p; index = e })) }
  | p=postfix DOT i=identifier { mk_expr $startpos $endpos (Field (mk_loc $startpos $endpos { target = p; arrow = false; field = i })) }
  | p=postfix ARROW i=identifier { mk_expr $startpos $endpos (Field (mk_loc $startpos $endpos { target = p; arrow = true; field = i })) }
  | p=primary { p }

expr_seq: exprs=separated_list(COMMA, expr) { exprs } ;

primary:
  | l=literal { mk_expr $startpos $endpos (Literal l) }
  | b=block { mk_expr $startpos $endpos (Block b) }
  | LPAREN e=expr RPAREN { mk_expr $startpos $endpos (ParenthesizedExpression e) }
  | i=init { mk_expr $startpos $endpos (Initializer i) }
  | i=identifier { mk_expr $startpos $endpos (Identifier i) }
  | i=if_expr { mk_expr $startpos $endpos (If i) }
  | m=match_expr { mk_expr $startpos $endpos (Match m) }
  | AS LT t=haven_type GT LPAREN e=expr RPAREN { mk_expr $startpos $endpos (As (mk_loc $startpos $endpos { target_type = t; inner = e })) }
  | SIZE LT t=haven_type GT { mk_expr $startpos $endpos (SizeType t) }
  | SIZE LPAREN e=expr RPAREN { mk_expr $startpos $endpos (SizeExpr e) }
  | NIL { mk_expr $startpos $endpos Nil }
  ;

init: LBRACE exprs=separated_nonempty_list(COMMA, expr) RBRACE { mk_loc $startpos $endpos { exprs } } ;

(** COMPLEX EXPRESSIONS **)

if_expr: IF c=expr t=block e=option(else_expr) { mk_loc $startpos $endpos { cond = c; then_block = t; else_block = e } } ;
else_expr:
  | ELSE e=if_expr { ElseIf e }
  | ELSE b=block { Else b }
  ;

match_expr: MATCH e=expr LBRACE a=separated_nonempty_list(COMMA, match_arm) RBRACE { mk_loc $startpos $endpos { expr = e; arms = a } } ;
match_arm: p=pattern FATARROW e=expr { mk_loc $startpos $endpos { pattern = p; expr = e } } ;

pattern:
  | UNDERSCORE { mk_loc $startpos $endpos PatternDefault }
  | l=literal { mk_loc $startpos $endpos (PatternLiteral l) }
  | v=identifier p=pattern_unwrap { mk_loc $startpos $endpos (PatternEnum (mk_loc $startpos $endpos { enum_name = None; enum_variant = v; binding = p })) }
  | e=identifier SCOPE v=identifier p=pattern_unwrap { mk_loc $startpos $endpos (PatternEnum (mk_loc $startpos $endpos { enum_name = Some e; enum_variant = v; binding = p })) }
  ;
pattern_unwrap:
  | LPAREN b=separated_nonempty_list(COMMA, pattern_binding) RPAREN { b }
  | { [] }
  ;
pattern_binding:
  | UNDERSCORE { mk_loc $startpos $endpos BindingIgnored }
  | i=identifier { mk_loc $startpos $endpos (BindingNamed i) }
  ;

(** LITERALS **)

literal:
  | n=integer_literal { n }
  | i=noninteger_literal { i }

integer_literal:
  | i=HEX_LIT { mk_loc $startpos $endpos (HexInt i) }
  | i=OCT_LIT { mk_loc $startpos $endpos (OctInt i) }
  | i=BIN_LIT { mk_loc $startpos $endpos (BinInt i) }
  | i=INT_LIT { mk_loc $startpos $endpos (DecInt i) }

noninteger_literal:
  | f=FLOAT_LIT { mk_loc $startpos $endpos (Float f) }
  | s=STRING_LIT { mk_loc $startpos $endpos (String s) }
  | c=CHAR_LIT { mk_loc $startpos $endpos (Char c) }
  | m=mat_literal { mk_loc $startpos $endpos (Matrix m) }
  | v=vec_literal { mk_loc $startpos $endpos (Vector v) }
  | e=enum_literal { mk_loc $startpos $endpos (Enum e) }
  ;

mat_literal: MAT LT rows=separated_nonempty_list(COMMA, vec_literal) GT { mk_loc $startpos $endpos { rows } } ;
vec_literal: VEC u=delimited(LT, separated_nonempty_list(COMMA, unary), GT) { mk_loc $startpos $endpos { elements = u } } ;

enum_literal:
  | e=identifier SCOPE v=identifier { mk_loc $startpos $endpos { enum_name = e; enum_variant = v; types = [] } }
  | e=identifier SCOPE t=contained_type_list SCOPE v=identifier { mk_loc $startpos $endpos { enum_name = e; enum_variant = v; types = t } }
  ;

contained_type_list: t=delimited(LT, separated_nonempty_list(COMMA, haven_type), GT) { t } ;

identifier: i=IDENT { mk_id i $startpos $endpos } ;

(** TYPES **)

haven_type:
  | t=type_primary { t }
  | t=type_primary STAR { mk_loc $startpos $endpos (PointerType t) }
  | t=type_primary CARET {
      let ty : haven_type_desc = BoxType t in
      mk_loc $startpos $endpos ty
    }
  | t=type_primary LBRACKET c=integer_literal RBRACKET { mk_loc $startpos $endpos (ArrayType (mk_loc $startpos $endpos { element = t; count = c })) }
  ;

type_primary:
  | t=builtin_type { t }
  | CELL LT t=haven_type GT { mk_loc $startpos $endpos (CellType t) }
  | FUNCTION LT LPAREN p=separated_list(COMMA, haven_type) RPAREN ARROW r=haven_type GT {
    mk_loc $startpos $endpos (FunctionType (mk_loc $startpos $endpos { param_types = p; return_type = r; vararg = false; }))
    }
  | VAFUNCTION LT LPAREN p=separated_list(COMMA, haven_type) RPAREN ARROW r=haven_type GT {
    mk_loc $startpos $endpos (FunctionType (mk_loc $startpos $endpos { param_types = p; return_type = r; vararg = true; }))
    }
  | o=identifier SCOPE LT i=separated_nonempty_list(COMMA, haven_type) GT { mk_loc $startpos $endpos (TemplatedType (mk_loc $startpos $endpos { outer = o; inner = i; })) }
  | i=identifier { mk_loc $startpos $endpos (CustomType { name = i; }) }
  ;

builtin_type:
  | t=NUMERIC_TYPE { mk_loc $startpos $endpos (NumericType t) }
  | t=VEC_TYPE { mk_loc $startpos $endpos (VecType t) }
  | t=MAT_TYPE { mk_loc $startpos $endpos (MatrixType t) }
  | FLOAT_TYPE { mk_loc $startpos $endpos FloatType }
  | VOID_TYPE { mk_loc $startpos $endpos VoidType }
  | STR_TYPE { mk_loc $startpos $endpos StringType }
  ;
