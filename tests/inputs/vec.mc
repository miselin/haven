pub float mut x = 0.0;
pub float mut y = 0.0;
pub float mut z = 0.0;

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

pub fn fvec3 vnorm(fvec3 v) {
    let denom = / 1.0 sqrtf(vdot(v, v));
    * v denom
}

fn void put_result(fvec3 result) {
    x = result.x;
    y = result.y;
    z = result.z;
}
