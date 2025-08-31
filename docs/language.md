# Haven Language Overview

## Key Characteristics

### Interopability

Haven is designed from the outset for interopability with C and other low-level languages.

### Default Const

In Haven, mutability is opt-in, not opt-out. Variables that you expect to modify must be annotated as mutable.

### Default Pure

All functions are assumed "pure" (they do not read or write memory) unless explicitly annotated as `impure`.

## Identifiers

In Haven, identifiers:

- Must start with either an `_` or a letter
- Must end with a digit, letter, or `_`
- Must only contain digits, letters, `_`, or `-`

Hyphens (`-`) may be used within an identifier:

```
-istrue // invalid, cannot start with hyphen
istrue- // invalid, cannot end with hyphen
is-true // valid
```

Note that the `-` operator for arithmetic therefore requires spaces around it when used with two identifiers:

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

### Strings

The `str` type carries string data. It is essentially a `const char *` under the hood.

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

Single-element structs require a trailing comma to initialize:

```
let Thing thing = { 1234, };
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
    Numeric::Float(_) => 1,
}
```

### Arrays

Arrays can be defined by adding a dimension to a type.

```
i32[2] arr = {
    0,
    1
};
```

A single-element array requires a trailing `,`:

```
i32[1] arr = {
    0,
};
```

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

Box types are written much like pointers, but using a caret (`^`) instead
of an asterisk (`*`):

```
fn example(i32^ boxed) -> i32;
```

To directly mutate the value of a box without using `load` or `store`, you can use the `:=` assignment
operator:

```
let val = box 5;
val := 6;
let result = unbox val; // 6
```

Note that `val` does not need to be mutable in this case. The box itself is a mutable cell, and `val` is not
reassigned when using the `:=` operator.

## Declarations

### Import Declarations

#### Haven Imports

Import declarations may only appear at the file scope. An import loads the contents of the imported file, allowing definitions from that file to be used locally.

```
import vec // imports vec.hv
```

#### C Imports

A C import declaration parses a C header file and retains declarations for the purpose of C interopability.

You need to pass `--bootstrap` to the compiler as C imports are currently primarily implemented for the
compiler bootstrap phases. They may become more readily available once a few ergonomics issues are worked out.

```
cimport "stdio.h" // imports declarations from stdio.h as Haven declarations
```

#### Foreign Interfaces

When introducing dependencies on external libraries, you may opt to use the `--Xl -lm` style of command line
flag to present the correct libraries for linking.

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

### Variable Declarations

#### File Scope

At the file scope, only the following declaration form is supported:

```
[pub] [mut] <ty> <ident> [= <init-expr>];
```

File-scope variable declarations include:

- an optional visibility modifier (`pub`) which determines whether the variable is accessible outside of the translation unit
- an optional mutability modifier (`mut`) which determines whether Haven code is permitted to modify the variable
- a required type
- a name
- an initialization expression, which is optional for public variables (creates an external reference) and required otherwise

#### Function Scope

Inside function definitions, variable declarations take a different form:

```
let [<ty>] <ident> = <init-expr>;
```

A type need not be specified. If unspecified, the type of the variable will be inferred from the initialization expression.

Variables at function scope must be initialized.

### Function Declarations

Functions can be forward-declared without a body.

```
[pub] [impure] <ident>(<arg-list>) -> <ret-ty>;
[pub] [impure] <ident>(<arg-list>) -> <ret-ty> { <body> }
```

Specifying `pub` on declarations that have no definitions will create an external reference to the function.

Specifying `impure` on declarations will mark the function as impure, which means it is allowed to read and write memory.

An argument list can be ended with `*` to indicate that the function accepts a variable number of arguments:

```
pub fn printf(str format, *);
```

> [!WARNING]
> Pure functions cannot call impure functions.

The following example shows usage of both a declaration and a defined function:

```
pub fn i32 printf(str fmt, *);

pub fn i32 main() {
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

The `iter` statement iterates over a range.

```
iter 0:10 i {
    printf("%d\n", i);
};
```

Ranges are inclusive; the above range will visit values `0` and `10` during iteration.

A constant step can be provided:

```
iter 10:0:-1 i {};
```

### while

The `while` statement loops as long as a condition is true-ish:

```
while 1 {
    // ...
};
```

### store

The `store` instruction stores a value into the memory pointed to by a pointer:

```
store ptr 5;
```

The equivalent syntax in C would be `*ptr = 5`.

### ret

The `ret` statement sets the return value for the function and immediately returns to its caller.

```
ret <value>;
```

### defer

The `defer` statement defers the execution of an expression to run right before the current function returns.

In this example, the string "hello from defer" is printed after the string "Hello, world!". `defer` can be used anywhere within a function and can be very useful for memory and error management.

```
pub fn i32 printf(str fmt, *);

pub fn i32 main() {
    defer printf("hello from defer\n");

    printf("Hello, world!\n");

    as i32 0
}
```

## Expressions

### Constants

A constant value can be used anywhere that an expression is expected:

```
let integer = 5;
let number = 5.0;
let text = "hello";
let vec = <1.0, 2.0, 3.0>;
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

- if the left side is true-ish, and the operator is `&&`, the right side will not be evaluated
- if the left side is false-ish, and the operator is `||`, the right side will not be evaluated

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

The `as` keyword provides the ability to cast types. In general, casting is quite restricted and its use may indicate a bug in the compiler's type inference passes.

```
let x = as i32 5;
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

> ![NOTE]
> The braces are not required for an `if` expression. It is legal to use _any_ expression, including those not wrapped in a block, in the `then` or `else` blocks of an `if` expression.

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

`nil` may be used in lieu of a reference to indicate `NULL`.

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
    5 => 0
    4 => { 2 + 2 } // any expression is valid
    _ => 1
};
```

#### Match without Bindings

```
let v = match number(2) {
    Numbers::Two => 0
    _ => 1
}
```

#### Match with Bindings

It is an error to _not_ provide a binding if the enum value includes a binding. The `_` binding value allows you to explicitly opt-out of binding.

```
let v = match numeric(0) {
    Numeric::Int(x) => x // x is defined for the duration of the expression
    Numeric::Float(_) => 0 // you may opt out of binding
    _ => 10
};
```
