
type Numeric = enum {
    Int(i32),
    Float(float)
};

pub fn i32 main() {
    let e = match 0 {
        0 => Numeric::Int(1)
        _ => Numeric::Float(1.0)
    };
    let e2 = match 1 {
        0 => Numeric::Int(1)
        _ => Numeric::Float(1.0)
    };

    let left = match e {
        enum Numeric::Int(x) => x
        enum Numeric::Float(_) => 0
        _ => 10
    };

    let i32 right = match e2 {
        enum Numeric::Int(_) => 0
        enum Numeric::Float(x) => 1
        _ => 10
    };

    // expected: 2
    left + right
}
