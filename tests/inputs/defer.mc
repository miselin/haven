pub fn i32 sut_exit(i32 rc);

pub fn i32 sut() {
    defer sut_exit(1);

    0
}
