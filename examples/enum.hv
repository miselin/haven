
type Numeric = enum {
    Int(i32),
    Float(float)
};

fn Numeric numeric(i32 i) {
    match i {
        0 => Numeric::Int(1)
        _ => Numeric::Float(1.0)
    }
}

pub fn i32 main() {
    let left = match numeric(0) {
        enum Numeric::Int(x) => x
        enum Numeric::Float(_) => 0
        _ => 10
    };

    let i32 right = match numeric(1) {
        enum Numeric::Int(_) => 0
        enum Numeric::Float(x) => 1
        _ => 10
    };

    left + right
}
