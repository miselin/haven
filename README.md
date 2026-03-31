# The Haven Programming Language

[Language Overview](docs/language.md)

## Why Haven?

Haven is primarily designed to support 2D/3D graphics programming with ever-increasing linear algebra capability baked in.

Math capabilities aside, it is also intended to support systems programming use cases with a number of conveniences:

- `cimport` to directly import C headers for trivial binding to C libraries.
- `defer` to run code on function exit.
- Pattern matching with sum types.
- Default-const variables, and default-pure functions.

And, all else aside, I really just wanted to build a language that's optimized for me and the kind of things I want to write.

## The Compilers

This repository currently contains three compiler implementations:

1. The mainline compiler, written in OCaml, in `src`.
2. The original compiler, written in C, in `legacy/src` and `legacy/include`.
3. The experimental self-hosted compiler, written in Haven, in `src-new`.

The OCaml compiler is the maintained implementation and the default build target throughout the repository.
The C compiler is deprecated and retained as a historical bootstrap artifact.
It can still be built through CMake with `-DWITH_LEGACY_COMPILER=ON`.
`nix build` and plain CMake now both target the OCaml compiler by default; use `nix build .#legacy`
when you explicitly want the deprecated C compiler path.

## AI Usage in Haven Development

I use AI (Codex) to support developing Haven, starting with the OCaml mainline compiler.

The legacy C compiler remains in this repository under `legacy/src` and was largely written by hand, using
the book [Writing a C Compiler](https://norasandler.com/book/) by Nora Sandler as a guide. Haven was not
my first compiler from that book, but it is the first that I've made public. What you will find if you
read the C code is a functional compiler with LLVM IR generation, a compiler driver, and a few compiler passes.
It's code that took months to write, and it's still brittle and fragile.

Here's the thing. I like writing code! But I want to design a language and write _Haven_ code, not so much write
a compiler. AI tools allow me to explore language design without having to be a compiler expert. I review the diffs
and stay fully in the loop of every change to make sure things don't go wildly off track.

## Fractal Example

This example shows some of the Haven syntax and its native vector and matrix support.

The program emits a PPM-formatted image to `stdout` of the [Barnsley Fern](https://en.wikipedia.org/wiki/Barnsley_fern) fractal.

```
cimport "stdio.h";
cimport "stdlib.h";

type AffineTransform = struct {
    mat2x2 transform;
    fvec2 translate;
};

type Image = struct {
    i8[640000] pixels;
};

impure fn apply(fvec2 v, AffineTransform *xform) -> fvec2 {
    (v * xform->transform) + xform->translate
}

fn build_cdf(fvec4 probabilities) -> fvec4 {
    Vec<
        probabilities.x,
        (probabilities.x + probabilities.y),
        (probabilities.x + probabilities.y + probabilities.z),
        (probabilities.x + probabilities.y + probabilities.z + probabilities.w)
    >
}

impure fn cdf_random(fvec4 cdf) -> i32 {
    let r = as<float>(rand()) / 2147483647.0;
    if r < cdf.x {
        0
    } else if r < cdf.y {
        1
    } else if r < cdf.z {
        2
    } else {
        3
    }
}

pub impure fn main() -> i32 {
    let AffineTransform stem = {
        Mat<Vec<0.0, 0.0>, Vec<0.0, 0.16>,>,
        Vec<0.0, 0.0>
    };
    let AffineTransform large_leaf = {
        Mat<Vec<0.85, 0.04>, Vec<-0.04, 0.85>,>,
        Vec<0.0, 1.6>
    };
    let AffineTransform small_leaf = {
        Mat<Vec<0.2, -0.26>, Vec<0.23, 0.22>,>,
        Vec<0.0, 1.6>
    };
    let AffineTransform right_leaf = {
        Mat<Vec<-0.15, 0.28>, Vec<0.26, 0.24>,>,
        Vec<0.0,  0.44>
    };

    let cdf = build_cdf(Vec<0.01, 0.85, 0.07, 0.07>);

    let mut point = Vec<0.0, 0.0>;

    let mut points = box Image;

    iter 0:100000000 i {
        let choice = cdf_random(cdf);
        let xform = match choice {
            0 => ref stem
            1 => ref large_leaf
            2 => ref small_leaf
            3 => ref right_leaf
            _ => ref stem
        };

        point = apply(point, xform);

        let sx = as<i32>((800.0 / 2.0) + (point.x * 100.0));
        let sy = as<i32>(800.0 - (point.y * 100.0));
        if sx >= 0 && sy >= 0 && sx < 800 && sy < 800 {
            points->pixels[sx + (sy * 800)] = 1;
        };
    };

    printf("P6 800 800 255 ");
    iter 0:799 y {
        iter 0:799 x {
            let idx = x + (y * 800);
            if points->pixels[idx] == 1 {
                printf("%c%c%c", 0, 255, 0);
            } else {
                printf("%c%c%c", 0, 0, 0);
            };
        };
    };

    0
}
```

On my machine, a C++ version of this fractal generation (using [Eigen](https://eigen.tuxfamily.org/index.php?title=Main_Page)), completes in a few seconds:

```
$ time ./fractal >../cppfractal.ppm

real    0m6.457s
user    0m6.452s
sys     0m0.005s
```

The program above completes even faster, taking full advantage of the native matrix and vector operations:

```
$ time ./hvfractal >../fractal.ppm

real    0m1.568s
user    0m1.559s
sys     0m0.008s
```
