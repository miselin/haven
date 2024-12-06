
pub fn i32 printf(str fmt, *);

pub fn float sqrtf(float v);

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

pub fn float vnorm(fvec3 v) {
    / 1.0 sqrtf(vdot(v, v))
}

pub fn void printvec(fvec3 v) {
    printf("(%.2f, %.2f, %.2f)\n", v.x, v.y, v.z);
}

pub fn i32 main() {
    0
}
