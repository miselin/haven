# High-Level TODOs

## TODOs from early compiler development

- [ ] UTF8 tokens, not chars
- [x] sret setup for returning structs/enums from functions
- [x] probably need a plan for passing structs/enums TO functions too
- [x] semantic analysis pass to identify semantic errors (e.g. match arm issues)
- [x] semantic analysis to detect enum values >4G
- [x] semantic analysis to detect enum that should have a value but doesn't
- [ ] figure out a better way to handle attribute values like "argmem: readwrite" which are now just a hardcoded constant
- [x] ranges stop before the terminal value (e.g. 10..0..-1's last iteration is 1)
- [x] finish on generic enums (for option types and such), some initial code is in there
- [x] `let node *p = ...` fails to parse (unexpected token \*)
- [x] avoid needing to explicitly tag `enum` in pattern matches
- [x] while/do-while loops
- [x] defers need to capture variables in scope
- [ ] match expressions should not require an otherwise arm if they fully exhaust all possible enum options
- [x] make field assignment work on pointers (maybe just use a different syntax like C?)
- [x] sizeof
- [x] expose matrix transpose somehow (more generally - how to expose intrinsics effectively?)
- [x] typecheck/codegen depend on a bug in KV where inserts of the same key don't overwrite the old value (and the new value is inaccessible)
- [ ] migrate more of the C code into Haven
- [ ] full self host
- [x] `else if`
- [x] expressions in fvec init
  - wrap in parens
- [ ] dvec, dmat, double types
- [x] emit_lvalue in codegen to stop the ref/not-ref/load/dont-load madness
- [ ] `<stmt> unless <cond>`
- [ ] va args (`va_arg` IR instruction + intrinsics)
- [ ] maybe emit C for easier bootstrapping?
- [ ] contextual KWs from the lexer - e.g. `type` or `iter` could be a valid identifier, as `type` and `iter` have very specific contexts in which they are valid keywords

## TODOs from later in development

- [ ] there's a subtle bug in struct layout somewhere - specifically around alignment - that's affecting C ABI compatibility (seeing it a lot with AST data structures in particular)
- [ ] use a GC strategy for AST nodes and types, the current structure is _so_ brittle
- [ ] switch to interning for strings across the compiler
- [ ] switch to interning for types
- [ ] drop cimport, support some sort of FFI instead

## Big TODOs for the future

- [ ] self-hosting should look like writing a compiler in the language, _not simply porting the C code_.

- [ ] vector init < <expr>, ... > requires brackets because otherwise we parse the trailing ">" as "greater than" and get confused
