/*
Copyright (c) 2014-2016 stoyan shopov

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
PHYSICAL_KERNEL_BASE_ADDRESS	= 0x50000
PHYSICAL_KERNEL_TOP_ADDRESS	= 0xa0000
PHYSICAL_KINIT_BASE	= PHYSICAL_KERNEL_BASE_ADDRESS - 0x8000

.text
.code16
.org	0

	jmp	load_kernel_proper

.align 8
/* define the global descriptor table */
gdt:
.code32
/* the null entry */
.long	0
.long	0
/* data segment descriptor - base 0, limit 4 GB, read-write */
.long	0x0000ffff
.long	0x00cf9200
/* code segment descriptor - base 0, limit 4 GB, reading enabled */
.long	0x0000ffff
.long	0x00cf9a00
gdt_end:
.code16
.align 2
gdtr_value:
/* gdt limit */
.word	gdt_end - gdt - 1
.code32
/* gdt physical base address */
.long	gdt + PHYSICAL_KINIT_BASE
.code16


.align	4

/* read sectors */

/*
well, it turns out that when reading sectors via bios with interrupt 0x13,
the memory where the sectors are being read cannot cross a 64 kilobyte
boundary - this seems to be a limitation of the dma controller
here is some code that does such loading

\ -----------------------------------------------------------
\ -----------------------------------------------------------
\ -----------------------------------------------------------

\ current sector to read in the current track at the current head
\ WARNING: sectors count from 1!
variable c-sector
variable c-head
variable c-track
0 value sectors-per-track
0 value head-cnt
0 value track-cnt

\ current segment that is being filled from disk; must be
\ at a base adddress which is an integral multiple of 64 kilobytes
variable c-segment
\ current offset in the current segment; must be an integral multiple of the sector size
variable c-offset
\ remaining sectors in the current segment
variable sectors-remaining

512 constant sector-size
$1000 constant segment-increment

( initialize values and variables appropriately)
: advance-head ( --)
	1 c-head +!
	c-head @ head-cnt = if 0 c-head ! 1 c-track +! then
	1 c-sector !
	;

: advance-segment ( --)
	segment-increment c-segment +!
	0 c-offset !
	segment-increment $10 * sector-size / sectors-remaining !
	." advancing segment to $" base @ hex c-segment @ . cr base !
	;

: read-sectors-via-BIOS ( sector-count -- t=success|f=failure)
	\ test-drive version - replace with real code
	base @ swap
	." reading #" decimal . ." sectors at $" hex c-segment @ . ." : $" c-offset @ . cr
	base ! true ;

: load-image ( nr-sectors -- t=success|f=failure)
	( initialize variables and values appropriately)
	begin dup 0> while
		\ see how many sectors to read on this run
		sectors-remaining @ sectors-per-track c-sector @ - 1+ min
		\ save count for later use
		dup
		read-sectors-via-BIOS false = if 2drop false exit then
		\ advance current sector number in the current track, and the offset in the current segment
		dup c-sector +! dup sector-size * c-offset +! dup -1 * sectors-remaining +!
		\ see if the current head and/or the current segment need advancing
		c-sector @ sectors-per-track > if advance-head then
		sectors-remaining @ 0= if advance-segment then
		\ update remaining sector count - there is no need for this to be exact, it can go negative
		-
	repeat drop true
	;


\ test drive
18 to sectors-per-track
2 to head-cnt
60 to track-cnt
1 c-sector !
0 c-head !
0 c-track !
advance-segment
0 c-segment !

\ -----------------------------------------------------------
\ -----------------------------------------------------------
\ -----------------------------------------------------------

*/

/* literal translation from forth to assembly (let alone real-mode x86 assembly) is very, very ugly...
   sorry for that, but assembly sucks anyways...  */
c_sector:
// current sector to read in the current track at the current head
// WARNING: sectors count from 1!
.word	1
c_head:
.word	0
c_track:
.word	2
sectors_per_track:
.word	18
head_cnt:
.word	2
track_cnt:
.word	100
c_drive:
.word	0

// current segment that is being filled from disk; must be
// at a base adddress which is an integral multiple of 64 kilobytes
c_segment:
.word	PHYSICAL_KERNEL_BASE_ADDRESS >> 4
// current offset in the current segment; must be an integral multiple of the sector size
c_offset:
.word	0

SECTOR_SIZE		=	512
SEGMENT_INCREMENT	=	0x1000
SECTORS_PER_SEGMENT	=	0x10000 / SECTOR_SIZE

// remaining sectors in the current segment
sectors_remaining:
.word	SECTORS_PER_SEGMENT

/*
: advance-head ( --)
	1 c-head +!
	c-head @ head-cnt = if 0 c-head ! 1 c-track +! then
	1 c-sector !
	;
*/
advance_head:
	incw	c_head
	movw	c_head,	%ax
	cmpw	%ax,	head_cnt
	jnz	1f
	movw	$0,	c_head
	incw	c_track
1:
	movw	$1,	c_sector
	ret

/*
: advance-segment ( --)
	segment-increment c-segment +!
	0 c-offset !
	segment-increment $10 * sector-size / sectors-remaining !
	." advancing segment to $" base @ hex c-segment @ . cr base !
	;
*/
advance_segment:
	addw	$SEGMENT_INCREMENT,	c_segment
	movw	$0,	c_offset
	movw	$SECTORS_PER_SEGMENT,	sectors_remaining
	ret

bios_read_sectors:
	/* %ax - sector count
	 * on return - carry is set if an error occurred */
	pushw	%ax
	movw	c_sector,	%ax
	movb	%al,		%cl
	movw	c_track,	%ax
	movb	%al,		%ch
	movw	c_drive,	%ax
	movb	%al,		%dl
	movw	c_head,	%ax
	movb	%al,		%dh
	pushw	(c_segment)
	popw	%es
	pushw	(c_offset)
	popw	%bx
	popw	%ax
	movb	$2,		%ah
	int	$0x13
	ret

/*
: load-image ( nr-sectors -- t=success|f=failure)
	( initialize variables and values appropriately)
	begin dup 0> while
		\ see how many sectors to read on this run
		sectors-remaining @ sectors-per-track c-sector @ - 1+ min
		\ save count for later use
		dup
		read-sectors-via-BIOS false = if 2drop false exit then
		\ advance current sector number in the current track, and the offset in the current segment
		dup c-sector +! dup sector-size * c-offset +! dup -1 * sectors-remaining +!
		\ see if the current head and/or the current segment need advancing
		c-sector @ sectors-per-track > if advance-head then
		sectors-remaining @ 0= if advance-segment then
		\ update remaining sector count - there is no need for this to be exact, it can go negative
		-
	repeat drop true
	;
*/
load_image:
	/* %ax - number of sectors to load; the current sector, head, track, load segment base and ofsset
	 * and remaining sectors counts must have been initialized prior to invoking this code
	 * on error, return with burden set */
	pushw	%ax
	movw	%sp,	%bp
2:
	movw	sectors_per_track,	%ax
	subw	c_sector,	%ax
	incw	%ax
	cmpw	sectors_remaining,	%ax
	jb	1f
	movw	sectors_remaining,	%ax
1:
	pushw	%ax
	call	bios_read_sectors
	jnc	1f
4:
	popw	%ax
	popw	%ax

	ret
1:
	/* retrieve sector count */
	popw	%ax
	addw	%ax,	c_sector
	subw	%ax,	sectors_remaining
	subw	%ax,	(%bp)

	shlw	$9,	%ax
	addw	%ax,	c_offset

	movw	c_sector,	%ax
	cmpw	%ax,	sectors_per_track
	jae	1f
	call	advance_head
1:
	cmp	$0,	sectors_remaining
	jne	1f
	call	advance_segment
1:
	cmp	$0,	(%bp)
	jg	2b
	clc
	popw	%ax
	ret

display_image_and_halt:	.long	0
force_load_kernel:	.word	0
.align	4
load_kernel_proper:

	pushw	%cs
	popw	%ds
	/* the kernel loader is meant to be entered via a far call, so that it can be entered from a dos loader, as well as from a generic bootloader */
	/* discard return address */
	popw	%ax
	popw	%ax
	/* retrieve parameters */
	popw	%ax
	movw	%ax,	force_load_kernel
	xorl	%eax,	%eax
	popw	%ax
	movl	%eax,	display_image_and_halt
	cmpl	$0,	%eax
	je	1f

	movw	$6,	%ax
	int	$0x10
	jmp	2f
1:
	mov	$3,	%ax
	int	$0x10
	mov	$0x1112,	%ax
	xorw	%bx,	%bx
	int	$0x10
2:
	cmpw	$0,	force_load_kernel
	je	2f
	movw	$((PHYSICAL_KERNEL_TOP_ADDRESS - PHYSICAL_KERNEL_BASE_ADDRESS) / SECTOR_SIZE),	%ax
	call	load_image

	movw	$0xb800,	%ax
	movw	%ax,	%ds

	jb	1f

	movb	$'+',	0
	jmp	2f
1:
	movb	$'-',	0
2:

enter_protected_mode:
	cli
	/* load global descriptor table register */
	lgdt	%cs:gdtr_value
	/* enable protection */
	movl	%cr0,	%eax
	orw	$1,	%ax
	movl	%eax,	%cr0

	jmpl	$0x10, $1f + PHYSICAL_KINIT_BASE
1:
.code32
	movw	$0x8,	%ax
	movw	%ax,	%ds
	movw	%ax,	%es
	movw	%ax,	%ss
	movl	$PHYSICAL_KERNEL_BASE_ADDRESS,	%esp
	movb	$'Q',	0xb8000
	movl	(display_image_and_halt + PHYSICAL_KINIT_BASE),	%eax
	pushl	%eax
	movl	$PHYSICAL_KERNEL_BASE_ADDRESS,	%eax
	call	*%eax
	jmp	.

