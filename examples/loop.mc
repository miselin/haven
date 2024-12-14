pub fn i32 printf(str fmt, *);

pub fn i32 main() {
    iter 0:4 i {
        printf("%d\n", i);
    };
    iter 4:0:-1 i {
        printf("%d\n", i);
    };
    0
}