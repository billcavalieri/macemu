| kernels68k.s - SheepBench's 68k kernels, assembled by CMakeLists.txt into a byte array (k68_code.h) and run by the
| ROM's 68k emulator through Mixed Mode (suite_68k.c). C calling convention: arguments on the stack from 4(sp), the
| result in d0; d2-d7/a2-a6 are preserved. The offset of each routine from the start is in the table below.

	.text
	.globl	k68_start
k68_start:
	.long	k68_int - k68_start
	.long	k68_copy - k68_start
	.long	k68_bytecopy - k68_start
	.long	k68_call - k68_start
	.long	k68_muldiv - k68_start

| long k68_int(long n): add, eor, rotate; 5 instructions an iteration
k68_int:
	move.l	4(%sp),%d1
	moveq	#0,%d0
	move.l	%d2,-(%sp)
	move.l	#0x12345678,%d2
1:	add.l	%d1,%d0
	eor.l	%d2,%d0
	rol.l	#3,%d0
	subq.l	#1,%d1
	bne.s	1b
	move.l	(%sp)+,%d2
	rts

| long k68_copy(long *src, long *dst, long nlongs): move.l (a0)+,(a1)+; 3 instructions an iteration
k68_copy:
	move.l	4(%sp),%a0
	move.l	8(%sp),%a1
	move.l	12(%sp),%d0
1:	move.l	(%a0)+,(%a1)+
	subq.l	#1,%d0
	bne.s	1b
	rts

| long k68_bytecopy(char *src, char *dst, long n): move.b (a0)+,(a1)+; 3 instructions an iteration
k68_bytecopy:
	move.l	4(%sp),%a0
	move.l	8(%sp),%a1
	move.l	12(%sp),%d0
1:	move.b	(%a0)+,(%a1)+
	subq.l	#1,%d0
	bne.s	1b
	rts

| long k68_call(long n): bsr to a leaf that adds one; 4 instructions an iteration; returns n
k68_call:
	move.l	4(%sp),%d1
	moveq	#0,%d0
1:	bsr.s	2f
	subq.l	#1,%d1
	bne.s	1b
	rts
2:	addq.l	#1,%d0
	rts

| long k68_muldiv(long n): mulu.w and divu.w; 9 instructions an iteration
k68_muldiv:
	move.l	4(%sp),%d1
	moveq	#0,%d0
	move.l	%d2,-(%sp)
1:	moveq	#0,%d2
	move.w	%d1,%d2
	mulu.w	#5,%d2
	divu.w	#7,%d2
	add.w	%d2,%d0
	swap	%d2
	add.w	%d2,%d0
	subq.l	#1,%d1
	bne.s	1b
	move.l	(%sp)+,%d2
	rts
