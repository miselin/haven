**start:**

![start](diagram/start.svg)

```
start    ::= module
```

**module:**

![module](diagram/module.svg)

```
module   ::= top_decl+
```

referenced by:

* start

**top_decl:**

![top_decl](diagram/top_decl.svg)

```
top_decl ::= import
           | cimport
           | foreign_block
           | fn_decl
           | tydecl
           | global_decl
```

referenced by:

* module

**import:**

![import](diagram/import.svg)

```
import   ::= 'import' STRING ';'
```

referenced by:

* top_decl

**cimport:**

![cimport](diagram/cimport.svg)

```
cimport  ::= 'cimport' STRING ';'
```

referenced by:

* top_decl

**foreign_block:**

![foreign_block](diagram/foreign_block.svg)

```
foreign_block
         ::= 'foreign' STRING '{' foreign_fn_decl* '}'
```

referenced by:

* top_decl

**foreign_fn_decl:**

![foreign_fn_decl](diagram/foreign_fn_decl.svg)

```
foreign_fn_decl
         ::= fn_header fn_intrinsic? ';'
```

referenced by:

* foreign_block

**fn_decl:**

![fn_decl](diagram/fn_decl.svg)

```
fn_decl  ::= fn_definition
           | fn_forward_decl
           | fn_intrinsic_decl
```

referenced by:

* top_decl

**fn_intrinsic_decl:**

![fn_intrinsic_decl](diagram/fn_intrinsic_decl.svg)

```
fn_intrinsic_decl
         ::= fn_header fn_intrinsic ';'
```

referenced by:

* fn_decl

**fn_forward_decl:**

![fn_forward_decl](diagram/fn_forward_decl.svg)

```
fn_forward_decl
         ::= fn_header ';'
```

referenced by:

* fn_decl

**fn_definition:**

![fn_definition](diagram/fn_definition.svg)

```
fn_definition
         ::= fn_header block
```

referenced by:

* fn_decl

**fn_header:**

![fn_header](diagram/fn_header.svg)

```
fn_header
         ::= 'pub'? fn_purity 'fn' IDENT '(' params? ')' return_type?
```

referenced by:

* fn_definition
* fn_forward_decl
* fn_intrinsic_decl
* foreign_fn_decl

**fn_purity:**

![fn_purity](diagram/fn_purity.svg)

```
fn_purity
         ::= 'impure'?
```

referenced by:

* fn_header

**return_type:**

![return_type](diagram/return_type.svg)

```
return_type
         ::= '->' type
```

referenced by:

* fn_header

**fn_intrinsic:**

![fn_intrinsic](diagram/fn_intrinsic.svg)

```
fn_intrinsic
         ::= 'intrinsic' STRING intrinsic_type_list
```

referenced by:

* fn_intrinsic_decl
* foreign_fn_decl

**intrinsic_type_list:**

![intrinsic_type_list](diagram/intrinsic_type_list.svg)

```
intrinsic_type_list
         ::= type ( ',' type )*
```

referenced by:

* fn_intrinsic

**params:**

![params](diagram/params.svg)

```
params   ::= param ( ',' param )* ( ',' '*' )?
           | '*'
```

referenced by:

* fn_header

**param:**

![param](diagram/param.svg)

```
param    ::= type IDENT
```

referenced by:

* params

**tydecl:**

![tydecl](diagram/tydecl.svg)

```
tydecl   ::= 'type' IDENT ( '=' type_body )? ';'
```

referenced by:

* top_decl

**type_body:**

![type_body](diagram/type_body.svg)

```
type_body
         ::= type
           | struct_decl
           | enum_decl
```

referenced by:

* tydecl

**struct_decl:**

![struct_decl](diagram/struct_decl.svg)

```
struct_decl
         ::= 'struct' '{' struct_field+ '}'
```

referenced by:

* type_body

**struct_field:**

![struct_field](diagram/struct_field.svg)

```
struct_field
         ::= type IDENT ';'
```

referenced by:

* struct_decl

**enum_decl:**

![enum_decl](diagram/enum_decl.svg)

```
enum_decl
         ::= 'enum' enum_generics? '{' enum_variant_list? '}'
```

referenced by:

* type_body

**enum_generics:**

![enum_generics](diagram/enum_generics.svg)

```
enum_generics
         ::= '<' IDENT ( ',' IDENT )* '>'
```

referenced by:

* enum_decl

**enum_variant_list:**

![enum_variant_list](diagram/enum_variant_list.svg)

```
enum_variant_list
         ::= enum_variant ( ',' enum_variant )* ','?
```

referenced by:

* enum_decl

**enum_variant:**

![enum_variant](diagram/enum_variant.svg)

```
enum_variant
         ::= IDENT ( '(' type ')' )?
```

referenced by:

* enum_variant_list

**global_decl:**

![global_decl](diagram/global_decl.svg)

```
global_decl
         ::= 'pub'? ( global_data | global_state ) ';'
```

referenced by:

* top_decl

**global_data:**

![global_data](diagram/global_data.svg)

```
global_data
         ::= 'data' global_binding
```

referenced by:

* global_decl

**global_state:**

![global_state](diagram/global_state.svg)

```
global_state
         ::= 'state' global_binding
```

referenced by:

* global_decl

**global_binding:**

![global_binding](diagram/global_binding.svg)

```
global_binding
         ::= type IDENT ( '=' expr )?
```

referenced by:

* global_data
* global_state

**block:**

![block](diagram/block.svg)

```
block    ::= '{' stmt* expr? '}'
```

referenced by:

* fn_definition
* iter_stmt
* primary
* while_stmt

**stmt:**

![stmt](diagram/stmt.svg)

```
stmt     ::= stmt_inner? ';'
```

referenced by:

* block

**stmt_inner:**

![stmt_inner](diagram/stmt_inner.svg)

```
stmt_inner
         ::= let_stmt
           | ret_stmt
           | defer_stmt
           | iter_stmt
           | while_stmt
           | 'break'
           | 'continue'
           | expr_stmt
```

referenced by:

* stmt

**let_stmt:**

![let_stmt](diagram/let_stmt.svg)

```
let_stmt ::= 'let' 'mut'? binding
```

referenced by:

* stmt_inner

**binding:**

![binding](diagram/binding.svg)

```
binding  ::= inferred_binding
           | typed_binding
```

referenced by:

* let_stmt

**inferred_binding:**

![inferred_binding](diagram/inferred_binding.svg)

```
inferred_binding
         ::= IDENT '=' expr
```

referenced by:

* binding

**typed_binding:**

![typed_binding](diagram/typed_binding.svg)

```
typed_binding
         ::= type IDENT '=' expr
```

referenced by:

* binding

**ret_stmt:**

![ret_stmt](diagram/ret_stmt.svg)

```
ret_stmt ::= 'ret' expr?
```

referenced by:

* stmt_inner

**defer_stmt:**

![defer_stmt](diagram/defer_stmt.svg)

```
defer_stmt
         ::= 'defer' expr
```

referenced by:

* stmt_inner

**iter_stmt:**

![iter_stmt](diagram/iter_stmt.svg)

```
iter_stmt
         ::= 'iter' iter_range IDENT block
```

referenced by:

* stmt_inner

**iter_range:**

![iter_range](diagram/iter_range.svg)

```
iter_range
         ::= expr ':' expr ( ':' expr )?
```

referenced by:

* iter_stmt

**while_stmt:**

![while_stmt](diagram/while_stmt.svg)

```
while_stmt
         ::= 'while' expr block
```

referenced by:

* stmt_inner

**expr_stmt:**

![expr_stmt](diagram/expr_stmt.svg)

```
expr_stmt
         ::= expr
```

referenced by:

* stmt_inner

**expr:**

![expr](diagram/expr.svg)

```
expr     ::= logic_or ( assignment_operator logic_or )*
```

referenced by:

* args
* as_expr
* block
* defer_stmt
* expr_stmt
* global_binding
* inferred_binding
* initializer_list
* iter_range
* match_arm
* match_expr
* postfix_part
* primary
* ret_stmt
* size_param
* typed_binding
* while_stmt

**assignment_operator:**

![assignment_operator](diagram/assignment_operator.svg)

```
assignment_operator
         ::= '='
           | ':='
```

referenced by:

* expr

**multiplicative:**

![multiplicative](diagram/multiplicative.svg)

```
multiplicative
         ::= unary ( multiplicative_operator unary )*
```

referenced by:

* additive

**additive:**

![additive](diagram/additive.svg)

```
additive ::= multiplicative ( additive_operator multiplicative )*
```

referenced by:

* shift

**shift:**

![shift](diagram/shift.svg)

```
shift    ::= additive ( shift_operator additive )*
```

referenced by:

* relational

**relational:**

![relational](diagram/relational.svg)

```
relational
         ::= shift ( relational_operator shift )*
```

referenced by:

* equality

**equality:**

![equality](diagram/equality.svg)

```
equality ::= relational ( equality_operator relational )*
```

referenced by:

* bit_and

**bit_and:**

![bit_and](diagram/bit_and.svg)

```
bit_and  ::= equality ( '&' equality )*
```

referenced by:

* bit_xor

**bit_xor:**

![bit_xor](diagram/bit_xor.svg)

```
bit_xor  ::= bit_and ( '^' bit_and )*
```

referenced by:

* bit_or

**bit_or:**

![bit_or](diagram/bit_or.svg)

```
bit_or   ::= bit_xor ( '|' bit_xor )*
```

referenced by:

* logic_and

**logic_and:**

![logic_and](diagram/logic_and.svg)

```
logic_and
         ::= bit_or ( '&&' bit_or )*
```

referenced by:

* logic_or

**logic_or:**

![logic_or](diagram/logic_or.svg)

```
logic_or ::= logic_and ( '||' logic_and )*
```

referenced by:

* expr

**multiplicative_operator:**

![multiplicative_operator](diagram/multiplicative_operator.svg)

```
multiplicative_operator
         ::= '*'
           | '/'
           | '%'
```

referenced by:

* multiplicative

**additive_operator:**

![additive_operator](diagram/additive_operator.svg)

```
additive_operator
         ::= '+'
           | '-'
```

referenced by:

* additive

**shift_operator:**

![shift_operator](diagram/shift_operator.svg)

```
shift_operator
         ::= '<<'
           | '>>'
```

referenced by:

* shift

**relational_operator:**

![relational_operator](diagram/relational_operator.svg)

```
relational_operator
         ::= '<'
           | '<='
           | '>'
           | '>='
```

referenced by:

* relational

**equality_operator:**

![equality_operator](diagram/equality_operator.svg)

```
equality_operator
         ::= '=='
           | '!='
```

referenced by:

* equality

**unary:**

![unary](diagram/unary.svg)

```
unary    ::= unary_prefix* primary postfix_part*
```

referenced by:

* multiplicative
* vec_literal
* vec_literal_tail

**unary_prefix:**

![unary_prefix](diagram/unary_prefix.svg)

```
unary_prefix
         ::= '!'
           | '-'
           | '~'
           | 'load'
           | 'unbox'
           | 'ref'
           | 'box'
```

referenced by:

* unary

**postfix_part:**

![postfix_part](diagram/postfix_part.svg)

```
postfix_part
         ::= '(' args? ')'
           | '[' expr ']'
           | ( '.' | '->' ) IDENT
```

referenced by:

* unary

**args:**

![args](diagram/args.svg)

```
args     ::= expr ( ',' expr )*
```

referenced by:

* postfix_part

**primary:**

![primary](diagram/primary.svg)

```
primary  ::= literal
           | enum_variant_literal
           | ( 'if' expr ( block 'else' 'if' expr )* ( block 'else' )? )? block
           | '(' expr ')'
           | initializer
           | match_expr
           | as_expr
           | size_expr
           | 'nil'
           | IDENT
```

referenced by:

* unary

**as_expr:**

![as_expr](diagram/as_expr.svg)

```
as_expr  ::= 'as' '<' type '>' '(' expr ')'
```

referenced by:

* primary

**size_expr:**

![size_expr](diagram/size_expr.svg)

```
size_expr
         ::= 'size' size_param
```

referenced by:

* primary

**size_param:**

![size_param](diagram/size_param.svg)

```
size_param
         ::= '<' type '>'
           | '(' expr ')'
```

referenced by:

* size_expr

**literal:**

![literal](diagram/literal.svg)

```
literal  ::= HEX
           | OCT
           | BIN
           | INT
           | FLOAT
           | STRING
           | CHAR
           | mat_literal
           | vec_literal
```

referenced by:

* pattern
* primary

**mat_literal:**

![mat_literal](diagram/mat_literal.svg)

```
mat_literal
         ::= 'Mat' '<' mat_inner '>'
```

referenced by:

* literal

**mat_inner:**

![mat_inner](diagram/mat_inner.svg)

```
mat_inner
         ::= mat_row+
```

referenced by:

* mat_literal

**mat_row:**

![mat_row](diagram/mat_row.svg)

```
mat_row  ::= vec_literal ','
```

referenced by:

* mat_inner

**vec_literal:**

![vec_literal](diagram/vec_literal.svg)

```
vec_literal
         ::= 'Vec' '<' unary vec_literal_tail? '>'
```

referenced by:

* literal
* mat_row

**vec_literal_tail:**

![vec_literal_tail](diagram/vec_literal_tail.svg)

```
vec_literal_tail
         ::= ( ',' unary )+
```

referenced by:

* vec_literal

**initializer:**

![initializer](diagram/initializer.svg)

```
initializer
         ::= '{' initializer_list '}'
```

referenced by:

* primary

**initializer_list:**

![initializer_list](diagram/initializer_list.svg)

```
initializer_list
         ::= expr ( ',' | ( ',' expr )+ ','? )
```

referenced by:

* initializer

**match_expr:**

![match_expr](diagram/match_expr.svg)

```
match_expr
         ::= 'match' expr '{' match_arm+ '}'
```

referenced by:

* primary

**match_arm:**

![match_arm](diagram/match_arm.svg)

```
match_arm
         ::= pattern '=>' expr
```

referenced by:

* match_expr

**pattern:**

![pattern](diagram/pattern.svg)

```
pattern  ::= '_'
           | literal
           | pattern_enum pattern_payload?
```

referenced by:

* match_arm

**pattern_enum:**

![pattern_enum](diagram/pattern_enum.svg)

```
pattern_enum
         ::= IDENT ( '::' IDENT )?
```

referenced by:

* pattern

**pattern_payload:**

![pattern_payload](diagram/pattern_payload.svg)

```
pattern_payload
         ::= '(' pattern_binding ( ',' pattern_binding )* ')'
```

referenced by:

* pattern

**pattern_binding:**

![pattern_binding](diagram/pattern_binding.svg)

```
pattern_binding
         ::= IDENT
           | '_'
```

referenced by:

* pattern_payload

**enum_variant_literal:**

![enum_variant_literal](diagram/enum_variant_literal.svg)

```
enum_variant_literal
         ::= IDENT '::' enum_variant_literal_typed? IDENT
```

referenced by:

* primary

**enum_variant_literal_typed:**

![enum_variant_literal_typed](diagram/enum_variant_literal_typed.svg)

```
enum_variant_literal_typed
         ::= type_arguments '::'
```

referenced by:

* enum_variant_literal

**type_path:**

![type_path](diagram/type_path.svg)

```
type_path
         ::= IDENT '::' type_arguments
```

referenced by:

* type_primary

**type_arguments:**

![type_arguments](diagram/type_arguments.svg)

```
type_arguments
         ::= '<' type ( ',' type )* '>'
```

referenced by:

* enum_variant_literal_typed
* type_path

**type:**

![type](diagram/type.svg)

```
type     ::= type_primary type_suffix*
```

referenced by:

* as_expr
* enum_variant
* function_type_params
* global_binding
* intrinsic_type_list
* param
* return_type
* size_param
* struct_field
* type_arguments
* type_body
* type_cell
* type_function_meta
* typed_binding

**type_primary:**

![type_primary](diagram/type_primary.svg)

```
type_primary
         ::= builtin_type
           | type_path
           | type_cell
           | type_function
           | type_vararg_function
           | IDENT
```

referenced by:

* type

**builtin_type:**

![builtin_type](diagram/builtin_type.svg)

```
builtin_type
         ::= NUMERIC_TYPE
           | VEC_TYPE
           | MAT_TYPE
           | 'float'
           | 'void'
           | 'str'
```

referenced by:

* type_primary

**type_cell:**

![type_cell](diagram/type_cell.svg)

```
type_cell
         ::= 'Cell' '<' type '>'
```

referenced by:

* type_primary

**type_function:**

![type_function](diagram/type_function.svg)

```
type_function
         ::= 'Function' type_function_meta
```

referenced by:

* type_primary

**type_vararg_function:**

![type_vararg_function](diagram/type_vararg_function.svg)

```
type_vararg_function
         ::= 'VAFunction' type_function_meta
```

referenced by:

* type_primary

**type_function_meta:**

![type_function_meta](diagram/type_function_meta.svg)

```
type_function_meta
         ::= '<' '(' ( function_type_params | '_' ) ')' '->' type '>'
```

referenced by:

* type_function
* type_vararg_function

**function_type_params:**

![function_type_params](diagram/function_type_params.svg)

```
function_type_params
         ::= type ( ',' type )*
```

referenced by:

* type_function_meta

**type_suffix:**

![type_suffix](diagram/type_suffix.svg)

```
type_suffix
         ::= '*'
           | '^'
           | array_suffix
```

referenced by:

* type

**array_suffix:**

![array_suffix](diagram/array_suffix.svg)

```
array_suffix
         ::= '[' INT ']'
```

referenced by:

* type_suffix

**NUMERIC_TYPE:**

![NUMERIC_TYPE](diagram/NUMERIC_TYPE.svg)

```
NUMERIC_TYPE
         ::=
           | [iu] [1-9] [0-9]*
           |
```

referenced by:

* builtin_type

**VEC_TYPE:**

![VEC_TYPE](diagram/VEC_TYPE.svg)

```
VEC_TYPE ::=
           | fvec [1-9] [0-9]*
           |
```

referenced by:

* builtin_type

**MAT_TYPE:**

![MAT_TYPE](diagram/MAT_TYPE.svg)

```
MAT_TYPE ::=
           | f? mat [1-9] [0-9]* x [1-9] [0-9]*
           |
```

referenced by:

* builtin_type

**FLOAT:**

![FLOAT](diagram/FLOAT.svg)

```
FLOAT    ::= ( '0' | NONZERO_DIGIT DIGIT* ) '.' DIGIT+
```

referenced by:

* literal

**INT:**

![INT](diagram/INT.svg)

```
INT      ::= '0'
           | NONZERO_DIGIT DIGIT*
```

referenced by:

* array_suffix
* literal

**HEX:**

![HEX](diagram/HEX.svg)

```
HEX      ::= '0' 'x' HEX_DIGIT+
```

referenced by:

* literal

**OCT:**

![OCT](diagram/OCT.svg)

```
OCT      ::= '0' 'o' OCT_DIGIT+
```

referenced by:

* literal

**BIN:**

![BIN](diagram/BIN.svg)

```
BIN      ::= '0' 'b' BIN_DIGIT+
```

referenced by:

* literal

**STRING:**

![STRING](diagram/STRING.svg)

```
STRING   ::= '"' string_char* '"'
```

referenced by:

* cimport
* fn_intrinsic
* foreign_block
* import
* literal

**string_char:**

![string_char](diagram/string_char.svg)

```
string_char
         ::= string_escape
           | string_plain
```

referenced by:

* STRING

**string_plain:**

![string_plain](diagram/string_plain.svg)

```
string_plain
         ::=
           | [^"\]
           |
```

referenced by:

* string_char

**string_escape:**

![string_escape](diagram/string_escape.svg)

```
string_escape
         ::= '\\' escaped_char
```

referenced by:

* string_char

**escaped_char:**

![escaped_char](diagram/escaped_char.svg)

```
escaped_char
         ::=
           | .
           |
```

referenced by:

* char_escape
* string_escape

**CHAR:**

![CHAR](diagram/CHAR.svg)

```
CHAR     ::= "'" char_char "'"
```

referenced by:

* literal

**char_char:**

![char_char](diagram/char_char.svg)

```
char_char
         ::= char_escape
           | char_plain
```

referenced by:

* CHAR

**char_plain:**

![char_plain](diagram/char_plain.svg)

```
char_plain
         ::=
           | [^'\]
           |
```

referenced by:

* char_char

**char_escape:**

![char_escape](diagram/char_escape.svg)

```
char_escape
         ::= '\\' escaped_char
```

referenced by:

* char_char

**IDENT:**

![IDENT](diagram/IDENT.svg)

```
IDENT    ::= IDENT_START IDENT_CONT* ( '-' IDENT_CONT+ )*
```

referenced by:

* enum_generics
* enum_variant
* enum_variant_literal
* fn_header
* global_binding
* inferred_binding
* iter_stmt
* param
* pattern_binding
* pattern_enum
* postfix_part
* primary
* struct_field
* tydecl
* type_path
* type_primary
* typed_binding

**IDENT_START:**

![IDENT_START](diagram/IDENT_START.svg)

```
IDENT_START
         ::=
           | [A-Za-z_]
           |
```

referenced by:

* IDENT

**IDENT_CONT:**

![IDENT_CONT](diagram/IDENT_CONT.svg)

```
IDENT_CONT
         ::=
           | [A-Za-z0-9_]
           |
```

referenced by:

* IDENT

**DIGIT:**

![DIGIT](diagram/DIGIT.svg)

```
DIGIT    ::=
           | [0-9]
           |
```

referenced by:

* FLOAT
* INT

**NONZERO_DIGIT:**

![NONZERO_DIGIT](diagram/NONZERO_DIGIT.svg)

```
NONZERO_DIGIT
         ::=
           | [1-9]
           |
```

referenced by:

* FLOAT
* INT

**HEX_DIGIT:**

![HEX_DIGIT](diagram/HEX_DIGIT.svg)

```
HEX_DIGIT
         ::=
           | [0-9a-fA-F]
           |
```

referenced by:

* HEX

**OCT_DIGIT:**

![OCT_DIGIT](diagram/OCT_DIGIT.svg)

```
OCT_DIGIT
         ::=
           | [0-7]
           |
```

referenced by:

* OCT

**BIN_DIGIT:**

![BIN_DIGIT](diagram/BIN_DIGIT.svg)

```
BIN_DIGIT
         ::=
           | [0-1]
           |
```

referenced by:

* BIN

**WS:**

![WS](diagram/WS.svg)

```
WS       ::= [#x20#x09#x0A#x0D]+
```

**COMMENT:**

![COMMENT](diagram/COMMENT.svg)

```
COMMENT  ::= '//' comment_char* line_break?
```

**comment_char:**

![comment_char](diagram/comment_char.svg)

```
comment_char
         ::=
           | [#x00-#x09#x0B-#x0C#x0E-#x10FFFF]
           |
```

referenced by:

* COMMENT

**ML_COMMENT:**

![ML_COMMENT](diagram/ML_COMMENT.svg)

```
ML_COMMENT
         ::= '/*' ml_comment_char* '*/'
```

**ml_comment_char:**

![ml_comment_char](diagram/ml_comment_char.svg)

```
ml_comment_char
         ::=
           | [#x00-#x10FFFF]
           |
```

referenced by:

* ML_COMMENT

**line_break:**

![line_break](diagram/line_break.svg)

```
line_break
         ::= #x0D #x0A?
           | #x0A
```

referenced by:

* COMMENT

## 
![rr-2.2](diagram/rr-2.2.svg) <sup>generated by [RR - Railroad Diagram Generator][RR]</sup>

[RR]: https://www.bottlecaps.de/rr/ui