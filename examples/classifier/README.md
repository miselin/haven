# Trainable XOR classifier

The Haven program trains a 2 → 3 → 1 sigmoid network with full-batch binary cross-entropy, explicit backpropagation, learning rate 1 and 20,000 updates. Two deterministic LCG seeds (1337 and 2024) are verified. This is a small educational model, not a general ML implementation or an accuracy/performance claim for arbitrary data.

From the repository root:

```sh
python3 examples/classifier/run.py
open output/classifier/index.html
```

Use `--compiler /path/to/haven` and `--output /path/to/output` to select another compiler or destination. Python uses only its standard library. The runner compiles and executes Haven at Os/O0/O1/O2/O3; the Python reference computes its own scalar, double-precision forward/backward pass and training trajectory. The viewer shows native Haven losses and a 61×61 grid produced by its trained model, with an independent reference overlay.

All 13 parameter gradients and both input gradients are checked against central finite differences. Haven uses step 0.01 to control float32 cancellation; its absolute-error bound is 3e-5. Python uses step 1e-5 and verifies its gradients to 1e-8. Analytic cross-language gradients agree to 1e-6. History, probabilities, parameters and grid use explicit tolerances in `run.py`, allowing expected float32-versus-double rounding. Loss must end below 0.001, and all XOR predictions must lie within 0.01 of their targets. No NaN or infinity is accepted.

The classifier uses both singleton and rectangular matrices, generic dense and sigmoid helpers at different widths, reductions at widths 1 and 3, two transpose shapes, two outer-product shapes, row mutation, and nested value aggregates. The weights remain stack/value data; no boxing or new runtime is needed. No compiler fixes were required.

The stable loss is `max(z, 0) + log(1 + exp(-abs(z))) - target*z`, avoiding a logarithm of rounded 0/1 probabilities. Explicit scalar intrinsic declarations supply exp/log. Matrix division is expressed as multiplication by 0.25; transpose and outer products use explicit row loops because the language has no packaged math helper library. Arbitrary seeds are accepted by the executable, but only the two documented seeds and this XOR task are convergence-tested.

The offline HTML also explains the native computation and browser display pipeline, and includes expandable Haven source sections. The runner extracts named functions directly from the current source file; excerpts show their original line locations, and a full-file download plus SHA-256 identifies the embedded source. No external scripts, source fetch or network connection is required.

Single-expression helpers now use `fn … = expression;` with unchanged eager evaluation. `iter each value of source` visits vector components or matrix rows; an optional `indexed by index` clause exposes a zero-based `u32` ordinal. Numeric ranges use `iter each i of start:end[:step]`, retaining their inclusive endpoints, cached bounds and mutable `i32` counter. Legacy headers remain temporarily accepted, but the formatter emits the sentence form. Iteration copies the source once and binds immutable value copies, so mutable results are written explicitly by index. Existing range loops remain useful for raster bounds and framebuffer writes. See `docs/language.md` for the complete semantics.


Natural reductions now use `fold each value of source with acc = 0.0 { acc + value }`. The source snapshot runs first, then the seed once; each eager body result supplies the next accumulator. Bindings are immutable, empty fixed arrays return the seed, and fold bodies reject `break`, `continue` and `ret`. An annotation follows ordinary type-first bindings, such as `with float acc = 0.0`. The classifier sum is shape-generic; camera and cube dot products fold the component-wise product. See `docs/language.md` for semantics and limits.

`ones` and `sigmoid` use eager shape-preserving `map`; the gradient-check target uses contextual `fill`. `sum` keeps `fold … with`. Map bodies return one scalar per vector lane; a matrix map visits rows, with nested maps for cell transforms.
