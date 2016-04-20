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
#include <stdint.h>
#include <engine.h>
#include <sf-word-wizard.h>

#include "common-data.h"

/*
 *
 * local function prototypes
 *
 */

void do_bit(void);
void do_inpb(void);
void do_outpb(void);
void do_inpw(void);
void do_outpw(void);
void do_inpl(void);
void do_outpl(void);
void sf_ext_reset(void);

/*
 *
 * generated function bodies
 *
 */

/* utility words */
/* bit

( bit-number -- bitmask)
 */
void do_bit(void)
{
sf_push(1); do_swap(); do_lshift();
}

/* io port words */
/* inpb

( port-number -- value-read)
 */
void do_inpb(void)
{
sf_push(read_io_port_byte(sf_pop()));
}

static void do_reboot(void)
{
	/* force the 8042 keyboard controller to pulse the cpu reset pin */
	/* first, flush any input data available */
	asm("cli\n");
	while (read_io_port_byte(0x64) & 2);
	write_io_port_byte(0x64, 0xfe);
	asm("hlt\n");
}

/* outpb

( value-to-write port-number --)
 */
void do_outpb(void)
{
uint32_t port_nr = sf_pop();
write_io_port_byte(port_nr, sf_pop());
}

/* inpw

( port-number -- value-read)
 */
void do_inpw(void)
{
sf_push(read_io_port_word(sf_pop()));
}

/* outpw

( value-to-write port-number --)
 */
void do_outpw(void)
{
uint32_t port_nr = sf_pop();
write_io_port_word(port_nr, sf_pop());
}

/* inpl

( port-number -- value-read)
 */
void do_inpl(void)
{
sf_push(read_io_port_long(sf_pop()));
}

/* outpl

( value-to-write port-number --)
 */
void do_outpl(void)
{
uint32_t port_nr = sf_pop();
write_io_port_long(port_nr, sf_pop());
}

extern unsigned int _bss_start, _bss_end;
static void do_bss_start(void) { sf_push(& _bss_start); }
static void do_bss_end(void) { sf_push(& _bss_end); }

extern void do_dump_mouse_bytes(void);
void do_console_refresh(void);

static void do_active_process(void) { sf_push(active_process); }

static struct word dict_base_dummy_word[1] = { MKWORD(0, 0, 0, "", 0), };
static const struct word custom_dict[] = {
	MKWORD(dict_base_dummy_word,	0,	"bit",	do_bit),
	MKWORD(custom_dict,	__COUNTER__,	"reboot",	do_reboot),
	/* x86 i/o space manipulation words */
	MKWORD(custom_dict,	__COUNTER__,	"inpb",	do_inpb),
	MKWORD(custom_dict,	__COUNTER__,	"outpb",	do_outpb),
	MKWORD(custom_dict,	__COUNTER__,	"inpw",	do_inpw),
	MKWORD(custom_dict,	__COUNTER__,	"outpw",	do_outpw),
	MKWORD(custom_dict,	__COUNTER__,	"inpl",	do_inpl),
	MKWORD(custom_dict,	__COUNTER__,	"outpl",	do_outpl),

	MKWORD(custom_dict,	__COUNTER__,	"dump-mouse-bytes",	do_dump_mouse_bytes),

	MKWORD(custom_dict,	__COUNTER__,	"bss-start",	do_bss_start),
	MKWORD(custom_dict,	__COUNTER__,	"bss-end",	do_bss_end),
	MKWORD(custom_dict,	__COUNTER__,	"console-refresh",	do_console_refresh),
	MKWORD(custom_dict,	__COUNTER__,	"active-process",	do_active_process),

}, * custom_dict_start = custom_dict + __COUNTER__;

static void sf_dict_init(void) __attribute__((constructor));
static void sf_dict_init(void)
{
	sf_merge_custom_dictionary(dict_base_dummy_word, custom_dict_start);
}

