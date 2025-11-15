%{
    open Haven_cst.Cst
%}

%token <string> IDENT
%token <Haven_token.numeric_type> NUMERIC_TYPE
%token <Haven_token.vec_type> VEC_TYPE
%token <Haven_token.mat_type> MAT_TYPE
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

program: decls=top_decl+ EOF { { decls } } ;

(** TOP-LEVEL CONSTRUCTS **)

top_decl:
  | d=fn_definition { FDecl d }
  | d=fn_forward_decl { FDecl d }
  | i=import_decl { Import i }
  | i=cimport_decl { CImport i }
  | f=foreign_decl { Foreign f }
  | t=type_decl { TDecl t }
  | v=global_decl { VDecl v }
  ;

import_decl: IMPORT i=STRING_LIT SEMICOLON { i } ;
cimport_decl: CIMPORT i=STRING_LIT SEMICOLON { i } ;

foreign_decl: FOREIGN l=STRING_LIT LBRACE d=fn_forward_decl* RBRACE { { lib = l; decls = d } } ;

fn_definition: f=fn_header b=block { { f with definition = Some b } } ;
fn_forward_decl: f=fn_header i=option(fn_intrinsic) SEMICOLON { { f with intrinsic = i } } ;

fn_header: pub=boption(PUB) impure=boption(IMPURE) FN name=IDENT LPAREN p=params RPAREN rt=return_type? {
    { public = pub; impure = impure; name = name; definition = None; intrinsic = None; params = p; return_type = rt; vararg = p.vararg }
} ;
return_type: ARROW t=haven_type { t } ;

fn_intrinsic: INTRINSIC n=STRING_LIT t=separated_list(COMMA, haven_type) { { name = n; types = t } } ;

params:
  | p=separated_nonempty_list(COMMA, param) va=boption(pair(COMMA, STAR)) { { params = p; vararg = va } }
  | STAR { { params = []; vararg = true } }
  | { { params = []; vararg = false } }
  ;

param: t=haven_type n=IDENT { { name = n; ty = t } } ;

type_decl:
  | TYPE i=IDENT EQUAL t=type_defn SEMICOLON { { name = i; data = t } }
  | TYPE i=IDENT SEMICOLON { { name = i; data = TypeDeclForward } }
  ;
type_defn:
  | t=haven_type { TypeDeclAlias t }
  | s=struct_decl { TypeDeclStruct s }
  | e=enum_decl { TypeDeclEnum e }
  ;

struct_decl: STRUCT LBRACE f=list(struct_field) RBRACE { { fields = f } } ;
struct_field: t=haven_type n=IDENT SEMICOLON { { name = n; ty = t } } ;

enum_decl: ENUM enum_generics? LBRACE v=separated_nonempty_list(COMMA, enum_variant) RBRACE { { variants = v } } ;
enum_generics: separated_list(COMMA, IDENT) {} ;
enum_variant: i=IDENT t=option(enum_wrapped_type) { { name = i; inner_ty = t }} ;
enum_wrapped_type: LPAREN t=haven_type RPAREN { t } ;

global_decl: p=boption(PUB) d=global_decl_inner SEMICOLON { { d with public = p } } ;
global_decl_inner:
  | DATA b=global_decl_binding { b }
  | STATE b=global_decl_binding { { b with is_mutable = true } }
  ;
global_decl_binding: t=haven_type n=IDENT e=option(bind_expr) {
    { name = n; public = false; is_mutable = false; ty = t; init_expr = e }
} ;
bind_expr: EQUAL e=expr { e } ;

block: LBRACE b=block_items { { items = b } } ;
block_items:
  | RBRACE { [] }
  | s=stmt b=block_items { BlockStatement s :: b }
  | e=expr RBRACE { [BlockExpression e] }
  ;

(** STATEMENTS **)

stmt: s=stmt_inner SEMICOLON { s } ;
stmt_inner:
  | LET m=boption(MUT) n=IDENT EQUAL e=expr { Let { mut = m; name = n; ty = None; init_expr = e; } }
  | LET m=boption(MUT) t=haven_type n=IDENT EQUAL e=expr { Let { mut = m; name = n; ty = Some t; init_expr = e; } }
  | RET e=option(expr) { Return e }
  | DEFER e=expr { Defer e }
  | ITER r=iter_range v=IDENT b=block { Iter { range = r; var = v; body = b } }
  | WHILE c=expr b=block { While { cond = c; body = b } }
  | BREAK { Break }
  | CONTINUE { Continue }
  | e=expr { Expression e }
  | { Empty }
  ;

iter_range: s=expr COLON e=expr i=option(iter_incr) { { range_start = s; range_end = e; range_incr = i } } ;
iter_incr: COLON e=expr { e }

(** EXPRESSIONS **)

(*
 * Splitting the expr grammar is a bit ugly but it enables expressions in contexts where a Boolean operator
 * doesn't make as much sense. For example, Vec<1.0, 2.0, 3.0>. The trailing > is never going to be relational,
 * and it's rare to need something like Vec<1 > 2> - which can be simply written Vec<(1 < > 2)>.
 *)
expr:
  | l=expr EQUAL r=expr { Binary { left = l; right = r; op = Assign; }}
  | l=expr WALRUS r=expr { Binary { left = l; right = r; op = Mutate; }}
  | l=expr PLUS r=expr { Binary { left = l; right = r; op = Add; }}
  | l=expr MINUS r=expr { Binary { left = l; right = r; op = Subtract; }}
  | l=expr STAR r=expr %prec MULITPLY { Binary { left = l; right = r; op = Multiply; }}
  | l=expr SLASH r=expr %prec DIVIDE { Binary { left = l; right = r; op = Divide; }}
  | l=expr PERCENT r=expr %prec MODULO { Binary { left = l; right = r; op = Modulo; }}
  | l=expr AMP r=expr %prec BITWISE_AND { Binary { left = l; right = r; op = BitwiseAnd; }}
  | l=expr CARET r=expr %prec BITWISE_XOR { Binary { left = l; right = r; op = BitwiseXor; }}
  | l=expr PIPE r=expr %prec BITWISE_OR { Binary { left = l; right = r; op = BitwiseOr; }}
  | l=expr LSHIFT r=expr { Binary { left = l; right = r; op = LeftShift; }}
  | l=expr RSHIFT r=expr { Binary { left = l; right = r; op = RightShift; }}
  | l=expr EQEQ r=expr { Binary { left = l; right = r; op = IsEqual; }}
  | l=expr BANGEQ r=expr { Binary { left = l; right = r; op = NotEqual; }}
  | l=expr LOGIC_AND r=expr { Binary { left = l; right = r; op = LogicAnd; }}
  | l=expr LOGIC_OR r=expr { Binary { left = l; right = r; op = LogicOr; }}
  | l=expr LT r=expr { Binary { left = l; right = r; op = LessThan; }}
  | l=expr LE r=expr { Binary { left = l; right = r; op = LessThanOrEqual; }}
  | l=expr GT r=expr { Binary { left = l; right = r; op = GreaterThan; }}
  | l=expr GE r=expr { Binary { left = l; right = r; op = GreaterThanOrEqual; }}
  | u=unary { u }

unary:
  | BANG i=unary { Unary { inner = i; op = Not; } }
  | MINUS i=unary %prec UMINUS { Unary { inner = i; op = Negate; } }
  | TILDE i=unary { Unary { inner = i; op = Complement; } }
  | REF e=unary { Ref e }
  | LOAD e=unary { Load e }
  | BOX e=unary { BoxExpr e }
  | BOX t=haven_type { BoxType t }
  | UNBOX e=unary { Unbox e }
  | p=postfix { p }
  ;

postfix:
  | p=postfix LPAREN e=expr_seq RPAREN { Call { target = p; params = e } }
  | p=postfix LBRACKET e=expr RBRACKET { Index { target = p; index = e } }
  | p=postfix DOT i=IDENT { Field { target = p; arrow = false; field = i } }
  | p=postfix ARROW i=IDENT { Field { target = p; arrow = true; field = i } }
  | p=primary { p }

expr_seq: exprs=separated_list(COMMA, expr) { exprs } ;

primary:
  | l=literal { Literal l }
  | b=block { Block b }
  | LPAREN e=expr RPAREN { ParenthesizedExpression e }
  | i=init { Initializer i }
  | i=IDENT { Identifier i }
  | i=if_expr { If i }
  | m=match_expr { Match m }
  | AS LT t=haven_type GT LPAREN e=expr RPAREN { As { target_type = t; inner = e } }
  | SIZE LT t=haven_type GT { SizeType t }
  | SIZE LPAREN e=expr RPAREN { SizeExpr e }
  | NIL { Nil }
  ;

init: LBRACE exprs=separated_nonempty_list(COMMA, expr) RBRACE { { exprs } } ;

(** COMPLEX EXPRESSIONS **)

if_expr: IF c=expr t=block e=option(else_expr) { { cond = c; then_block = t; else_block = e } } ;
else_expr:
  | ELSE e=if_expr { ElseIf e }
  | ELSE b=block { Else b }
  ;

match_expr: MATCH e=expr LBRACE a=separated_nonempty_list(COMMA, match_arm) RBRACE { { expr = e; arms = a } } ;
match_arm: p=pattern FATARROW e=expr { { pattern = p; expr = e } } ;

pattern:
  | UNDERSCORE { PatternDefault }
  | l=literal { PatternLiteral l }
  | v=IDENT p=pattern_unwrap { PatternEnum { enum_name = None; enum_variant = v; binding = p } }
  | e=IDENT SCOPE v=IDENT p=pattern_unwrap { PatternEnum { enum_name = Some e; enum_variant = v; binding = p } }
  ;
pattern_unwrap:
  | LPAREN b=separated_nonempty_list(COMMA, pattern_binding) RPAREN { b }
  | { [] }
  ;
pattern_binding:
  | UNDERSCORE { BindingIgnored }
  | i=IDENT { BindingNamed i }
  ;

(** LITERALS **)

literal:
  | n=integer_literal { n }
  | i=noninteger_literal { i }

integer_literal:
  | i=HEX_LIT { HexInt i }
  | i=OCT_LIT { OctInt i }
  | i=BIN_LIT { BinInt i }
  | i=INT_LIT { DecInt i }

noninteger_literal:
  | f=FLOAT_LIT { Float f }
  | s=STRING_LIT { String s }
  | c=CHAR_LIT { Char c }
  | m=mat_literal { Matrix m }
  | v=vec_literal { Vector v }
  | e=enum_literal { Enum e }
  ;

mat_literal: MAT LT rows=separated_nonempty_list(COMMA, vec_literal) GT { { rows } } ;
vec_literal: VEC u=delimited(LT, separated_nonempty_list(COMMA, unary), GT) { u } ;

enum_literal:
  | e=IDENT SCOPE v=IDENT { { enum_name = e; enum_variant = v; types = [] } }
  | e=IDENT SCOPE t=contained_type_list SCOPE v=IDENT { { enum_name = e; enum_variant = v; types = t } }
  ;

contained_type_list: t=delimited(LT, separated_nonempty_list(COMMA, haven_type), GT) { t } ;

(** TYPES **)

haven_type:
  | t=type_primary { t }
  | t=type_primary STAR { PointerType t }
  | t=type_primary CARET { BoxType t }
  | t=type_primary LBRACKET c=integer_literal RBRACKET { ArrayType { element = t; count = c } }
  ;

type_primary:
  | t=builtin_type { t }
  | CELL LT t=haven_type GT { CellType t }
  | FUNCTION LT LPAREN p=separated_list(COMMA, haven_type) RPAREN ARROW r=haven_type GT {
    FunctionType { param_types = p; return_type = r; vararg = false; }
    }
  | VAFUNCTION LT LPAREN p=separated_list(COMMA, haven_type) RPAREN ARROW r=haven_type GT {
    FunctionType { param_types = p; return_type = r; vararg = true; }
    }
  | o=IDENT SCOPE LT i=separated_nonempty_list(COMMA, haven_type) GT { TemplatedType { outer = o; inner = i; } }
  | i=IDENT { CustomType { name = i; } }
  ;

builtin_type:
  | t=NUMERIC_TYPE { NumericType t }
  | t=VEC_TYPE { VecType t }
  | t=MAT_TYPE { MatrixType t }
  | FLOAT_TYPE { FloatType }
  | VOID_TYPE { VoidType }
  | STR_TYPE { StringType }
  ;
