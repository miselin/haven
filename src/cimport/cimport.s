
./src/cimport/cimport.o:     file format elf64-x86-64


Disassembly of section .text:

0000000000000000 <next_token>:
   0:	55                   	push   %rbp
   1:	48 89 e5             	mov    %rsp,%rbp
   4:	41 57                	push   %r15
   6:	41 56                	push   %r14
   8:	53                   	push   %rbx
   9:	50                   	push   %rax
   a:	49 89 f6             	mov    %rsi,%r14
   d:	48 89 fb             	mov    %rdi,%rbx
  10:	48 8b 3d 00 00 00 00 	mov    0x0(%rip),%rdi        # 17 <next_token+0x17>
  17:	31 c0                	xor    %eax,%eax
  19:	e8 00 00 00 00       	call   1e <next_token+0x1e>
  1e:	4c 89 f7             	mov    %r14,%rdi
  21:	e8 00 00 00 00       	call   26 <next_token+0x26>
  26:	41 89 c7             	mov    %eax,%r15d
  29:	41 8d 47 01          	lea    0x1(%r15),%eax
  2d:	83 f8 02             	cmp    $0x2,%eax
  30:	73 32                	jae    64 <next_token+0x64>
  32:	48 89 e0             	mov    %rsp,%rax
  35:	48 8d b0 f0 fe ff ff 	lea    -0x110(%rax),%rsi
  3c:	48 89 f4             	mov    %rsi,%rsp
  3f:	c7 80 f0 fe ff ff 00 	movl   $0x0,-0x110(%rax)
  46:	00 00 00 
  49:	ba 04 01 00 00       	mov    $0x104,%edx
  4e:	48 89 df             	mov    %rbx,%rdi
  51:	e8 00 00 00 00       	call   56 <next_token+0x56>
  56:	48 89 d8             	mov    %rbx,%rax
  59:	48 8d 65 e8          	lea    -0x18(%rbp),%rsp
  5d:	5b                   	pop    %rbx
  5e:	41 5e                	pop    %r14
  60:	41 5f                	pop    %r15
  62:	5d                   	pop    %rbp
  63:	c3                   	ret    
  64:	45 84 ff             	test   %r15b,%r15b
  67:	7e c9                	jle    32 <next_token+0x32>
  69:	41 0f b6 ff          	movzbl %r15b,%edi
  6d:	e8 00 00 00 00       	call   72 <next_token+0x72>
  72:	85 c0                	test   %eax,%eax
  74:	74 2a                	je     a0 <next_token+0xa0>
  76:	48 8b 3d 00 00 00 00 	mov    0x0(%rip),%rdi        # 7d <next_token+0x7d>
  7d:	4c 89 f6             	mov    %r14,%rsi
  80:	31 c0                	xor    %eax,%eax
  82:	e8 00 00 00 00       	call   87 <next_token+0x87>
  87:	4c 89 f7             	mov    %r14,%rdi
  8a:	e8 00 00 00 00       	call   8f <next_token+0x8f>
  8f:	41 89 c7             	mov    %eax,%r15d
  92:	41 8d 47 01          	lea    0x1(%r15),%eax
  96:	83 f8 02             	cmp    $0x2,%eax
  99:	72 97                	jb     32 <next_token+0x32>
  9b:	45 84 ff             	test   %r15b,%r15b
  9e:	7e 92                	jle    32 <next_token+0x32>
  a0:	41 0f b6 c7          	movzbl %r15b,%eax
  a4:	8d 48 d0             	lea    -0x30(%rax),%ecx
  a7:	48 89 e6             	mov    %rsp,%rsi
  aa:	48 81 c6 f0 fe ff ff 	add    $0xfffffffffffffef0,%rsi
  b1:	48 89 f4             	mov    %rsi,%rsp
  b4:	83 f9 09             	cmp    $0x9,%ecx
  b7:	77 08                	ja     c1 <next_token+0xc1>
  b9:	c7 06 01 00 00 00    	movl   $0x1,(%rsi)
  bf:	eb 88                	jmp    49 <next_token+0x49>
  c1:	83 c0 df             	add    $0xffffffdf,%eax
  c4:	83 f8 5d             	cmp    $0x5d,%eax
  c7:	77 f0                	ja     b9 <next_token+0xb9>
  c9:	48 8d 0d 00 00 00 00 	lea    0x0(%rip),%rcx        # d0 <next_token+0xd0>
  d0:	48 63 04 81          	movslq (%rcx,%rax,4),%rax
  d4:	48 01 c8             	add    %rcx,%rax
  d7:	ff e0                	jmp    *%rax
  d9:	c7 06 12 00 00 00    	movl   $0x12,(%rsi)
  df:	e9 65 ff ff ff       	jmp    49 <next_token+0x49>
  e4:	c7 06 06 00 00 00    	movl   $0x6,(%rsi)
  ea:	e9 5a ff ff ff       	jmp    49 <next_token+0x49>
  ef:	c7 06 04 00 00 00    	movl   $0x4,(%rsi)
  f5:	e9 4f ff ff ff       	jmp    49 <next_token+0x49>
  fa:	c7 06 08 00 00 00    	movl   $0x8,(%rsi)
 100:	e9 44 ff ff ff       	jmp    49 <next_token+0x49>
 105:	c7 06 05 00 00 00    	movl   $0x5,(%rsi)
 10b:	e9 39 ff ff ff       	jmp    49 <next_token+0x49>
 110:	c7 06 18 00 00 00    	movl   $0x18,(%rsi)
 116:	e9 2e ff ff ff       	jmp    49 <next_token+0x49>
 11b:	c7 06 0d 00 00 00    	movl   $0xd,(%rsi)
 121:	e9 23 ff ff ff       	jmp    49 <next_token+0x49>
 126:	c7 06 02 00 00 00    	movl   $0x2,(%rsi)
 12c:	e9 18 ff ff ff       	jmp    49 <next_token+0x49>
 131:	c7 06 0f 00 00 00    	movl   $0xf,(%rsi)
 137:	e9 0d ff ff ff       	jmp    49 <next_token+0x49>
 13c:	c7 06 17 00 00 00    	movl   $0x17,(%rsi)
 142:	e9 02 ff ff ff       	jmp    49 <next_token+0x49>
 147:	c7 06 07 00 00 00    	movl   $0x7,(%rsi)
 14d:	e9 f7 fe ff ff       	jmp    49 <next_token+0x49>
 152:	c7 06 03 00 00 00    	movl   $0x3,(%rsi)
 158:	e9 ec fe ff ff       	jmp    49 <next_token+0x49>
 15d:	c7 06 0c 00 00 00    	movl   $0xc,(%rsi)
 163:	e9 e1 fe ff ff       	jmp    49 <next_token+0x49>
 168:	c7 06 0e 00 00 00    	movl   $0xe,(%rsi)
 16e:	e9 d6 fe ff ff       	jmp    49 <next_token+0x49>
 173:	c7 06 11 00 00 00    	movl   $0x11,(%rsi)
 179:	e9 cb fe ff ff       	jmp    49 <next_token+0x49>
 17e:	c7 06 0b 00 00 00    	movl   $0xb,(%rsi)
 184:	e9 c0 fe ff ff       	jmp    49 <next_token+0x49>
 189:	c7 06 09 00 00 00    	movl   $0x9,(%rsi)
 18f:	e9 b5 fe ff ff       	jmp    49 <next_token+0x49>
 194:	c7 06 14 00 00 00    	movl   $0x14,(%rsi)
 19a:	e9 aa fe ff ff       	jmp    49 <next_token+0x49>
 19f:	c7 06 13 00 00 00    	movl   $0x13,(%rsi)
 1a5:	e9 9f fe ff ff       	jmp    49 <next_token+0x49>
 1aa:	c7 06 0a 00 00 00    	movl   $0xa,(%rsi)
 1b0:	e9 94 fe ff ff       	jmp    49 <next_token+0x49>
 1b5:	c7 06 19 00 00 00    	movl   $0x19,(%rsi)
 1bb:	e9 89 fe ff ff       	jmp    49 <next_token+0x49>
 1c0:	c7 06 16 00 00 00    	movl   $0x16,(%rsi)
 1c6:	e9 7e fe ff ff       	jmp    49 <next_token+0x49>
 1cb:	c7 06 15 00 00 00    	movl   $0x15,(%rsi)
 1d1:	e9 73 fe ff ff       	jmp    49 <next_token+0x49>
 1d6:	c7 06 10 00 00 00    	movl   $0x10,(%rsi)
 1dc:	e9 68 fe ff ff       	jmp    49 <next_token+0x49>
 1e1:	66 66 66 66 66 66 2e 	data16 data16 data16 data16 data16 cs nopw 0x0(%rax,%rax,1)
 1e8:	0f 1f 84 00 00 00 00 
 1ef:	00 

00000000000001f0 <haven_cimport_present>:
 1f0:	b8 01 00 00 00       	mov    $0x1,%eax
 1f5:	c3                   	ret    
 1f6:	66 2e 0f 1f 84 00 00 	cs nopw 0x0(%rax,%rax,1)
 1fd:	00 00 00 

0000000000000200 <haven_cimport_process>:
 200:	41 56                	push   %r14
 202:	53                   	push   %rbx
 203:	48 81 ec 08 01 00 00 	sub    $0x108,%rsp
 20a:	48 89 fb             	mov    %rdi,%rbx
 20d:	48 8b 35 00 00 00 00 	mov    0x0(%rip),%rsi        # 214 <haven_cimport_process+0x14>
 214:	e8 00 00 00 00       	call   219 <haven_cimport_process+0x19>
 219:	49 89 c6             	mov    %rax,%r14
 21c:	48 8b 3d 00 00 00 00 	mov    0x0(%rip),%rdi        # 223 <haven_cimport_process+0x23>
 223:	48 89 de             	mov    %rbx,%rsi
 226:	48 89 c2             	mov    %rax,%rdx
 229:	31 c0                	xor    %eax,%eax
 22b:	e8 00 00 00 00       	call   230 <haven_cimport_process+0x30>
 230:	48 89 e7             	mov    %rsp,%rdi
 233:	4c 89 f6             	mov    %r14,%rsi
 236:	e8 c5 fd ff ff       	call   0 <next_token>
 23b:	4c 89 f7             	mov    %r14,%rdi
 23e:	e8 00 00 00 00       	call   243 <haven_cimport_process+0x43>
 243:	b8 ff ff ff ff       	mov    $0xffffffff,%eax
 248:	48 81 c4 08 01 00 00 	add    $0x108,%rsp
 24f:	5b                   	pop    %rbx
 250:	41 5e                	pop    %r14
 252:	c3                   	ret    
