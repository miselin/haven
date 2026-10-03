# Moving camera and near-plane clipping

This extends the cube software renderer with a moving look-at camera, three rotating objects, and a foreground object crossing the camera's near plane. Geometry, clipping, projection, interpolation, depth tests and all pixels run in compiled Haven. Python verifies and packages those pixels; the offline viewer plays a pre-rendered loop at 24 fps.

```sh
python3 examples/camera/run.py
open output/camera/index.html
```

`--compiler /path/to/haven` and `--output /path/to/output` select another compiler or destination. All scripts use Python's standard library. The executable emits valid P6 frames with a comment containing frame statistics; mode `1` prints geometry checks and mode `2` emits ten raster fixtures.

The row-vector pipeline is model → look-at view → near-plane clipping → perspective division → rasterization. The clipper retains camera-space `z >= 1`, inserts linearly interpolated position/color at crossed edges, and triangulates a resulting quadrilateral. It never divides by an unclipped zero or negative camera depth. The exact near-plane coordinate is pinned after intersection to prevent float32 roundoff reclassifying it. Projection uses a 270-pixel focal length and square pixels.

At each pixel, screen barycentric weights interpolate `1/z` and `color/z`; dividing the latter by the former recovers perspective-correct color. Larger reciprocal depth wins. Winding is normalized and a top-left edge rule controls shared boundaries. Empty viewport bounding boxes and degenerate triangles are rejected before entering inclusive `iter` ranges. Vertex attributes are clipped before projection, rather than clamping projected vertices.

Validation covers every supported optimization level:

- Nine clipping cases: inside, one/two/all outside, exact-plane vertices, parallel edges, wholly behind the camera, and degeneracy. Native vertex positions/colors match a separate scalar, double-precision clipper within 2e-6.
- All 96 look-at poses map the eye to the origin and the target onto positive camera Z, matching the independent camera calculation within 2e-6.
- Ten raster fixtures cover perspective color, reverse draw order, one/two outside vertices, full rejection, exact-plane projection, an 80×80 shared-edge square, fully off-screen geometry, degeneracy and zero/negative pre-clip Z. Empty cases produce exactly the background. Draw-order images match byte for byte. Shared-edge coverage is exactly 6,400 writes.
- All fixture pixel hashes agree across Os/O0/O1/O2/O3. Interior colors match the independent rasterizer within one byte per channel. A one-pixel margin around reference triangle edges is excluded from color comparisons; coverage comparisons allow up to 12 boundary pixels of float32/double disagreement, and measured results are reported rather than assumed.
- The 96-frame native scene must contain clipping, quadrilaterals and full rejection, no invalid projected geometry, every depth-buffer entry finite and in [0, 1 + 2e-6], and 96 unique frames. Full scalar scene references check frames 0, 24, 48 and 72 with the same interior-color/coverage rules. Other scene frames receive invariants and visual playback checks rather than full pixel references.

There were no compiler or runtime fixes. Static arrays, nested vertex/polygon aggregates, pointer-based framebuffer mutation, matrix composition and malloc/free were sufficient. The camera and raster arithmetic are float32; the Python oracle is double precision, so this is deliberately not a universal bit-exact rendering claim. Near-plane clipping does not cap a cut mesh, so interior surfaces can become visible. No far-plane clipping, antialiasing, interactive window, input-driven camera or runtime/ABI redesign is included.

The offline HTML also explains the native computation and browser display pipeline, and includes expandable Haven source sections. The runner extracts named functions directly from the current source file; excerpts show their original line locations, and a full-file download plus SHA-256 identifies the embedded source. No external scripts, source fetch or network connection is required.

Single-expression helpers now use `fn … = expression;` with unchanged eager evaluation. `iter each value of source` visits vector components or matrix rows; an optional `indexed by index` clause exposes a zero-based `u32` ordinal. Numeric ranges use `iter each i of start:end[:step]`, retaining their inclusive endpoints, cached bounds and mutable `i32` counter. Legacy headers remain temporarily accepted, but the formatter emits the sentence form. Iteration copies the source once and binds immutable value copies, so mutable results are written explicitly by index. Existing range loops remain useful for raster bounds and framebuffer writes. See `docs/language.md` for the complete semantics.


Natural reductions now use `fold each value of source with acc = 0.0 { acc + value }`. The source snapshot runs first, then the seed once; each eager body result supplies the next accumulator. Bindings are immutable, empty fixed arrays return the seed, and fold bodies reject `break`, `continue` and `ret`. An annotation follows ordinary type-first bindings, such as `with float acc = 0.0`. The classifier sum is shape-generic; camera and cube dot products fold the component-wise product. See `docs/language.md` for semantics and limits.

The runner links the system math library explicitly (`--Xl -lm`), since LLVM trigonometric/exponential intrinsics may become `libm` calls on Linux, including at O0.
