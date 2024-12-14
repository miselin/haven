
./tests/inputs/defer.o:     file format elf64-x86-64


Disassembly of section .text:

0000000000000000 <sut>:
   0:	50                   	push   %rax
   1:	bf 01 00 00 00       	mov    $0x1,%edi
   6:	e8 00 00 00 00       	call   b <sut+0xb>
   b:	31 c0                	xor    %eax,%eax
   d:	59                   	pop    %rcx
   e:	c3                   	ret    
