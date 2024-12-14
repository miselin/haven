
pub i32[9] G = i32 {
    247570,
    280596,
    280600,
    249748,
    18578,
    18577,
    231184,
    16,
    16
};

pub impure fn i32 printf(str fmt, *);

pub float RAND_MAX = 2147483647.0;

pub fn float sqrtf(float v);
pub fn float ceilf(float v);
pub fn float powf(float a, float b);
pub impure fn i32 rand();
pub fn i32 exit(i32 code);

pub impure fn fvec3 S(fvec3 o, fvec3 d);
pub impure fn i8 T(fvec3 o, fvec3 d, float *t, fvec3 *n);

pub impure fn float R() {
    (as float rand()) / RAND_MAX
}

pub fn fvec3 vadd(fvec3 a, fvec3 b) {
    a + b
}

pub fn fvec3 vscale(fvec3 a, float b) {
    a * b
}

pub fn float vdot(fvec3 a, fvec3 b) {
    let mult = a * b;
    mult.x + mult.y + mult.z
}

pub fn fvec3 vcross(fvec3 a, fvec3 b) {
    let x = a.y * b.z - a.z * b.y;
    let y = a.z * b.x - a.x * b.z;
    let z = a.x * b.y - a.y * b.x;
    <x, y, z>
}

pub fn fvec3 vnorm(fvec3 v) {
    let denom = 1.0 / sqrtf(vdot(v, v));
    v * denom
}

pub impure fn void printvec(fvec3 v) {
    printf("(%f, %f, %f)\n", v.x, v.y, v.z);
}

pub impure fn i32 main() {
    let xdim = 511;
    let ydim = xdim;

    printf("P6 %d %d 255 ", xdim + 1, ydim + 1);
    let g = {
        let vec = <-6.0, -16.0, 0.0>;
        vnorm(vec)
    };
    let a = {
        let cross = vcross(<0.0, 0.0, 1.0>, g);
        vnorm(cross) * 0.002
    };
    let b = {
        let norm = vnorm(vcross(g, a));
        norm * 0.002
    };
    let c = {
        let sum = a + b;
        let mul = sum * -256.0;
        mul + g
    };

    iter ydim:0:-1 y {
        iter xdim:0:-1 x {
            let mut p = <13.0, 13.0, 13.0>;

            iter 0:63 r {
                let t = {
                    {
                        let r = R() - 0.5;
                        let delta = a * r;
                        delta * 99.9
                    } +
                    {
                        let r = R() - 0.5;
                        let delta = b * r;
                        delta * 99.9
                    }
                };

                let contrib = {
                    let origin = <17.0, 16.0, 8.0> + t;
                    let dir = {
                        let negt = t * -1.0;

                        let xnudge = R() + as float x;
                        let ynudge = R() + as float y;

                        let anudge = a * xnudge;
                        let bnudge = b * ynudge;
                        let nudge = anudge + bnudge + c;

                        let inner = nudge * 16.0;
                        let outer = negt + inner;

                        vnorm(outer)
                    };

                    S(origin, dir) * 3.5
                };

                p = p + contrib;
            };

            printf("%c%c%c", as i32 p.x, as i32 p.y, as i32 p.z);
        };
    };

    as i32 0
}

pub impure fn fvec3 S(fvec3 o, fvec3 d) {
    let float t = 0.0;
    let fvec3 n = <0.0, 0.0, 0.0>;

    let m = T(o, d, ref t, ref n);

    match m {
        (as i8 0) => {
            let sky = <0.7, 0.6, 1.0>;
            sky * powf(1.0 - d.z, 4.0)
        }
        _ => {
            let h = {
                let dirt = d * t;
                o + dirt
            };
            let l = {
                let v = <(R() + 9.0), (R() + 9.0), 16.0>;
                vnorm(v - h)
            };
            let r = {
                let inner = vdot(n, d) * -2.0;
                let outer = n * inner;
                d + outer
            };

            let shadow = T(h, l, ref t, ref n);

            let LdN = vdot(l, n);
            let b = if (LdN < 0.0) || (shadow != as i8 0) {
                0.0
            } else {
                LdN
            };

            let LdR = vdot(l, r);
            let mmm = if (b > 0.0) { 1.0 } else { 0.0 };

            match m {
                (as i8 1) => {
                    let hfifth = h * 0.2;
                    let mul = b * 0.2 + 0.1;
                    let ceils = ceilf(hfifth.x) + ceilf(hfifth.y);
                    let iceils = as i32 ceils;
                    let floorsquare = if (iceils & as i32 1) == as i32 1 {
                        <3.0, 1.0, 1.0>
                    } else {
                        <3.0, 3.0, 3.0>
                    };
                    floorsquare * mul
                }
                _ => {
                    let p = powf(LdR * mmm, 99.0);
                    let base = <p, p, p>;
                    let bounce = S(h, r) * 0.5;
                    let contrib = base + bounce;
                    contrib
                }
            }
        }
    }
}

pub impure fn i8 T(fvec3 o, fvec3 d, float *t, fvec3 *n) {
    store t 100000000.0;
    let mut m = as i8 0;
    let p = -o.z / d.z;
    if 0.01 < p {
        store t p;
        store n <0.0, 0.0, 1.0>;
        m = 1;
    };

    iter 0:18 k {
        iter 0:8 j {
            let bit = 1 << k;
            let lookup = as i64 G[j];
            if (lookup & bit) != 0 {
                let p = o + <(as float -k), 0.0, (as float -j - 4.0)>;
                let b = vdot(p, d);
                let c = vdot(p, p) - 1.0;
                let b2 = b * b;
                let q = b2 - c;

                if q > 0.0 {
                    let s = -b - sqrtf(q);

                    let t_ = load t;
                    if (s < t_) && (s > 0.01) {
                        let dir_dist = d * s;
                        store t s;
                        store n vnorm(dir_dist + p);
                        m = as i8 2;
                    };
                };
            };
        };
    };

    m
}
