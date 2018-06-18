/*
Copyright (c) 2018 stoyan shopov

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

static unsigned char physical_mem_map_window[4 /* pages */ * (4 * 1024)] __attribute__((section(".physical-mem-map")));

static do_physical_mem_window_base_address(void) { sf_push((cell) physical_mem_map_window); }
static do_physical_mem_map(void)
{
	/* ( physical-base-address --) */
int i;
uint32_t paddr = sf_pop();

	for (i = 0; i < sizeof physical_mem_map_window >> 12; i ++)
		mem_map_physical_page((uint32_t) physical_mem_map_window + (i << 12), paddr + (i << 12));
}

static struct word dict_base_dummy_word[1] = { MKWORD(0, 0, 0, "", 0), };
static const struct word custom_dict[] = {
	MKWORD(dict_base_dummy_word,	0,	"phys-mem-window-base",		do_physical_mem_window_base_address),
	MKWORD(custom_dict,	__COUNTER__,	"phys-mem-map",			do_physical_mem_map),

}, * custom_dict_start = custom_dict + __COUNTER__;

static void sf_dict_init(void) __attribute__((constructor));
static void sf_dict_init(void)
{
	sf_merge_custom_dictionary(dict_base_dummy_word, custom_dict_start);
}


/* make memory in the physical memory access window non-cacheable */
void init_physical_mem_map(void)
{
int i;
	for (i = 0; i < sizeof physical_mem_map_window >> 12; i ++)
		mem_disable_cache_for_page((uint32_t) physical_mem_map_window + (i << 12));
}

