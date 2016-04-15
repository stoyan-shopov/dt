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
.code32

.extern	x86_idt
.extern	translate_scancode
.extern	kmain

.global load_idtr
.global _8259a_remap
.global _8259a_set_mask
.global _8042_init
.global keyboard_interrupt_handler
.global mouse_interrupt_handler
.global read_io_port_byte
.global write_io_port_byte
.global read_io_port_word
.global write_io_port_word
.global read_io_port_long
.global write_io_port_long
.global enable_paging_low
.global enable_gate_a20
.global get_irq_flag_and_disable_irqs
.global restore_irq_flag

kernel_entry_point:
	/* copy any stack parameters, and relocate the stack */
	/* assumed is that the top of the stack is set up at this location */
	call	enable_gate_a20
	movl	$.,	%ecx
	movl	%ecx,	%esi
	decl	%esi
	subl	%esp,	%ecx
	movl	$(0x200000 - 1),	%edi
	std
	rep	movsb
	cld
	incl	%edi
	movl	%edi,	%esp
	jmp	kmain

load_idtr:
	lidt	x86_idtr_value
	ret


	/* 1 parameter - io port (16 bit) */
read_io_port_byte:
	pushl	%edx
	movl	8(%esp),	%edx
	inb	%dx,	%al
	andl	$255,	%eax
	popl	%edx
	ret

	/* 2 parameters - io port (16 bit), value */
write_io_port_byte:
	pushl	%edx
	movl	8(%esp),	%edx
	movl	12(%esp),	%eax
	outb	%al,	%dx
	popl	%edx
	ret

	/* 1 parameter - io port (16 bit) */
read_io_port_word:
	pushl	%edx
	movl	8(%esp),	%edx
	inw	%dx,	%ax
	andl	$65535,	%eax
	popl	%edx
	ret

	/* 2 parameters - io port (16 bit), value */
write_io_port_word:
	pushl	%edx
	movl	8(%esp),	%edx
	movl	12(%esp),	%eax
	outw	%ax,	%dx
	popl	%edx
	ret

	/* 1 parameter - io port (16 bit) */
read_io_port_long:
	pushl	%edx
	movl	8(%esp),	%edx
	inl	%dx,	%eax
	popl	%edx
	ret

	/* 2 parameters - io port (16 bit), value */
write_io_port_long:
	pushl	%edx
	movl	8(%esp),	%edx
	movl	12(%esp),	%eax
	outl	%eax,	%dx
	popl	%edx
	ret


/* reinitialize the PIC controllers, giving them specified vector offsets
   rather than 8h and 70h, as configured by default */

PIC1		=	0x20		/* IO base address for master PIC */
PIC2		=	0xA0		/* IO base address for slave PIC */
PIC1_COMMAND	=	PIC1
PIC1_DATA	=	(PIC1+1)
PIC2_COMMAND	=	PIC2
PIC2_DATA	=	(PIC2+1)

 
ICW1_ICW4	=	0x01		/* ICW4 (not) needed */
ICW1_SINGLE	=	0x02		/* Single (cascade) mode */
ICW1_INTERVAL4	=	0x04		/* Call address interval 4 (8) */
ICW1_LEVEL	=	0x08		/* Level triggered (edge) mode */
ICW1_INIT	=	0x10		/* Initialization - required! */
 
ICW4_8086	=	0x01		/* 8086/88 (MCS-80/85) mode */
ICW4_AUTO	=	0x02		/* Auto (normal) EOI */
ICW4_BUF_SLAVE	=	0x08		/* Buffered mode/slave */
ICW4_BUF_MASTER	=	0x0C		/* Buffered mode/master */
ICW4_SFNM	=	0x10		/* Special fully nested (not) */
 
/*
arguments:
	offset1 - vector offset for master PIC
		vectors on the master become offset1..offset1+7
	offset2 - same for slave PIC: offset2..offset2+7
*/
_8259a_remap:
	/* note: master/slave is selected in hardware and is not reprogrammable */
	/* icw1 - reinitialize pics */
	pushl	%ebp
	movl	%esp,	%ebp
	movb	$(ICW1_INIT + ICW1_ICW4),	%al
	outb	%al,	$PIC1_COMMAND
	outb	%al,	$PIC2_COMMAND

	/* icw2 - reprogram interrupt vector bases */
	movl	8(%ebp),	%eax
	outb	%al,	$PIC1_DATA
	movl	12(%ebp),	%eax
	outb	%al,	$PIC2_DATA

	/* icw3 - different for master and slave */
	/* program master pic slave mask (here, irq2) */
	movb	$4,	%al
	outb	%al,	$PIC1_DATA
	/* program slave identity */
	movb	$2,	%al
	outb	%al,	$PIC2_DATA

	/* icw4 - operating mode - here, 8086/8088 */
	movb	$ICW4_8086,	%al
	outb	%al,	$PIC1_DATA
	outb	%al,	$PIC2_DATA

	/* clear interrupt masks */
	movb	$0xff,	%al
	outb	%al,	$PIC1_DATA
	outb	%al,	$PIC2_DATA

	popl	%ebp
	ret

_8259a_set_mask:
	movl	4(%esp),	%eax
	outb	%al,		$PIC1_DATA
	movb	$0xef,		%al
	outb	%al,		$PIC2_DATA
	ret


_8042_init:

.macro	WAIT_FOR_8042_INPUT_BUFFER_EMPTY

/* wait for input buffer empty */
1:
	inb	$I8042_STATUS_PORT,		%al
	testb	$I8042_INPUT_BUF_FULL_BIT,	%al
	jnz	1b

.endm

/* data port is read/write */
I8042_DATA_PORT		=	0x60
/* status port is read-only */
I8042_STATUS_PORT	=	0x64
/* data port is write-only */
I8042_COMMAND_PORT	=	0x64

I8042_DISABLE_FIRST_PS2_PORT	=	0xad
I8042_ENABLE_FIRST_PS2_PORT	=	0xae
I8042_DISABLE_SECOND_PS2_PORT	=	0xa7
/* bits in the command byte */
I8042_OUTPUT_BUF_FULL_BIT	=	0x1
I8042_INPUT_BUF_FULL_BIT	=	0x2

/* command codes */
I8042_READ_COMMAND_BYTE		=	0x20
I8042_WRITE_COMMAND_BYTE	=	0x60

	/* disable devices */

	movb	$I8042_DISABLE_FIRST_PS2_PORT,	%al
	outb	%al,	$I8042_COMMAND_PORT

/* wait for input buffer empty */
1:
	inb	$I8042_STATUS_PORT,		%al
	testb	$I8042_INPUT_BUF_FULL_BIT,	%al
	jnz	1b

	movb	$I8042_DISABLE_SECOND_PS2_PORT,	%al
	outb	%al,	$I8042_COMMAND_PORT

/* wait for input buffer empty */
1:
	inb	$I8042_STATUS_PORT,		%al
	testb	$I8042_INPUT_BUF_FULL_BIT,	%al
	jnz	1b

	/* flush the output buffer */
	inb	$I8042_DATA_PORT,	%al

	/* set controller configuration byte */
/* wait for input buffer empty */
1:
	inb	$I8042_STATUS_PORT,		%al
	testb	$I8042_INPUT_BUF_FULL_BIT,	%al
	jnz	1b
	/* read configuration byte */
	movb	$I8042_READ_COMMAND_BYTE,	%al
	outb	%al,	$I8042_DATA_PORT

/* wait for output buffer full */
1:
	inb	$I8042_STATUS_PORT,		%al
	testb	$I8042_OUTPUT_BUF_FULL_BIT,	%al
	jz	1b

	/* read current command byte */
	inb	$I8042_DATA_PORT,	%al
	pushl	%eax

/* wait for input buffer empty */
1:
	inb	$I8042_STATUS_PORT,		%al
	testb	$I8042_INPUT_BUF_FULL_BIT,	%al
	jnz	1b

	movb	$I8042_WRITE_COMMAND_BYTE,	%al
	outb	%al,	$I8042_COMMAND_PORT

/* wait for input buffer empty */
1:
	inb	$I8042_STATUS_PORT,		%al
	testb	$I8042_INPUT_BUF_FULL_BIT,	%al
	jnz	1b

	popl	%eax
	andb	$~((1 << 7) | (1 << 6) | (1 << 3) | (1 << 1) | (1 << 0)),	%al
	pushl	%eax
	outb	%al,	$I8042_DATA_PORT

/* wait for input buffer empty */
1:
	inb	$I8042_STATUS_PORT,		%al
	testb	$I8042_INPUT_BUF_FULL_BIT,	%al
	jnz	1b

	/* enable device - only first ps/2 port for now */
	movb	$I8042_ENABLE_FIRST_PS2_PORT,	%al
	outb	%al,	$I8042_COMMAND_PORT

/* wait for input buffer empty */
1:
	inb	$I8042_STATUS_PORT,		%al
	testb	$I8042_INPUT_BUF_FULL_BIT,	%al
	jnz	1b

	/* enable irq for the first port */
	movb	$I8042_WRITE_COMMAND_BYTE,	%al
	outb	%al,	$I8042_COMMAND_PORT

/* wait for input buffer empty */
1:
	inb	$I8042_STATUS_PORT,		%al
	testb	$I8042_INPUT_BUF_FULL_BIT,	%al
	jnz	1b

	popl	%eax
	orb	$(1 << 0),	%al
	outb	%al,	$I8042_DATA_PORT

/* wait for input buffer empty */
1:
	inb	$I8042_STATUS_PORT,		%al
	testb	$I8042_INPUT_BUF_FULL_BIT,	%al
	jnz	1b

	ret

#############################################3
	/* reset devices */
	movb	$0xff,	%al
	outb	%al,	$I8042_COMMAND_PORT

/* wait for output buffer full */
1:
	inb	$I8042_STATUS_PORT,		%al
	testb	$I8042_OUTPUT_BUF_FULL_BIT,	%al
	jz	1b

	/* read current command byte */
	inb	$I8042_DATA_PORT,	%al
	pushl	%eax

	ret
#############################################3


keyboard_interrupt_handler_raw:
	pushl	%eax
	/* read scancode */
	inb	$I8042_DATA_PORT,	%al
	/* dump scancode */
	pushl	%eax

	/* dump high order nibble */
	sarl	$4,	%eax
	andl	$15,	%eax
	cmpl	$10,	%eax
	jb	1f
	subl	$10,	%eax
	addl	$('a' - '0'),	%eax
1:
	addl	$'0',	%eax
	movb	%al,	0xb8000

	/* dump low order nibble */
	popl	%eax
	andl	$15,	%eax
	cmpl	$10,	%eax
	jb	1f
	subl	$10,	%eax
	addl	$('a' - '0'),	%eax
1:
	addl	$'0',	%eax
	movb	%al,	0xb8002

	movb	$0x20,	%al
	outb	%al,	$PIC1_COMMAND
	popl	%eax

	iret

keyboard_interrupt_handler:
	pushal
	/* read scancode */
	inb	$I8042_DATA_PORT,	%al
	andl	$0xff,	%eax
	pushl	%eax
	call	translate_scancode
	movb	%al,	0xb8000
	popl	%eax

	movb	$0x20,	%al
	outb	%al,	$PIC1_COMMAND
	popal

	iret

mouse_interrupt_handler:
	pushal
	/* read mouse byte */
	inb	$I8042_DATA_PORT,	%al
	andl	$0xff,	%eax
	pushl	%eax
	call	mouse_handler
	popl	%eax
	movb	$'M',	0xb8000

	movb	$0x20,	%al
	outb	%al,	$PIC2_COMMAND
	outb	%al,	$PIC1_COMMAND
	popal

	iret

enable_gate_a20:

	inb	$0x92,		%al
	orb	$2,		%al
	outb	%al,		$0x92
	ret

get_irq_flag_and_disable_irqs:
	pushfl
	cli
	popl	%eax
	shl	$9,	%eax
	andl	$1,	%eax
	ret

restore_irq_flag:
	movl	4(%esp),	%eax
	orl	%eax,	%eax
	jz	1f
	sti
	ret
1:
	cli
	ret

enable_paging_low:

	movl	4(%esp),	%eax
	orl	$(1 << 4),	%eax
	movl	%eax,		%cr3
	movl	%cr0,		%eax
	orl	$(1 << 31),	%eax
	movl	%eax,		%cr0

	/* copied from 'sonar' */
	ljmp	$0x10,	$1f		# clear prefetch queue
1:
	ret

.data
.align	2
x86_idtr_value:
	.word	256 * 8 - 1
	.long	x86_idt

.end

void IRQ_set_mask(unsigned char IRQline) {
    uint16_t port;
    uint8_t value;
 
    if(IRQline < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        IRQline -= 8;
    }
    value = inb(port) | (1 << IRQline);
    outb(port, value);        
}
 
void IRQ_clear_mask(unsigned char IRQline) {
    uint16_t port;
    uint8_t value;
 
    if(IRQline < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        IRQline -= 8;
    }
    value = inb(port) & ~(1 << IRQline);
    outb(port, value);        
}


#define PIC1_CMD                    0x20
#define PIC1_DATA                   0x21
#define PIC2_CMD                    0xA0
#define PIC2_DATA                   0xA1
#define PIC_READ_IRR                0x0a    /* OCW3 irq ready next CMD read */
#define PIC_READ_ISR                0x0b    /* OCW3 irq service next CMD read */
 
/* Helper func */
static uint16_t __pic_get_irq_reg(int ocw3)
{
    /* OCW3 to PIC CMD to get the register values.  PIC2 is chained, and
     * represents IRQs 8-15.  PIC1 is IRQs 0-7, with 2 being the chain */
    outb(PIC1_CMD, ocw3);
    outb(PIC2_CMD, ocw3);
    return (inb(PIC2_CMD) << 8) | inb(PIC1_CMD);
}
 
/* Returns the combined value of the cascaded PICs irq request register */
uint16_t pic_get_irr(void)
{
    return __pic_get_irq_reg(PIC_READ_IRR);
}
 
/* Returns the combined value of the cascaded PICs in-service register */
uint16_t pic_get_isr(void)
{
    return __pic_get_irq_reg(PIC_READ_ISR);
}

