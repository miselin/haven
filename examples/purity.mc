

fn i32 add(i32 x, i32 y) {
    x + y
}

impure fn void add2(i32 x, i32 y, i32 *out) {
    store out x + y
}

pub impure fn i32 main() {
    let mut i32 result = 0;
    add2(5, 6, ref result);
    add(result, 4)
}
