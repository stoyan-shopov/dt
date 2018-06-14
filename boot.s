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

.text
.code16

.globl entry_point

entry_point:
.org 0
	/* normalize addresses - start from 0 */
	jmpl	$BIOS_BOOT_SEG_BASE, $start
display_image_and_halt:
.word	0
boot_selection_string_start:
.asciz	"\nDT\n1 - display image\nOther - normal boot"

init_message:
	.asciz	"death track bootloader launched"
read_error_message:
	.asciz	"failed to read the death track kernel"
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
	movsb
	/* check for null terminator - prepare flags for the 'loopnz' below */
	orb	%al,	%al
	movb	%al,	%cl
	movb	%ah,	%al
	stosb
	loopnz	1b
	movw	%di,	%cs:video_ptr
	popaw
	ret

start:
	cld
	movw	$VIDEO_SEG_BASE,	%ax
	movw	%ax,	%ds
	movb	$'x',	0
	/* initialize segments */
	movw	%cs,	%ax
	movw	%ax,	%ds
	movw	%ax,	%es
	movw	%ax,	%ss
	movw	$STACK_BASE,	%sp

	/* print boot option menu */
	movw	$boot_selection_string_start,	%si
1:
	movb	$0xa,	%ah
	movw	$1,	%cx
	movw	$0x0000,	%bx
	lodsb	%ds:(%si),	%al
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
	movb	$1,	display_image_and_halt
1:

	leaw	init_message,	%si
	callw	print_msg
	movw	$100,	%cx
	movw	$(80 * 2),	video_ptr
	movb	$8,	%ah
	movb	$0x00,	%dl
	/* obtain floppy parameters */
	int	$0x13
	pushw	%dx
	pushw	%cx
	movw	%sp,	%si
	movw	$8,	%cx
	callw	dump_data_block

	xorw	%ax,	%ax

	/* read and jump to the death track kernel */
	movw	$(KINIT_PHYSICAL_BASE_ADDRESS >> 4),	%ax
	movw	%ax,	%es
	/* load sectors per track count */
	popw	%ax
	andb	$0x3f,	%al
	movb	$2,	%ah
	xorw	%bx,	%bx
	movw	$0x0101,	%cx
	movw	$0x00,	%dx

1:
	pushaw
	int	$0x13
	popaw
	jc	1f
	/* switch head */
	xorb	$1,	%dh
	jnz	2f
	/* increment cylinder number */
	addb	$1,	%ch
2:
	movw	%es,	%bx
	pushw	%ax
	andw	$0xff,	%ax
	subw	%ax,	nr_sectors_to_load
	jbe	2f
	shl	$5,	%ax
	add	%bx,	%ax
	movw	%ax,	%es
	popw	%ax
	xorw	%bx,	%bx
	jmp	1b
1:
	leaw	read_error_message,	%si
	callw	print_msg
	jmp	.
2:
	movw	$(KINIT_PHYSICAL_BASE_ADDRESS >> 4),	%ax
	movw	%ax,	%ds
1:
	xorw	%si,	%si
	movw	$512,	%cx
	movw	$0,	%cs:video_ptr
	callw	dump_data_block
	popw	%ax
	decw	%ax
	jz	1f
	decb	%cs:xcnt
	jz	1f
	pushw	%ax
	movw	%ds,	%ax
	addw	$0x20,	%ax
	movw	%ax,	%ds
	xorw	%ax,	%ax
	jmp	1b
1:
	movw	$VIDEO_SEG_BASE,	%ax
	movw	%ax,	%ds
	movb	$'?',	0
	pushw	%cs:display_image_and_halt
	pushw	$1	/* 1 - force kernel loading */
	callw	$(KINIT_PHYSICAL_BASE_ADDRESS >> 4), $0
	jmp	.

.org 512 - 2
	/* boot sector signature - 2 bytes */
	.byte	0x55
	.byte	0xaa

