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
#include <stdbool.h>
#include "graphics-image.h"
#include "idt.h"
#include "setjmp.h"

static uint8_t INITIAL_DT_SFORTH_CODE[] =
{
#include "init.efs"

" cr .( loading modules...) cr "
#include "pci.efs"
#include "ata.efs"
#include "arena.efs"
" cr .( modules loaded) cr "
//" source type "
};

static int int_handler(void);
extern void asm_handler();
extern struct x86_idt_gate_descriptor x86_idt[256];
extern void load_idtr();
extern void _8259a_remap(unsigned pic1_irq_base, unsigned pic2_irq_base);
extern void _8259a_set_mask(unsigned mask);
extern void _8042_init(void);
extern void init_console(void);
extern void keyboard_interrupt_handler();
extern void mouse_interrupt_handler();

jmp_buf jbuf;

static uint8_t mouse_bytes[3], mouse_idx;
void kmain(bool display_image_and_halt)
{
struct x86_idt_gate_descriptor idesc =
{
	.segment_selector = 0x10,
	.set_to_zero = 0,
	.type = INTERRUPT_GATE_32_BITS,
	.dpl	= 0,
	.present = 1,

};
uint32_t x;
int i;
unsigned char * bss, * data_src, * data_dest;
extern void (* const _init_startup) (void), (* const _init_startup_end) (void);
extern char _data_start, _data_end, _idata_contents_start;
extern unsigned int _bss_start, _bss_end;
void (* const * finit) (void);

	* (unsigned char *) 0xb8002 = 'A';
	bss = (unsigned char *) & _bss_start;
	i = (unsigned int) & _bss_end - (unsigned int) & _bss_start;
	while (i --)
		* bss ++ = 0;

	* (unsigned char *) 0xb8002 = 'B';
	i = (unsigned int) & _data_end - (unsigned int) & _data_start;
	data_src = & _idata_contents_start;
	data_dest = & _data_start;
	while (i --)
		* data_dest ++ = * data_src ++;

	finit = & _init_startup;
	while (finit != & _init_startup_end)
		(* finit ++) ();

	if (display_image_and_halt)
		load_dt_image();

	x = (uint32_t) int_handler;
	idesc.offset_15_0 = x;
	idesc.offset_31_16 = x >> 16;
	for (i = 0; i < 256; i ++)
		x86_idt[i] = idesc;

	x = (uint32_t) keyboard_interrupt_handler;
	idesc.offset_15_0 = x;
	idesc.offset_31_16 = x >> 16;
	x86_idt[0x31] = idesc;

	x = (uint32_t) mouse_interrupt_handler;
	idesc.offset_15_0 = x;
	idesc.offset_31_16 = x >> 16;
	x86_idt[0x44] = idesc;

	load_idtr();

	_8259a_remap(0x30, 0x40);
	_8259a_set_mask(~ 6); // enable keyboard only
	/*! \todo	the 8042 initialization is currently buggy... debug it */
	if (0) _8042_init();

	init_console();

	populate_initial_page_directory();
	enable_paging();

	fork();

	sf_init();
	sf_eval(INITIAL_DT_SFORTH_CODE);

	do_quit();

	asm("sti");
	while (1)
		asm("hlt");
}

void do_dump_mouse_bytes(void)
{
	sf_push(mouse_bytes[2]);
	sf_push(mouse_bytes[1]);
	sf_push(mouse_bytes[0]);
	sf_eval(".( mouse bytes:) . . . cr");
}


static int int_handler(void)
{
	* (unsigned char *) 0xb8002 = 'X';
	sf_eval(".( UNHANDLED INTERRUPT)");
	while (1)
		asm("hlt");
}

void mouse_handler(uint8_t byte)
{
	mouse_bytes[mouse_idx ++] = byte;
	mouse_idx %= sizeof mouse_bytes;
}

int load_dt_image(void)
{
unsigned char * p;
int i;
	p = (unsigned char *) 0xb8000;
	for (i = 0; i < sizeof death_track_bitmap;)
	{
		* p ++ = death_track_bitmap[i];
		i ++;
		if (!(i % 80))
		{
			if ((i / 80) & 1)
				p = (unsigned char *) 0xb8000 + 0x2000 + i / 2 - 40;
			else
				p = (unsigned char *) 0xb8000 + i / 2;
		}
	}
	asm("hlt");
	while (1);
}

static void disable_caches(void)
{
	asm(
	"movl	%cr0,	%eax\n"
	"orl	$(1 << 30),	%eax\n"
	"movl	%eax,	%cr0\n"
	"wbinvd\n"
	);
}
