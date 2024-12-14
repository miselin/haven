
impure fn void set_value(i32 *x) {
    store x as i32 5;
}

impure fn i32 load_value(i32 *x) {
    load x
}

pub impure fn i32 sut() {
    let i32 x = as i32 0;
    set_value(ref x);
    load_value(ref x)
}
