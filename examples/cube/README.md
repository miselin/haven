# Spinning cube: OCaml graphics spike

This is a native Haven CPU rasterizer. It emits 96 concatenated 480×360 P6 PPM frames: one complete turn. All geometry, matrix composition, perspective division, Lambert lighting, barycentric triangle coverage, reciprocal-depth testing, and framebuffer writes run in Haven. The Python runner only compiles/executes the program and losslessly packages its pixels as PNGs in a self-contained HTML animation. Browser playback is 24 fps; this is a pre-rendered loop, not an interactive graphics backend.

From the repository root, using the configured OCaml environment (or `nix develop`):

```sh
(cd src && dune build bin/haven.exe)
python3 examples/cube/run.py
open output/cube/index.html
```

`run.py` uses only Python's standard library. `--compiler /path/to/haven` selects another OCaml compiler build; `--output /path/to/output` selects the output directory. Generated files belong under `output/`, not in source control. The HTML file works offline and can be opened directly. Pause and scrub to inspect individual poses.

To render raw pixels without Python:

```sh
src/_build/default/bin/haven.exe --O2 examples/cube/cube.hv -o /tmp/haven-cube
/tmp/haven-cube > /tmp/haven-cube.ppm
```

The PPM stream contains multiple images; a viewer that reads only its first image will show a stationary pose. The HTML runner displays all images.

## Transform convention and corrected lowering

`Mat<Vec<...>, Vec<...>>` contains rows. Matrix indexing returns a row; `m[row][column]` selects an element. Vectors multiply matrices on the left. Thus `v * Rx * Ry * T * P` rotates around X, rotates around Y, translates, then projects. Translation occupies the last row of a homogeneous matrix.

The OCaml implementation previously passed Haven's row-major flat lanes directly into LLVM's column-major matrix multiply intrinsic. Non-symmetric inputs produced incorrect numerical results even though LLVM verification passed. The fix uses `(A B)^T = B^T A^T`: swap the operands, their overloaded intrinsic types, and the outer dimensions. This preserves Haven's existing literal and row-access layout without introducing transpose instructions.

The rectangular probe `Vec<10,20> * Mat<Vec<1,2,3>,Vec<4,5,6>>` now gives `Vec<90,120,150>`, rather than `Vec<50,110,170>`. Multiplying that 2×3 matrix by rows `<7,8>,<9,10>,<11,12>` gives rows `<58,64>,<139,154>`. The runtime regression also checks writable matrix rows, homogeneous translation, noncommuting rotation/translation, composed versus sequential transforms, and unary vector/matrix negation. It runs through the existing C runtime harness at `O0`, `O1`, `O2`, `O3`, and `Os`.

LLVM convention reference: <https://llvm.org/doxygen/classllvm_1_1MatrixBuilder.html>.

## Capability findings and remaining gaps

- `fvec3`, `fvec4`, `mat4x4`, scalar/vector operations, dynamic vector/matrix literals, arrays of vectors, row access, loops, `cimport`, explicit casts, and `defer` suffice for this renderer. No parser change or language redesign was required.
- Trigonometry uses explicit `llvm.sin`, `llvm.cos`, and `llvm.sqrt` intrinsic declarations. Dot/cross helpers are straightforward but need to be declared or imported; there is no packaged graphics/transform library or window integration in the current examples.
- A boxed framebuffer compiled but the default executable link lacked `__haven_new_empty_box`, `__haven_box_ref`, and `__haven_box_unref`. The regression harness supplies these symbols, but standalone box executables still need runtime linkage. The cube uses `malloc`/`free` through existing C imports instead. `free` needs an explicit `as<void*>` conversion.
- A float helper named `abs` after `cimport "stdlib.h"` conflicted with C's integer `abs`. Analysis accepted it and LLVM verification failed with `ret float` in an `i32` function. The helper is named `abs_value` here; conflicting imported signatures still need a frontend diagnostic.
- Unary vector/matrix negation was accepted by analysis but missing from LLVM lowering. It now lowers to lane-wise `fneg`, covered in both IR and executable regressions.
- Matrix transpose, swizzles, and matrix-times-column-vector syntax are not evaluated or added by this spike. The example uses existing row-vector multiplication and extracts XYZ explicitly.
- No clipping pipeline, antialiasing, input-driven camera, or real-time window loop is included. The fixed camera keeps every vertex safely in front of the near plane.

## Validation

Use the existing suite:

```sh
(cd src && dune build && dune runtest)
nix build -L .#ocaml
nix flake check -L
```

The runner checks that the executable exits successfully, every PPM frame is complete, and 96 distinct poses were emitted. Image inspection is still necessary: different hashes alone do not prove correct rendering.

Single-expression helpers now use `fn … = expression;` with unchanged eager evaluation. `iter each value of source` visits vector components or matrix rows; an optional `indexed by index` clause exposes a zero-based `u32` ordinal. Numeric ranges use `iter each i of start:end[:step]`, retaining their inclusive endpoints, cached bounds and mutable `i32` counter. Legacy headers remain temporarily accepted, but the formatter emits the sentence form. Iteration copies the source once and binds immutable value copies, so mutable results are written explicitly by index. Existing range loops remain useful for raster bounds and framebuffer writes. See `docs/language.md` for the complete semantics.


Natural reductions now use `fold each value of source with acc = 0.0 { acc + value }`. The source snapshot runs first, then the seed once; each eager body result supplies the next accumulator. Bindings are immutable, empty fixed arrays return the seed, and fold bodies reject `break`, `continue` and `ret`. An annotation follows ordinary type-first bindings, such as `with float acc = 0.0`. The classifier sum is shape-generic; camera and cube dot products fold the component-wise product. See `docs/language.md` for semantics and limits.

The runner links the system math library explicitly (`--Xl -lm`), since LLVM trigonometric/exponential intrinsics may become `libm` calls on Linux, including at O0.
