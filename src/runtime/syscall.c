#include <stdint.h>
#include <unistd.h>

int64_t __syscall_inner(int64_t num, int64_t p0, int64_t p1, int64_t p2, int64_t p3, int64_t p4,
                        int64_t p5) {
  return (int64_t)syscall((long)num, (long)p0, (long)p1, (long)p2, (long)p3, (long)p4, (long)p5);
}

int64_t ptr2int(void *ptr) {
  return (int64_t)(intptr_t)ptr;
}
