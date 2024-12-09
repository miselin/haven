pub fn i32 and() {
    as i32 (0 && (1 / 0))
}

pub fn i32 or() {
    as i32 (1 || (1 / 0))
}

pub fn i32 sut() {
    and() + or()
}