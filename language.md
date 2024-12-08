```
pub i32 x = 0; /* global definition, publicly visible */
extern i32 y; /* external reference */

i32 z = 2; /* private definition, not publicly visible */

// internal, not publicly visible
fn i32 add(i32 a, i32 b) {
    + a b // last statement is the return value; it is allowed to have no semicolon
}

fn i32 another(i32 a) {
    match a {
        0 => 1
        1 => 2
        2 => 3
        _ => -1
    }
}

// publicly visible
pub fn i32 main() {
    i32 mut result = 0; // mut modifier creates a mutable value
    result = + add(x, another(3)) z;
    result
}
```

## Types

Core types:

- `iN` = integer of N bits
- `uN` = unsigned integer of N bits
- `str` = string of ASCII characters

Builtin type aliases:

- `char` = `i8`
- `bool` = `u1`

Custom type aliasing:

```
type <new-ty> = <ty>;
```

Structured types:

```
struct [packed] <name> = {
    /* type declarations */
};
```

An optional alignment overrides the minimum alignment required for elements. For example, `1` would generate a "packed" struct.

This creates a type alias `<name>` which can then be used anywhere a `<ty>` is expected.

Arrays:

```
<ty> <name>[<dimension>];
```

Pointers:

```
<ty> *
```

TODO: function pointers?

## Modifiers

- `pub` = make the declaration visible outside of this translation unit. Can only be used at file scope.
- `mut` = make the declaration mutable, allowing its value to change in the current scope. Declarations are default-const.

## Declarations

### Variable

```
[vis] <ty> [mut] <name> [= <init-expr>];
```

This creates a fully-typed variable named `<name>`. At file scope, `<init-expr>` must be constant.

```
let [mut] <name> = <expr>;
```

This creates an optionally-mutable variable named `<name>` that is defined with the type of the given expression.

### Function

```
[vis] fn <ret-ty> <name>([<decl>]*) [attrs] {
    /* statements */
    <optional return expr>
}
```

An implied return is used in the final statement of the function if it is an expression. In this case, a terminating semicolon is _not_ required.

Examples of attributes:

- `noinline` = disallow inlining by optimization passes

## Expressions

Expressions can be bracketed in parentheses to create sub-expressions and control precedence.

### Constants

- `0` = integer value zero
- `-1` = signed integer
- `'c'` = `char`
- `"foo"`
- `*1234` = pointer to address `0x1234`

Integer constants will adopt the type of their destination. If truncation would be required to fit, an error will be thrown.

### Variables

- `a` = resolves to the value of the `a` variable
- `&a` = resolves to the address of the `a` variable
- `a(<expr>*)` = calls the function named `a` with the given expressions as parameters

### Unary

- `~a` = 2's complement of `a`
- `-a` = signed negation of `a`
- `!a` = logical NOT of `a` (resolves to either 0 or 1, as an `i1`)

### Binary

`<op> <expr-left> <expr-right>`

Perform binary operation on the two expressions (e.g. `add`, `sub`).

Logical operators (e.g. `||`, `&&`) short-circuit evaluation of the left and right expressions.

### Conditional

`if <cond-expr> <expr-true> <expr-false>`

Resolve to `<expr-true>` if `<cond-expr>` is true(-ish), `<expr-false>` otherwise. Short-circuits, does not evaluate the unused expression.

### Blocks

A block contains statements, separated by semicolons.

The last statement of a block may be an expression, which does _not_ require a semicolon. The result of this expression will become the result of the block.

```
{
    i32 x = 1;
    i32 y = 2;
    + x y
}
```

### Match

```
match <match-expr> {
    <case-expr> => <expr>
    [_ => <expr>]
}
```

Matches the given expression against a range of case values. The case values can be constants or patterns. All inner expressions must resolve to the same type.

An example of pattern matching to extract fields as local variables:

```
match some_struct {
    struct_type(x: x, y: y) => + x y
    _ => -1
}
```

An example of pattern matching to change behavior based on values:

```
match some_struct {
    struct_type(x: 0, y: 0) => -1
    struct_type(x: x, y: y) => + x y
    _ => -2
}
```

### Type Conversion

`<expr> as <ty>`

Resolves the given expression as the given type.

Widened integer types will zero-extend if unsigned, and sign-extend if signed. Narrowed types will truncate the high bits and cannot guarantee correct sign.

## Statements

All statements end in a semicolon (`;`).

### Return

Early return from a function.

```

return <expr>;

```

### Expression

Any expression can be used as a statement. They should be terminated by a semicolon unless they are the last statement of the block.

```

```

### Loops

```
iter <start>..<end>[..<step>] <index-var> { ... } // statement - iterate over a range of integers
map <array-var> <element-var> { ... } // expression (returns array of block type, which may differ from the input)
each <array-var> <element-var> { ... } // statement (void block)
reduce <array-var> <init-value> <accumulator> { ... }
```

## Other ideas

first class vector support: `fvec<N>` for floating point vectors, `ivec<N>` for integer vectors

```

```
