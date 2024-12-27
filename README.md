# The Haven Programming Language

[Language Overview](docs/language.md)

## Why Haven?

Haven is primarily designed to support 2D/3D graphics programming with ever-increasing linear algebra capability baked in.

Math capabilities aside, it is also intended to support systems programming use cases with a number of conveniences:

- `cimport` to directly import C headers for trivial binding to C libraries.
- `defer` to run code on function exit.
- Pattern matching with sum types.
- Default-const variables, and default-pure functions.

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

fn fvec2 apply(fvec2 v, AffineTransform *xform) {
    (v * xform.transform) + xform.translate
}

fn fvec4 build_cdf(fvec4 probabilities) {
    <
        probabilities.x,
        (probabilities.x + probabilities.y),
        (probabilities.x + probabilities.y + probabilities.z),
        (probabilities.x + probabilities.y + probabilities.z + probabilities.w)
    >
}

impure fn i32 cdf_random(fvec4 cdf) {
    let r = (as float rand()) / 2147483647.0;
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

pub impure fn i32 main() {
    let stem = struct AffineTransform {
        mat2x2 {<0.0, 0.0>, <0.0, 0.16>},
        <0.0, 0.0>
    };
    let large_leaf = struct AffineTransform {
        mat2x2 {<0.85, 0.04>, <-0.04, 0.85>},
        <0.0, 1.6>
    };
    let small_leaf = struct AffineTransform {
        mat2x2 {<0.2, -0.26>, <0.23, 0.22>},
        <0.0, 1.6>
    };
    let right_leaf = struct AffineTransform {
        mat2x2 {<-0.15, 0.28>, <0.26, 0.24>},
        <0.0,  0.44>
    };

    let cdf = build_cdf(<0.01, 0.85, 0.07, 0.07>);

    let mut point = <0.0, 0.0>;

    let mut i8* points = calloc(1, 800 * 800);
    defer { free(points); };

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

        let sx = as i32 ((800.0 / 2.0) + (point.x * 100.0));
        let sy = as i32 (800.0 - (point.y * 100.0));
        if sx >= 0 && sy >= 0 && sx < 800 && sy < 800 {
            store (points + (sx + (sy * 800))) as i8 1;
        };
    };

    printf("P6 800 800 255 ");
    iter 0:799 y {
        iter 0:799 x {
            let idx = x + (y * 800);
            if (load (points + idx)) == 1 {
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
