
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

pub fn i32 printf(str fmt, *);

pub float RAND_MAX = 2147483647.0;

pub fn float sqrtf(float v);
pub fn float ceilf(float v);
pub fn float powf(float a, float b);
pub fn i32 rand();

pub fn fvec3 S(fvec3 o, fvec3 d, i32 maxbounce);
pub fn i8 T(fvec3 o, fvec3 d, float *t, fvec3 *n);

pub fn float R() {
    / (as float rand()) RAND_MAX
}

pub fn fvec3 vadd(fvec3 a, fvec3 b) {
    + a b
}

pub fn fvec3 vscale(fvec3 a, float b) {
    * a b
}

pub fn float vdot(fvec3 a, fvec3 b) {
    let mult = * a b;
    + + mult.x mult.y mult.z
}

pub fn fvec3 vcross(fvec3 a, fvec3 b) {
    let x = - * a.y b.z * a.z b.y;
    let y = - * a.z b.x * a.x b.z;
    let z = - * a.x b.y * a.y b.x;
    <x, y, z>
}

pub fn fvec3 vnorm(fvec3 v) {
    let denom = / 1.0 sqrtf(vdot(v, v));
    * v denom
}

pub fn void printvec(fvec3 v) {
    printf("(%f, %f, %f)\n", v.x, v.y, v.z);
}

pub fn i32 main() {
    let xdim = + 512 1;
    let ydim = xdim;

    printf("P6 %d %d 255 ", xdim, ydim);
    let g = {
        let vec = <neg 6.0, neg 16.0, 0.0>;
        vnorm(vec)
    };
    let a = {
        let cross = vcross(<0.0, 0.0, 1.0>, g);
        * vnorm(cross) 0.002
    };
    let b = {
        let norm = vnorm(vcross(g, a));
        * norm 0.002
    };
    let c = {
        let sum = + a b;
        let mul = * sum neg 256.0;
        + mul g
    };

    iter ydim:0:(neg 1) y {
        iter xdim:0:(neg 1) x {
            let mut p = <13.0, 13.0, 13.0>;

            // 64 rays per pixel
            iter 0:64 r {
                let t = {
                    let left = {
                        let r = - R() 0.5;
                        let delta = * a r;
                        * delta 99.9
                    };
                    let right = {
                        let r = - R() 0.5;
                        let delta = * b r;
                        * delta 99.9
                    };
                    + left right
                };

                let contrib = {
                    let origin = + <17.0, 16.0, 8.0> t;
                    let dir = {
                        let negone = neg 1.0;
                        let negt = * t negone;

                        let xnudge = + R() as float x;
                        let ynudge = + R() as float y;

                        let anudge = * a xnudge;
                        let bnudge = * b ynudge;
                        let nudge = + + anudge bnudge c;

                        let inner = * nudge 16.0;
                        let outer = + negt inner;

                        vnorm(outer)
                    };

                    // printf("o=%.2f %.2f %.2f d=%.2f %.2f %.2f\n", origin.x, origin.y, origin.z, dir.x, dir.y, dir.z);

                    * S(origin, dir, as i32 8) 3.5
                };

                p = + p contrib;
            };

            // printf("%f %f %f\n", p.x, p.y, p.z);
            printf("%c%c%c", as i32 p.x, as i32 p.y, as i32 p.z);
        };
    };

    as i32 0
}

pub fn fvec3 S(fvec3 o, fvec3 d, i32 maxbounce) {
    let float t = 0.0;
    let fvec3 n = <0.0, 0.0, 0.0>;
    let m = T(o, d, ref t, ref n);

    if == maxbounce as i32 0 {
        ret <0.0, 0.0, 0.0>;
    };

    if == m as i8 0 {
        let sky = <0.7, 0.6, 1.0>;
        ret * sky powf(- 1.0 d.z, 4.0);
    };

    let h = {
        let dirt = * d t;
        + o dirt
    };
    let l = {
        let v = <+ R() 9.0, + R() 9.0, 16.0>;
        vnorm(- v h)
    };
    let r = {
        let neg2 = neg 2.0;
        let inner = * vdot(n, d) neg2;
        let outer = * n inner;
        + d outer
    };

    let LdN = vdot(l, n);
    let b = if || (< LdN 0.0) (!= T(h, l, ref t, ref n) as i8 0) {
        0.0
    } else {
        LdN
    };

    let LdR = vdot(l, r);
    let mmm = if (> b 0.0) { 1.0 } else { 0.0 };

    if == m as i8 1 {
        let hfifth = * h 0.2;
        let mul = + * b 0.2 0.1;
        let ceils = + ceilf(hfifth.x) ceilf(hfifth.y);
        let iceils = as i32 ceils;
        let floorsquare = if == (& iceils as i32 1) as i32 1 {
            <3.0, 1.0, 1.0>
        } else {
            <3.0, 3.0, 3.0>
        };
        ret * floorsquare mul;
    };

    {
        let p = powf(* LdR mmm, 99.0);
        let base = <p, p, p>;
        let bounce = * S(h, r, - maxbounce as i32 1) 0.5;
        let contrib = + base bounce;
        // contrib
        base
    }
}

pub fn i8 T(fvec3 o, fvec3 d, float *t, fvec3 *n) {
    store t 100000000.0;
    let mut m = as i8 0;
    let p = neg (/ o.z d.z);
    if < 0.01 p {
        store t p;
        store n <0.0, 0.0, 1.0>;
        m = as i8 1;
    };

    iter 20:0:(neg 1) k_ {
        let k = - k_ 1; // make it really 19..0
        iter 10:0:(neg 1) j_ {
            let j = - j_ 1; // make it really 9..0 // TODO: there's a bug here - 9:0:-1 will skip zero!
            let bit = as i32 << 1 k;
            let lookup = G[j];
            if != (& lookup bit) as i32 0 {
                let p = {
                    let negk = as float neg k;
                    let negj = as float neg j;
                    + o <negk, 0.0, - negj 4.0>
                };
                let b = vdot(p, d);
                let c = - vdot(p, p) 1.0;
                let b2 = * b b;
                let q = - b2 c;

                if > q 0.0 {
                    let s = neg (+ b sqrtf(q));

                    let t_ = load t;
                    if && (< s t_) (> s 0.01) {
                        let dir_dist = * d t_;
                        store t s;
                        store n vnorm(+ dir_dist p);
                        m = as i8 2;
                    };
                };
            };
        };
    };

    m
}
