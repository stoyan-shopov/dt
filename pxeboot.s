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

.include "constants.s"

BIOS_BOOT_SEG_BASE		=	0x7c0
STACK_BASE			=	0x600
/* pxe! bios return codes */
PXENV_EXIT_SUCCESS		=	0
/* pxe! bios command codes */
PXENV_GET_CACHED_INFO		=	0x71
PXENV_TFTP_OPEN			=	0x20
PXENV_TFTP_CLOSE		=	0x21
PXENV_TFTP_READ			=	0x22

.text
.code16

.globl entry_point

entry_point:
.org 0
	/* normalize addresses - start from 0 */
	jmpl	$BIOS_BOOT_SEG_BASE, $start
current_text_attributes:
	.byte	3
/*
copied from:
http://webpages.charter.net/danrollins/techhelp/0087.HTM

bits 0-3  the foreground color (the color of the character itself).  It
may have one of the values:
00H black     08H gray
01H blue      09H bright blue
02H green     0aH bright green
03H cyan      0bH bright cyan
04H red       0cH bright red
05H magenta   0dH bright magenta
06H brown     0eH yellow
07H white     0fH bright white

bits 4-6  the background color.  When blink is enabled (the default BIOS
setting), this may have only values 0xH-7xH; that is, dark
colors.  When blink is disabled, the background may be any of
the 16 colors (0xH-fxH).

bit 7  foreground blinks.  When blink is enabled (the default), this
bit may be 1 (values 8xH-FxH) to make the foreground color
flash.  When blinks is disabled, this bit is part of the
background color.
*/

video_ptr:
	.word	0
nr_sectors_to_load:
	.word	16

hex_digits:
	.ascii	"0123456789abcdef"

xcnt:
	.byte	0x40

text_dump_stride:
	.word	(80 - 16 * 3) * 2
dump_data_block:
	/* dumps a data block in memory; block address is given in
	 * ds:si, block size is given in cx */
	pushaw

	movw	$VIDEO_SEG_BASE,	%ax
	movw	%ax,	%es
	movw	%cs:video_ptr,	%di
	xorw	%dx,	%dx
	movb	%cs:current_text_attributes,	%ah
1:
	lodsb
	pushw	%ax
	movw	%ax,	%bx
	shr	$4,	%bx
	andw	$15,	%bx
	addw	$hex_digits,	%bx
	movb	%cs:(%bx),	%al
	stosb
	movb	%ah,	%al
	stosb
	popw	%bx
	andw	$15,	%bx
	addw	$hex_digits,	%bx
	movb	%cs:(%bx),	%al
	stosb
	movb	%ah,	%al
	stosb
	movb	$' ',	%al
	stosb
	movb	%ah,	%al
	stosb
	incw	%dx
	cmpw	$16,	%dx
	jb	2f
	xorw	%dx,	%dx
	addw	%cs:text_dump_stride,	%di
2:
	loop	1b
	movw	%di,	%cs:video_ptr

	popaw
	ret

print_msg:
	/* prints a null terminated string starting at %si */
	pushaw
	movw	$VIDEO_SEG_BASE,	%ax
	movw	%ax,	%es
	movw	%cs:video_ptr,	%di
	movb	%cs:current_text_attributes,	%ah

1:
	lodsb
	stosb
	/* check for null terminator - prepare flags for the 'loopnz' below */
	orb	%al,	%al
	movb	%al,	%cl
	movb	%ah,	%al
	stosb
	loopnz	1b
	movw	%di,	%cs:video_ptr
	popaw
	ret

dump_error_code_and_halt:

	cli
	movw	$VIDEO_SEG_BASE,	%ax
	movw	%ax,	%ds
	movb	%al,	0
	hlt


.align	2	
pxe_far_entry_point:
.word	0, 0
pxenv_cached_info_buffer:
.word	0
.word	2
.word	0
.word	0, 0
.word	0

pxenv_tftp_open_buffer:
.word	0
/* TODO: this is the default ip of the tftp server supplied by virtualbox;
 * it hould be adjusted for a non-virtual pxe boot environment */
.byte	10, 0, 2, 4
.byte	0, 0, 0, 0
.asciz	"dt.bin"
.fill	128 - 7, 1, 0
/* !!! port 69 - needs to be in network order (big-endian) !!! */
.word	0x4500
requested_packet_length:
.word	512

negotiated_packet_length:
.word	0

pxenv_tftp_read_buffer:
.word	0
.word	0
/* this is filled by the 'pxenv_tftp_read_buffer' function with the number of bytes read;
 * if this is set to a number less than the negotiated packet length, then this is the
 * last block in the file */
tftp_read_packet_length:
.word	0
.word	tftp_read_buffer
.word	0x7c0

destination_for_kernel_binary:
.long	KERNEL_PHYSICAL_BASE_ADDRESS
display_image_and_halt:
.long	0
boot_selection_string_start:
.asciz	"\nDeath Track kernel PXE loader\n\nChoose boot option:\n1 - display image and halt\nAny other key - normal boot\n"

start:
	movw	$boot_selection_string_start,	%si
1:
	movb	$0xa,	%ah
	movw	$1,	%cx
	movw	$0x0000,	%bx
	lodsb	%cs:(%si),	%al
	orb	%al,	%al
	jz	3f
	pushw	%ax
	cmpb	$'\n',	%al
	je	4f
	int	$0x10
4:
	movb	$3,	%ah
	int	$0x10
	popw	%ax
	incb	%dl
	cmpb	$'\n',	%al
	jne	2f
	movb	$0,	%dl
	incb	%dh
2:
	movb	$2,	%ah
	int	$0x10

	jmp	1b
3:
	movw	$0x0,	%ax
	int	$0x16
	cmpb	$'1',	%al
	jne	1f
	movb	$1,	%cs:display_image_and_halt
1:

enable_gate_a20:

	inb	$0x92,		%al
	orb	$2,		%al
	outb	%al,		$0x92

	pushw	%cs
	popw	%ds

	xorl	%eax,	%eax
	movw	%ds,	%ax
	shll	$4,	%eax
	addl	%eax,	gdt_address
	addl	%eax,	unreal_gdt_address

enter_unreal_mode:
	cli
	/* load unreal global descriptor table register */
	lgdt	%cs:unreal_gdtr_value
	/* enable protection */
	movl	%cr0,	%eax
	orb	$1,	%al
	movl	%eax,	%cr0
	/* serialize processor core - is this really needed? */
	jmp	1f
1:
	/* update segment registers limit values, but do not touch the code segment (cs) register */
	movw	$8,	%bx
	movw	%bx,	%ds
	movw	%bx,	%es
	/* disable protection, and enter big unreal mode */
	andb	$0xfe,	%al
	movl	%eax,	%cr0
	/* serialize processor core - is this really needed? */
	jmp	1f
1:
	cld
	movw	$VIDEO_SEG_BASE,	%ax
	movw	%ax,	%ds
	movb	$'x',	0
	/* initialize data segment */
	movw	%cs,	%ax
	movw	%ax,	%ds

	/* stash pxe! bios entry point */
	movw	%sp,	%bp
	lesw	%ss:4(%bp),	%bx
	lesw	%es:16(%bx),	%bx
	movw	%bx,	pxe_far_entry_point
	movw	%es,	%bx
	movw	%bx,	pxe_far_entry_point + 2

	/* open kernel binary file */
	pushw	%ds
	pushw	$pxenv_tftp_open_buffer
	pushw	$PXENV_TFTP_OPEN
	lcallw	*(pxe_far_entry_point)
	addw	$6,	%sp

	cmpw	$PXENV_EXIT_SUCCESS,	%ax
	je	1f
	movb	$'A',	%al
	jmp	dump_error_code_and_halt
1:
	/* stash negotiated packet length */
	movw	requested_packet_length,	%ax
	movw	%ax,	negotiated_packet_length

	xorw	%ax,	%ax
	movw	%ax,	%es
	movw	%ax,	%ds
	xorl	%ecx,	%ecx

read_kernel_loop:	
	/* read kernel binary */
	pushw	%cs
	pushw	$pxenv_tftp_read_buffer
	pushw	$PXENV_TFTP_READ
	lcallw	*%cs:(pxe_far_entry_point)
	addw	$6,	%sp

	cmpw	$PXENV_EXIT_SUCCESS,	%ax
	je	1f
	movb	$'B',	%al
	jmp	dump_error_code_and_halt
1:
	movw	%cs:tftp_read_packet_length,	%cx
	xorl	%esi,	%esi
	movw	$tftp_read_buffer,	%si
	addl	$0x7c00,		%esi
	movl	%cs:destination_for_kernel_binary,	%edi
	pushw	%cx

	rep	movsb		%ds:(%esi),	%es:(%edi)
	movl	%edi,	%cs:destination_for_kernel_binary

	popw	%cx
	cmpw	%cx,	%cs:negotiated_packet_length
	je	read_kernel_loop

	/* close file */
	pushw	%cs
	pushw	$pxenv_tftp_read_buffer
	pushw	$PXENV_TFTP_CLOSE
	lcallw	*%cs:(pxe_far_entry_point)
	addw	$6,	%sp
	/* ignore status return code */

	/* switch text mode */
	cmpb	$0,	%cs:display_image_and_halt
	je	switch_to_80x50_text_mode
switch_to_monochrome_graphics_mode:
	movw	$6,	%ax	/* graphics mode - cga, monochrome */
	int	$0x10
	jmp	1f
switch_to_80x50_text_mode:
	movw	$3,	%ax	/* text mode, 80x50 */
	mov	$0x1112,	%ax
	xorw	%bx,	%bx
	int	$0x10
	int	$0x10
1:

	movl	%cs:display_image_and_halt,	%ebx

enter_protected_mode:
	cli
	/* load global descriptor table register */
	lgdt	%cs:gdtr_value
	/* enable protection */
	movl	%cr0,	%eax
	orw	$1,	%ax
	movl	%eax,	%cr0

	jmpl	$0x10, $1f + 0x7c00
1:

.code32
	movw	$0x8,	%ax
	movw	%ax,	%ds
	movw	%ax,	%es
	movw	%ax,	%ss
	/* TODO: THIS IS WRONG!!! THIS ADDRESS MAY FALL IN THE ROM BIOS!!! */
	movl	$KERNEL_PHYSICAL_BASE_ADDRESS,	%esp
	movb	$'Q',	0xb8000
	movl	%ebx,	%eax	/* display image and halt parameter */
	movl	$KERNEL_PHYSICAL_BASE_ADDRESS,	%ebx
	jmp	*%ebx

.align 8
/* define the protected mode global descriptor table */
gdt:
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
.align 2
gdtr_value:
/* gdt limit */
.word	gdt_end - gdt - 1

/* gdt physical base address */
gdt_address:
.long	gdt

.align 8
/* define the unreal mode global descriptor table */
unreal_gdt:
/* the null entry */
.long	0
.long	0
/* data segment descriptor - base 0, limit 4 GB, read-write */
.long	0x0000ffff
.long	0x00cf9200
unreal_gdt_end:
.align 2
unreal_gdtr_value:
/* gdt limit */
.word	unreal_gdt_end - unreal_gdt - 1

/* gdt physical base address */
unreal_gdt_address:
.long	unreal_gdt

.align	16
tftp_read_buffer:
.fill	2048, 1, 0

