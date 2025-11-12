.text
.global __syscall_inner
.type   __syscall_inner,@function
__syscall_inner:
    # Shuffle registers left (amd64 ABI -> syscall ABI)
    mov %rdi, %rax
    mov %rsi, %rdi
    mov %rdx, %rsi
    mov %rcx, %rdx
    mov %r8, %rcx
    mov %r9, %r10
    mov 8(%rsp), %r9

    syscall

    # Caller consumes return value
    ret

.global ptr2int
.type   ptr2int,@function
ptr2int:
    mov %rdi, %rax
    ret
