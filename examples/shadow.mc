i32 foo = 123;

// should return 123, not 5
// if it returns 5, the shadow foo overrode the global
pub fn i32 main() {
    {
        let foo = 5;
        foo
    };
    foo
}