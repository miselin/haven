
type Result = enum <T> {
    Ok(T),
    Error
};

type NoTemplate = {
    Int(i32),
    Float(float)
};

fn Result<i32> thing() {
    Ok(5)
}

fn NoTemplate foo() {
    Float(1.0)
}

pub fn main() {
    match thing() {
        Ok(x) => x;
        _ => 1;
    }
}
