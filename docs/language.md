# Haven Programmer Reference

This reference describes the maintained OCaml compiler in `src/`. The parser and compiler tests are the authority for accepted syntax; the legacy C and experimental self-hosted compilers are separate implementations.

Recent aggregate and function forms: [expression bodies](#function-declarations), [value and range iteration](#iter), [contextual fill](#fill-expression), [shape-preserving map](#map-expression), [fold](#fold-expression), and [shape specialization](#specialized-vector-functions). Native working examples live in `examples/cube`, `examples/camera`, and `examples/classifier`. The HTML viewers package native outputs; they do not execute Haven or train a model in the browser.

## Key Characteristics

### Interopability

Haven is designed from the outset for interopability with C and other low-level languages.

### Default Const

In Haven, mutability is opt-in, not opt-out. Variables that you expect to modify must be annotated as mutable.

### Default Pure

All functions are assumed pure unless explicitly annotated as `impure`. In practice, purity tracks observable side
effects such as impure calls and mutating or loading through references, cells, or boxes. Pure functions may still
perform ordinary local computation and local reassignment.

## Identifiers

In Haven, identifiers:

- Must start with either an `_` or a letter
- Must end with a digit, letter, or `_`
- Must only contain digits, letters, `_`, or `-`

Hyphens (`-`) may be used only within an identifier:

```
-istrue // invalid, cannot start with hyphen
istrue- // invalid, cannot end with hyphen
is-true // valid
```

Note that the `-` operator for arithmetic requires spaces around it when used with two identifiers:

```
abc-def // identifier abc-def
abc - def // subtract the value of def from abc
```

## Types

### Integers

To type a variable as a signed integer N bits wide, use `iN`:

- `i32` defines a 32-bit signed integer
- `i8` defines an 8-bit signed integer

For an unsigned integer, use a `u` prefix instead of `i`.

### Floats

Use the type `float` for floating-point numbers.

### Vectors

Haven offers a `fvecN` type defining a vector of floating point numbers.

Vectors can be used with binary expressions and optimize to parallel arithmetic where available on the target machine.

For example, the following function returns a new vector with the result of element-wise addition of the two input vectors.

```
pub fn vector_add(fvec3 a, fvec3 b) -> fvec3 {
    a + b
}
```

> [!TIP]
> When integrating Haven with C, `fvecN` is the equivalent of ([non-standard](https://gcc.gnu.org/onlinedocs/gcc/Vector-Extensions.html)) `typedef float floatN __attribute__((vector_size(sizeof(float)) * N))`.

#### Specialized Vector Functions

Functions may accept vectors of any concrete dimension using `fvec?`.

These are not runtime-sized vectors. Instead, the compiler specializes the function at each call site using the
concrete argument types from that call.

```
fn vadd(fvec? a, fvec? b) {
    @assert a.dim == b.dim, "vector dimensions must match";
    a + b
}
```

Inside such a function:

- `a.dim` is a compile-time property of the specialized vector type
- omitted return types are inferred after specialization
- `@assert` conditions must reduce to compile-time constants after specialization

If a compile-time assertion fails, the compiler reports both the original condition and the specialized one:

```
semantic: error: vector dimensions must match
  compile-time assertion failed: a.dim == b.dim
  specialized as: 3 == 2
```

### Matrices

Haven offers `matMxN` matrix types of floating point numbers.

`Mat<Vec<...>, Vec<...>>` supplies rows, and `m[row][column]` selects an
element. Vectors multiply matrices on the left: `v * m` treats `v` as a row
vector. Transform composition therefore applies from left to right; for
homogeneous transforms, translation belongs in the last row. Matrix products
preserve this row-major layout.

Like vectors, matrices support specialization holes in function signatures through `mat?`.

```
fn mat-width(mat? m) {
    m.cols
}

fn get-mat-row(mat? m, u32 row) {
    m[row]
}
```

Inside specialized matrix functions:

- `m.rows` and `m.cols` are compile-time properties
- indexing a concrete `matMxN` yields an `fvecN`
- specialized functions are cloned before LLVM lowering, so hole types do not reach IR

### Strings

The current `str` representation is a pointer to bytes, equivalent to a C `const char *` in imported interfaces. String literals are emitted with a trailing NUL in static module storage. A copied `str` copies that pointer, not the bytes; the type carries no length, capacity, encoding validation or ownership metadata. It is not a growable native string. A pointer supplied from elsewhere must satisfy the receiving C function’s storage and terminator requirements.

### Type Aliasing

You may define your own aliases for types:

```
type int = i32;
```

These aliases are fully erased during compilation and are not accessible at runtime.

### Structures

Defining a structured type looks similar to defining a type alias:

```
type Point = struct {
    i32 x;
    i32 y;
    i32 z;
};
```

Structures may contain pointers to their own type:

```
type Node = struct {
    i32 value;
    Node *next;
};
```

Initializing a structure in a variable declaration requires an explicit type annotation:

```
let Node node = { 1234, nil };
```

In contexts where the type is known (e.g. a function return), the type will be inferred automatically.

Single-element structs do not require a trailing comma:

```
let Thing thing = { 1234 };
```

### Enums

You may define an enum type using two forms.

The first form simply defines a set of names:

```
type Number = enum {
    One,
    Two
};
```

The second form allows for creating union types with bindings:

```
type Numeric = enum {
    Int(i32),
    Float(float)
};
```

Use of enums in expressions requires both the enum name and the field name to be provided:

```
match x {
    Numeric::Int(_) => 0,
    Numeric::Float(_) => 1
}
```

#### Generic Enums

There is _limited_ support for templating enum types in Haven:

```
type Result = enum <T> {
    Ok(T),
    Error
};

fn thing() -> Result::<i32> {
    Result::<i32>::Ok(5)
}
```

### Arrays

`T[N]` is a fixed-size inline aggregate containing exactly `N` elements of `T`. The count is part of the type, not a runtime length or capacity descriptor. Whole-array assignment, parameter passing and return copy the aggregate value. Pointer or box elements keep their ordinary shallow value and ownership semantics; copying an array does not copy pointed-to storage.

```haven
fn pair() -> i32[2] = { 10, 20 };
fn first(i32[2] values) -> i32 = values[0];

fn sample() -> i32 {
    let i32[2] values = pair();
    let mut i32[1] single = zero;
    single[0] = first(values);
    let i32[0] empty = zero;
    single[0]
}
```

A multi-element brace initializer must supply exactly the declared number of elements. A lone `{ expression }` is a value block, and a trailing comma is not accepted by the maintained parser. For one-element or empty arrays, use a typed `zero` and explicit indexed assignment as needed.

Indexing uses the existing array/pointer operations; this compiler does not provide automatic bounds checks or a borrow/lifetime checker. `ref values` points to the whole fixed array (`T[N]*`), while `ref values[0]` points to one element (`T*`). A pointer copied from a local aggregate does not extend its storage lifetime. No growable native array, borrowed slice or dictionary type is implied by these forms.

### Zero Initialization

`zero` zero-initializes aggregates such as arrays, structures, vectors, and
matrices. `nil` is used in pointer-like contexts.

```
type Buffer = struct {
    i8* ptr;
    u64 length;
};

let Buffer buffer = zero;
let mut u32[16] words = zero;
pub state Buffer[4] buffers = zero;
```

Numeric elements and fields become `0` or `0.0`, pointer-like fields become
`nil`, and nested aggregates are zero-initialized recursively. `zero` is
contextually typed, so the surrounding declaration or expression must provide
the complete aggregate type. For example, `let value = zero;` is invalid.

`zero` is not a partial aggregate initializer. A brace initializer must still
provide every required element or field. Scalar and enum targets are rejected;
use an ordinary numeric literal or `nil` when initializing those values.

### Boxed Types

> [!CAUTION]
> Boxed types are very much under construction. Their definition may yet change, and they tend to
> have rough edges that lead to bugs at runtime in their current form.

Boxing wraps a value in a heap-allocated structure. The underlying value
can be retrieved with the `unbox` keyword.

```
fn example() -> i32 {
    let mut val = box 5; // i4^
    let result = unbox val; // i4
    val = nil; // box is freed
    result
}
```

`box` also supports type-directed construction:

```
type Buffer = struct {
    i32 len;
    i32 cap;
};

extend Buffer with {
    construct(i32 cap) {
        self->len = 0;
        self->cap = cap;
    }
}

fn example() -> i32 {
    let mut boxed = box Buffer(64);
    let value = unbox boxed;
    boxed = nil;
    value.cap
}
```

`box T` allocates storage for `T`, recursively default-initializes its members, and then runs a zero-argument
constructor if `T` defines one. `box T(args...)` performs the same recursive default initialization, then calls
`construct` with the supplied arguments. This means member pointers are `nil` and inline subobjects are already in a
known state before `construct` runs.

Box types are written much like pointers, but using a caret (`^`) instead
of an asterisk (`*`):

```
fn example(i32^ boxed) -> i32;
```

To directly mutate the value of a box, use the `:=` mutation
operator:

```
let val = box 5;
val := 6;
let result = unbox val; // 6
```

Note that `val` does not need to be mutable in this case. `let mut` permits reassignment of `val` but does not
control the mutability of the stored value. In the example above, `:=` would be very similar to `*val = 6` in C.

## Declarations

### Visibility

Top-level declarations are file-visible by default. Use `pub(module)` for declarations shared by files in the same module, and `pub` for declarations visible outside the module.

```haven
fn file_helper() -> i32 { 1 }
pub(module) fn module_helper() -> i32 { 2 }
pub fn public_helper() -> i32 { 3 }
```

Visibility blocks are shorthand for applying the same visibility to each declaration in the block:

```haven
pub(module) {
    fn first_helper() -> i32 { 1 }
    fn second_helper() -> i32 { 2 }
}
```

An explicit declaration modifier overrides the surrounding block. Visibility blocks do not introduce a lexical scope.

### Import Declarations

#### Haven Imports

Import declarations may only appear at the file scope. An import loads the contents of the imported file, allowing definitions from that file to be used locally.

```
import "vec.hv";
```

#### C Imports

A C import declaration parses a C header file and retains declarations for the purpose of C interopability.

The maintained compiler uses Clang to read the requested header. Use `-I` for include paths and `-isysroot` when a platform SDK is required. Imported C calls retain their foreign/impure behavior; no `--bootstrap` flag is required.

```
cimport "stdio.h";
```

#### Foreign Interfaces

When introducing dependencies on external libraries, you may opt to use the `--Xl -lm` style of command line
flag to present the correct libraries for linking.

LLVM math intrinsics such as `llvm.exp`, `llvm.sin` and `llvm.cos` may lower to system math functions. On Linux, link `libm` explicitly with `--Xl -lm`; the maintained demo runners do so.

Alternatively, Haven offers the `foreign` declaration to simplify this end-to-end:

```
foreign "m" {
    fn fsqrtf(float x) -> float;
}

foreign "c" {
    fn printf(str format, *) -> i32;
}
```

A module with these `foreign` declarations will automatically add `-lm -lc` to the command line. The function
declarations will also be automatically marked `pub` and `impure`, simplifying the declarations for import.

### Type Declarations

Type declarations (`type X = ...`) may only appear at the file scope.

### Type Extensions

Type extensions attach behavior to an existing type without changing its layout.

```
extend Buffer with {
    construct(i32 cap) {
        self->len = 0;
        self->cap = cap;
    }

    destruct {
        self->len = 0;
    }
}
```

In the current model, `extend` is behavioral only:

- `construct(...) { ... }` defines an optional constructor hook.
- `destruct { ... }` defines an optional destructor hook.
- `self` is provided implicitly inside both hooks and is pointer-like, so fields are accessed with `self->field`.

Constructors run automatically after recursive default-initialization. Destructors run automatically when the final
boxed reference is released. `extend` does not add fields or change ABI layout.

### Variable Declarations

#### File Scope

File scope variables are split into two categories:

- `data`, for constant, immutable data used by the program without modification, and
- `state`, for mutable program state that may be initialized either from a constant expression or from startup code

```
data i32 x = 1234; // constant, local
pub data i32 y = 5678; // constant, with global linkage (visible outside the translation unit)
```

```
state i32 x = 1234; // mutable, local
pub state i32 y = 5678; // mutable, global linkage
```

For `pub` data and state, an initializer may be omitted to create a reference to be resolved by the linker.
Non-constant global initializers are lowered to program startup initialization before user code runs.

An explicit `zero` initializer creates a definition rather than an external
reference. For example:

```
pub state i32[4] supplied_elsewhere;    // external declaration
pub state i32[4] owned_here = zero;     // zero-filled definition
```

#### Function Scope

Inside function definitions, variable declarations take a different form:

```
let [mut] [<ty>] <ident> = <init-expr>;
```

A type need not be specified. If unspecified, the type of the variable will be inferred from the initialization expression. Specifying `mut` will allow reassignment of the variable.

Variables at function scope must be initialized.

### Function Declarations

Functions can be forward-declared without a body.

```
[visibility] [impure] fn <ident>(<arg-list>) -> <ret-ty>;
[visibility] [impure] fn <ident>(<arg-list>) -> <ret-ty> { <body> }
[visibility] [impure] fn <ident>(<arg-list>) -> <ret-ty> = <expression>;
```

A function with one result expression can use an expression body:

```haven
fn scale(fvec3 v, float factor) -> fvec3 = v * factor;
fn absolute(float x) -> float = if x < 0.0 { -x } else { x };
fn generic_scale(fvec? v, float factor) = v * factor;
```

`fn … = expression;` lowers to the same function body as `fn … { expression }`. The semicolon ends the declaration; it does not discard the expression's result. Existing return-type inference remains limited to shape-specialized functions. Existing visibility, purity, return checks and function-pointer behavior also apply. As with a trailing result in a braced body, an expression body supplies a return value; `void` functions continue to use braced statement bodies.

Evaluation is **strict and eager**. Calling an expression-bodied function evaluates its arguments once, from left to right, even if a parameter is unused, then evaluates the body. `if`, `&&` and `||` retain their existing conditional/short-circuit evaluation. A declaration does not run its body. An expression body introduces no lazy value, thunk, memoization or implicit closure; this is syntax for an ordinary function call.


The LLVM backend adds advisory `inlinehint` to defined functions whose normalized body has no statements and one result expression, for either spelling. Statement-bearing nested blocks are not eligible. Generic specializations follow the same rule; declaration-only imports do not receive this hint. The optimizer can decline it: the attribute is not `alwaysinline`, does not change argument/effect evaluation, and does not promise removal of a private symbol when its address or linkage is required. See [LLVM 18's attribute definition](https://releases.llvm.org/18.1.8/docs/LangRef.html#function-attributes).

Specifying external `pub` on declarations that have no definitions will create an external reference to the function. A `pub(module)` declaration without a definition remains module-visible and is not exported at linker scope.

Specifying `impure` on declarations will mark the function as impure, which means it is allowed to read and write memory.

An argument list can be ended with `*` to indicate that the function accepts a variable number of arguments:

```
pub fn printf(str format, *) -> i32;
```

> [!WARNING]
> Pure functions cannot call impure functions.

The following example shows usage of both a declaration and a defined function:

```
pub fn printf(str fmt, *) -> i32;

pub fn main() -> i32 {
    printf("Hello, world!\n");
    0
}
```

## Blocks

Blocks contain statements and expressions. Every function definition has at least one block. Defining a block creates a new scope: variables defined before a block begins are visible, but variables defined _inside_ the block are not visible outside the block.

If the final statement in a block is an expression, the result of that expression is used as the result value of the block. In functions, this result value becomes the return value of the function.

Blocks are themselves expressions, and can appear anywhere that an expression is expected:

```
let x = {
    5 + 5
};
```

Note that the addition in this example is not terminated with a semicolon. Terminating with a semicolon would convert the block's result to be `void`, thereby making it an invalid initializer.

## Statements

### Expression

Any expression is also a valid statement. The last expression in a block must not be terminated with a semicolon.

### Void

An empty statement is also called a "void" statement. It has no effect and is omitted in code generation.

### let

The `let` statement defines new variables in the current scope:

```
let test = 5;
let mut mutable = 6;
let i32 typed = 7;
```

### iter

The `iter` statement supports inclusive ranges and value iteration over vectors, row-major matrices and fixed arrays.

```haven
iter each i of 0:10 {
    printf("%d\n", i);
};
```

Ranges are inclusive; the above range will visit values `0` and `10` during iteration. They retain the existing mutable `i32` counter and evaluate the end expression, step expression (or default `1`), and start expression once, in that order, before testing the first iteration. End and step values are cached. A zero step keeps its existing behavior; this syntax adds no generator or range object.

A constant step can be provided:

```haven
iter each i of 10:0:-1 {};
```

Value iteration removes explicit shape arithmetic:

```haven
let mut sum = 0.0;
iter each value of vector { sum = sum + value; };

// A matrix yields whole row-vector values, in row order.
iter each row of matrix indexed by r {
    iter each value of row indexed by c { transposed[c][r] = value; };
};
```

The canonical forms are `iter each <value-name> of <source-expression> { <body> };` and `iter each <value-name> of <source-expression> indexed by <index-name> { <body> };`. The index clause is optional for aggregate value iteration; the numeric-range form is `iter each <counter-name> of start:end[:step] { … };`. A range already binds its numeric counter, so it does not introduce a second ordinal binding.

`indexed by` is a contextual clause: `indexed` and `by` remain ordinary identifiers outside this position. There is no comma shorthand in the sentence form. The old `iter source value[, index] { … };` and `iter start:end[:step] counter { … };` forms remain temporarily accepted for compatibility, but the formatter emits only the canonical sentence form. No removal date is set.

- The source expression runs exactly once before the loop. Its result is stored as an ordinary Haven aggregate value, forming a snapshot for this traversal.
- Vectors yield float components at indices `0` through `dim - 1`. Matrices yield row-vector values at indices `0` through `rows - 1`; a row has the matrix's column width. Fixed arrays yield element values at indices `0` through `count - 1`. Nested row iteration therefore visits cells in row-major order.
- The value binding and optional zero-based `u32` ordinal are immutable, scoped to the body and rebound each iteration. Neither is an implicit reference. Use `let mut copy = value` to change a local copy, or write an explicitly indexed mutable result. Reassigning elements of the original source does not change the values subsequently read from the snapshot.
- Copying follows ordinary Haven value semantics; it does not deep-copy pointees or objects reachable through pointer/box elements. Existing purity and ownership rules continue to govern explicit references and loads.
- Shape specialization resolves generic `fvec?` and `mat?` cardinalities and row types before LLVM lowering. A zero-length fixed array evaluates its source once and executes no body; a strict `index < count` bound avoids unsigned underflow. Empty vector/matrix literal syntax is not introduced.
- `break`, `continue` and `ret` use existing loop semantics. `continue` advances to the next value; `break` leaves the current loop.
- Scalars, strings, structs, pointers and boxes are not implicit iterable containers. Explicitly load a pointer to an aggregate when needed. The copied source can have a cost for large arrays; existing range loops let code address such arrays directly.

This is value iteration over existing fixed aggregates, not a generator, new collection protocol or a reference-iteration/ownership model.

### fill expression

`fill <scalar>` constructs a vector or matrix of the expected type, evaluating the numeric scalar **once** and replicating its float value across every cell. For example:

```haven
let fvec3 ones = fill 1.0;
let mat2x3 weights = fill (scale + 1.0);
fn repeat(float x) -> fvec3 = fill x;
```

An explicit binding type, assignment target, function return type or typed argument can supply the context, as with `zero`. `let v = fill 1.0` has insufficient context. This version supports vectors and matrices, including concrete shapes obtained by specialization; it does not fill structs or fixed arrays. `fill` is a reserved prefix operator with unary precedence: parenthesize an arithmetic operand. In a generic body, a vector/matrix context may carry an existing `?` shape hole; specialization must resolve it before code generation. Integer scalars use the ordinary float conversion.

### map expression

```haven
fn ones(fvec? v) = map each value of v { 1.0 };
fn offset(fvec? v) = map each value of v indexed by i { value + as<float>(i) };
fn shift(mat? m) = map each row of m { map each value of row { value + 1.0 } };
```

`map each <value-name> of <source-expression> [indexed by <index-name>] { <body-result> }` eagerly constructs a new aggregate. `map` is reserved; `indexed by` retains the contextual clause used by `iter`.

- The source is evaluated once and captured before the first body evaluation. Elements are visited sequentially in ascending order. Vectors yield float cells, matrices yield row vectors, and fixed arrays yield their existing element type. Empty arrays still evaluate the source once and execute no body evaluations.
- The result preserves **both source shape and element type**. Each body result must match the source element under existing assignment compatibility, including ordinary numeric conversion. A float result in an integer array remains an integer element after conversion; the array is not widened. A matrix body must return a row of the same width; use nested maps for scalar-cell transforms. This is not type-changing map, broadcasting or a collection protocol.
- Values and optional zero-based `u32` indices are immutable and scoped to the body. The result is a distinct aggregate value; the map performs no implicit write to its source. Explicit body effects follow ordinary Haven rules. Changes to the original aggregate do not alter future reads from the snapshot. Pointer/box elements keep existing shallow value semantics.
- Every body must supply a result, even for an empty source. `break`, `continue` and `ret` are rejected throughout the body, including nested loops, because each visited element must supply one output. Called functions retain ordinary return behavior. `defer` retains its existing function-exit semantics.
- Lowering uses existing Core value copies, indexed assignment and bounded loops. There are no lambdas, implicit in-place updates, parallel execution or allocator changes. Optimizers may eliminate copies or unroll loops; the language makes no blanket allocation or in-place guarantee.

### fold expression

`fold` combines the values of a vector, row-major matrix or fixed array using exactly one accumulator binding:

```haven
fn sum(fvec? v) -> float =
    fold each value of v with acc = 0.0 { acc + value };

// A matrix yields row vectors; folds can be nested to visit its cells.
fn total(mat? matrix) -> float =
    fold each row of matrix with acc = 0.0 {
        acc + (fold each value of row with subtotal = 0.0 { subtotal + value })
    };

let i32 total = fold each value of integers with i32 acc = 0 { acc + value };
```

The form is `fold each <value-name> of <source-expression> with [<type>] <accumulator-name> = <seed-expression> { <body-result> }`. An optional annotation uses Haven's existing type-first binding order: `with i32 acc = 0`, rather than `with acc i32 = 0`. `fold`, `each` and `of` are reserved words. There is one value binding and one accumulator binding; no ordinal or multiple-accumulator syntax is introduced.

- Evaluation is strict and sequential: evaluate and snapshot the source first, evaluate the seed once second, then execute the body once for each element in ascending ordinal order. The body result becomes the next accumulator automatically. The expression returns the final accumulator; an empty fixed array returns the seed without executing the body. There is no parallel reduction or reassociation; normal strict floating-point order applies.
- Sources and snapshot/copy semantics are the same as value iteration: vectors yield float cells, matrices yield rows, and fixed arrays yield their element type. Mutating the original source in the seed or body cannot change the captured aggregate. Pointer/box elements retain ordinary shallow value semantics.
- The value and current accumulator are immutable, body-scoped value bindings. The compiler keeps the changing accumulator privately. Both names must be distinct. They are not visible in the source or seed expressions (an outer binding with the same name remains visible there). Explicitly copy into a `let mut` local when useful; return the next value through the body's final expression.
- The accumulator type comes from the seed or explicit annotation and stays fixed throughout the fold. Each body result must be compatible under ordinary Haven assignment rules, including existing numeric conversions. Use `0.0` or an explicit `float` accumulator for floating-point sums; an integer seed does not automatically widen the accumulator based on later elements. Aggregate dimensions and fixed-array sizes must match. The body is checked even for an empty input and must produce a value.
- `break`, `continue` and `ret` are rejected anywhere inside a fold body, including its nested loops or blocks. This first version requires every visited element to supply a next accumulator. Called functions use their own normal return rules; ordinary loops outside the fold retain their controls. `defer` retains its existing function-exit behavior; use ordinary calls/statements for effects that must happen per element.
- Generic vector/matrix shapes are specialized before LLVM emission. Fold lowers to ordinary Core blocks, lets, indexing, assignment and a bounded loop, so existing purity, ownership and optimization rules apply. A fold's lowered loop is statement-bearing and does not become eligible for `inlinehint` merely because the source function uses `=`.

Parenthesize a fold when that makes an enclosing arithmetic expression clearer. Multi-statement fold bodies can declare locals and perform permitted effects; their final expression still supplies exactly one next accumulator.

### while

The `while` statement loops as long as a condition is true-ish:

```
while 1 {
    // ...
};
```

### until

The `until` statement is sugar for `while !cond`:

```
until done {
    // ...
};
```

### Mutation

The `:=` operator mutates the value referenced by a pointer, cell, or box:

```
ptr := 5;
```

The equivalent syntax in C would be `*ptr = 5`.

### ret

The `ret` statement sets the return value for the function and immediately returns to its caller.

```
ret <value>;
```

### defer

The `defer` statement defers the execution of an expression to run right before the current function returns.
On function exit, deferred expressions run before the compiler's ownership cleanup for that scope.

In this example, the string "hello from defer" is printed after the string "Hello, world!". `defer` can be used anywhere within a function and can be very useful for memory and error management.

```
pub fn printf(str fmt, *) -> i32;

pub fn main() -> i32 {
    defer printf("hello from defer\n");

    printf("Hello, world!\n");

    as<i32>(0)
}
```

## Expressions

### Constants

A constant value can be used anywhere that an expression is expected:

```
let integer = 5;
let number = 5.0;
let text = "hello";
let vec = Vec<1.0, 2.0, 3.0>;
let s obj = { 1, 2, 3 };
let foo = Numbers::One;
```

### Block

See [Blocks](#blocks) for more about blocks.

### Binary Expressions

```
<expr> <op> <expr>
```

#### Operators and Precedence

| Operator          | Purpose                          | Precedence |
| ----------------- | -------------------------------- | ---------- |
| `\|\|`            | Logical OR                       | 5          |
| `&&`              | Logical AND                      | 10         |
| `\|`              | Bitwise OR                       | 15         |
| `^`               | Bitwise XOR                      | 20         |
| `&`               | Bitwise AND                      | 25         |
| `==` `!=`         | Boolean Equal, Boolean Not Equal | 30         |
| `<` `<=` `>` `>=` | Boolean Inequalities             | 35         |
| `<<` `>>`         | Bitwise Shifts                   | 40         |
| `+` `-`           | Addition, Subtraction            | 45         |
| `*` `/` `%`       | Multiplication, Division, Modulo | 50         |

Note that parenthesis (`(` `)`) may be used to control order of operations.

#### Short Circuiting

Logical operators (`||` and `&&`) short-circuit their operation:

- `&&` skips the right side when the left side is false; otherwise it evaluates the right side once.
- `||` skips the right side when the left side is true; otherwise it evaluates the right side once.

Scalar conditions use zero/nonzero conversion: integers and floats compare against zero; supported pointer-like values compare against `nil`. Conditions produce a Boolean `u1` result. Vectors, matrices, arrays and structures are not scalar conditions. `if`, `while`, `until`, logical operators and `!` use the same conversion. Comparisons are explicitly converted even when the input is a wider integer; an integer value is not used directly as an LLVM branch condition.

### Variable References

Any variable in scope may be used in an expression. Its value at the time of expression evaluation will be used.

### Dereferences & Indices

#### Structures

```
let x = struct_var.x;
```

#### Arrays

```
let x = array_var[5];
```

#### Vectors

Vectors can be dereferenced using `xyzw` or `rgba` letters, or a digit.

```
let x = vec.x; // 1st element
let a = vec.a; // 4th element
let v = vec.5; // 5th element
```

### Calls

Functions may be called using parentheses:

```
let result = my_function(1, 2, 3);
```

### Casts

Use the `as` syntax to cast between types:

```
let x = as<i32>(5);
```

### Unary Expressions

```
let x = !0; // 1
let y = 3 ^ 1; // 2
let z = ~0; // (all bits set to one)
```

### If Expressions

#### As an Expression

`if` may be used as an expression to select between two values. Both the `then` and `else` expressions must resolve to the same type. An `else` is not optional in this context.

```
let sign = if x >= 0 { 0 } else { 1 };
```

Branches use braced blocks in the maintained parser. Each value branch ends in a result expression; an `else if` chains another conditional.

#### As a Statement

When used as a statement, `if` does not require its blocks to have identical types, and an `else` is not required.

```
if x >= 0 {
    // do things
} else {
    // do other things
};
```

### References & Nil

To create a pointer to an existing variable or object, use the `ref` keyword:

```
let node tail = { 1, nil };
let node head = { 0, ref tail };
```

`nil` may be used in lieu of a reference to indicate `NULL`. To zero-initialize
an aggregate, use [`zero`](#zero-initialization) instead.

### Load

To read the contents of a pointer created using `ref`, use `load`:

```
{
    let node = load head.next;
    node.value
}
```

### Match

`match` provides the main pattern matching syntax for Haven.

#### Expression Match

This variant simply evaluates comparisons between the condition and the arms of the `match`, returning the expression that matches.

```
let v = match 5 {
    5 => 0,
    4 => { 2 + 2 }, // any expression is valid
    _ => 1
};
```

#### Match without Bindings

```
let v = match number(2) {
    Numbers::Two => 0,
    _ => 1
}
```

#### Match with Bindings

It is an error to _not_ provide a binding if the enum value includes a binding. The `_` binding value allows you to explicitly opt-out of binding.

```
let v = match numeric(0) {
    Numeric::Int(x) => x, // x is defined for the duration of the expression
    Numeric::Float(_) => 0, // you may opt out of binding
    _ => 10
};
```

## Builtins & Low-Level Interfaces

### Builtins

#### size

This builtin offers the size of types and expressions as a constant. It will always resolve to a
size that is constant at compile-time, and will emit error diagnostics if this size cannot be determined.

To use the size of a type as a constant, use the `size<T>` syntax variant:

```
let sz = size<i32>; // 4
```

To use the sixe of an expression's result as a constant, use the `size(...)` syntax variant:

```
let i32 x = 1234;
let sz = size(x);
```

## Intrinsics

Haven compiles to LLVM IR, allowing use of a wide range of LLVM instrinsics.

To declare a function that maps to an intrinsic, use the `intrinsic` keyword in the declaration after the
return type annotation. Following the keyword, add the name of the intrinsic as a string. A comma-separated
list of parameter types follows to help LLVM identify the correct intrinsic variant to use.

For example, the following declarations declare the LLVM `sqrt` and `powi` intrinsics for the program:

```
pub fn __builtin_ipow(float x, i32 power) -> float intrinsic "llvm.powi" float, i32;
pub fn __builtin_sqrtf(float x) -> float intrinsic "llvm.sqrt" float;
```
