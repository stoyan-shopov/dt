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
#include "pgtable.h"

/* initial page directory and table used during the death track
 * kernel initialization; it is meant to identity map the first
 * 4 megabytes of memory */
static struct
{
	struct pgde pgdir[1024];
	struct pgte pgtab[1024];
}
init_pgdir_tab __attribute__((section(".init_pgdir")));

void populate_initial_page_directory(void)
{
	int i;
	struct pgte pgte =
	{
		.present			= PGTE_PRESENT,
		.read_write			= PGTE_READ_WRITE,
		.user_supervisor		= PGTE_USER_ACCESS_NOT_ALLOWED,
		.page_write_through		= PGTE_PAGE_WRITE_THROUGH,
		.page_level_cache_disable	= PGTE_PAGE_LEVEL_CACHE_ENABLED,
	};
	xmemset(& init_pgdir_tab, 0, sizeof init_pgdir_tab);

	* init_pgdir_tab.pgdir = (struct pgde)
		{
			.present			= PGDE_PRESENT,
			.read_write			= PGDE_READ_WRITE,
			.user_supervisor		= PGDE_USER_ACCESS_NOT_ALLOWED,
			.page_write_through		= PGDE_PAGE_WRITE_THROUGH,
			.page_level_cache_disable	= PGDE_PAGE_LEVEL_CACHE_ENABLED,
			.page_size			= 0,
			.physical_address		= (unsigned) init_pgdir_tab.pgtab >> 12,
		};

	for (i = 0; i < 1024; pgte.physical_address = i, init_pgdir_tab.pgtab[i ++] = pgte);
	/* make the page at address 0 non-present, to catch null pointer dereference errors */
	init_pgdir_tab.pgtab[0].present = PGTE_NOT_PRESENT;
}

void enable_paging(void)
{
	enable_paging_low(& init_pgdir_tab);
}

