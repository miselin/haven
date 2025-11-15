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
%token PUB FN MUT IF ELSE LET FOR WHILE BREAK CONTINUE MATCH AS ITER
%token LOAD RET STRUCT TYPE NIL DEFER IMPURE ENUM IMPORT CIMPORT SIZE
%token BOX UNBOX INTRINSIC UNTIL FOREIGN DATA STATE VEC MAT FUNCTION
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
%%

program: decls=top_decl+ EOF { { decls } } ;

(** TOP-LEVEL CONSTRUCTS **)

top_decl:
  | d=fn_intrinsic_decl { FDecl d }
  | d=fn_definition { FDecl d }
  | d=fn_forward_decl { FDecl d }
  | import_decl { Import }
  | cimport_decl { CImport }
  | foreign_decl { Foreign }
  | type_decl { TDecl }
  | global_decl { VDecl }
  ;

import_decl: IMPORT STRING_LIT SEMICOLON {} ;
cimport_decl: CIMPORT STRING_LIT SEMICOLON {} ;

foreign_decl: FOREIGN STRING_LIT LBRACE foreign_fn_decl* RBRACE {} ;
foreign_fn_decl: fn_header fn_intrinsic? SEMICOLON {} ;

fn_definition: f=fn_header b=block { { f with definition = Some b } } ;
fn_forward_decl: f=fn_header SEMICOLON { f } ;
fn_intrinsic_decl: f=fn_header fn_intrinsic SEMICOLON { f } ;

fn_header: pub=boption(PUB) impure=boption(IMPURE) FN name=IDENT LPAREN params? RPAREN return_type? {
    { public = pub; impure = impure; name = name; definition = None }
} ;
return_type: ARROW haven_type {} ;

fn_intrinsic: INTRINSIC STRING_LIT intrinsic_type_list {} ;
intrinsic_type_list: separated_list(COMMA, haven_type) {} ;

params:
  | param_list {}
  | STAR {}
  ;
param_list: separated_nonempty_list(COMMA, param) param_list_vararg? {} ;
param_list_vararg: COMMA STAR {} ;

param: haven_type IDENT {} ;

type_decl: TYPE IDENT type_binding? SEMICOLON {} ;
type_binding: EQUAL type_body {} ;
type_body:
  | haven_type {}
  | struct_decl {}
  | enum_decl {}
  ;

struct_decl: STRUCT LBRACE list(terminated(struct_field, SEMICOLON)) RBRACE {} ;
struct_field: haven_type IDENT {} ;

enum_decl: ENUM enum_generics? LBRACE separated_nonempty_list(COMMA, enum_variant) RBRACE {} ;
enum_generics: separated_list(COMMA, IDENT) {} ;
enum_variant: IDENT enum_wrapped_type? {} ;
enum_wrapped_type: LPAREN haven_type RPAREN {} ;

global_decl: PUB? global_decl_inner SEMICOLON {} ;
global_decl_inner:
  | DATA global_decl_binding {}
  | STATE global_decl_binding {}
  ;
global_decl_binding: haven_type IDENT bind_expr? {} ;

bind_expr: EQUAL expr {} ;

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
  ;

unary:
  | BANG i=unary { Unary { inner = i; op = Not; } }
  | MINUS i=unary %prec UMINUS { Unary { inner = i; op = Negate; } }
  | TILDE i=unary { Unary { inner = i; op = Complement; } }
  | REF unary { Ref }
  | LOAD unary { Load }
  | BOX unary { Box }
  | BOX haven_type { Box }
  | UNBOX unary { Unbox }
  | p=primary LPAREN e=expr_seq RPAREN { Call { target = p; params = e } }
  | p=primary LBRACKET e=expr RBRACKET { Index { target = p; index = e } }
  | p=primary DOT i=IDENT { Field { target = p; arrow = false; field = i } }
  | p=primary ARROW i=IDENT { Field { target = p; arrow = true; field = i } }
  | p=primary { p }
  ;

expr_seq: exprs=separated_list(COMMA, expr) { exprs } ;

primary:
  | l=literal { Literal l }
  | b=block { Block b }
  | LPAREN e=expr RPAREN { ParenthesizedExpression e }
  | i=init { Initializer i }
  | i=IDENT { Identifier i }
  | i=if_expr { If i }
  | match_expr { Match }
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

match_expr: MATCH expr LBRACE separated_nonempty_list(COMMA, match_arm) RBRACE {} ;
match_arm: pattern FATARROW expr {} ;

pattern:
  | UNDERSCORE {}
  | literal {}
  | pattern_enum pattern_unwrap? {}
  ;
pattern_enum:
  | IDENT {}
  | IDENT SCOPE IDENT {}
  ;
pattern_unwrap: LPAREN separated_nonempty_list(COMMA, pattern_binding) RPAREN {} ;
pattern_binding:
  | IDENT {}
  | UNDERSCORE {}
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
  | enum_literal { Enum }
  ;

mat_literal: MAT LT rows=separated_nonempty_list(COMMA, vec_literal) GT { { rows } } ;
vec_literal: VEC u=delimited(LT, separated_nonempty_list(COMMA, unary), GT) { u } ;

enum_literal:
  | IDENT SCOPE IDENT {}
  | IDENT SCOPE contained_type_list SCOPE IDENT {}
  ;

contained_type_list: delimited(LT, separated_nonempty_list(COMMA, haven_type), GT) {} ;

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
