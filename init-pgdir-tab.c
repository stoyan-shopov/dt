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
#include "common-data.h"

/* initial page directory and table used during the death track
 * kernel initialization; it is meant to identity map the first
 * 4 megabytes of memory */
static struct
{
	struct pgde pgdir[1024];
	struct pgte pgtab[(NUMBER_OF_KERNEL_PROCESSES >> 2) + /* always have at least one page table */ 1][1024];
}
init_pgdir_tab __attribute__((section(".init_pgdir")));

void populate_initial_page_directory(void)
{
	int i, j;
	struct pgte pgte =
	{
		.present			= PGTE_PRESENT,
		.read_write			= PGTE_READ_WRITE,
		.user_supervisor		= PGTE_USER_ACCESS_NOT_ALLOWED,
		.page_write_through		= PGTE_PAGE_WRITE_THROUGH,
		.page_level_cache_disable	= PGTE_PAGE_LEVEL_CACHE_ENABLED,
	};
	xmemset(& init_pgdir_tab, 0, sizeof init_pgdir_tab);

	/* identity map 1 MB for the shared kernel code, and 1 MB for each kernel process */
	for (i = 0; i < NUMBER_OF_KERNEL_PROCESSES + /* always map the first MB of memory */ 1; i ++)
	{
		if (!(i & 3))
		{
			/* set up a new page directory entry */
			init_pgdir_tab.pgdir[i >> 2] = (struct pgde)
			{
				.present			= PGDE_PRESENT,
				.read_write			= PGDE_READ_WRITE,
				.user_supervisor		= PGDE_USER_ACCESS_NOT_ALLOWED,
				.page_write_through		= PGDE_PAGE_WRITE_THROUGH,
				.page_level_cache_disable	= PGDE_PAGE_LEVEL_CACHE_ENABLED,
				.page_size			= 0,
				.physical_address		= (unsigned) init_pgdir_tab.pgtab[i >> 2] >> 12,
			};
		}

		for (j = 0; j < 256; pgte.physical_address = (i << 8) + j, init_pgdir_tab.pgtab[i >> 2][((i & 3) << 8) + j ++] = pgte);
	}
	/* make the page at address 0 non-present, to catch null pointer dereference errors */
	init_pgdir_tab.pgtab[0][0].present = PGTE_NOT_PRESENT;
}

void enable_paging(void)
{
	enable_paging_low(& init_pgdir_tab);
}

void switch_task(int task_number)
{
extern char _data_start;
	if (NUMBER_OF_KERNEL_PROCESSES < 2)
		return;
	if (active_process == task_number)
		return;
	if (!setjmp(kernel_process_contexts[active_process]))
	{
		active_process = task_number;
		next_task_low(
				init_pgdir_tab.pgtab[0] + ((unsigned) & _data_start >> 12),	/* address of first page table entry to adjust */
				((active_process << 20) + ((unsigned) & _data_start)) >> 12,	/* starting physical page number of the adjustment */
				(0x200000 - (unsigned) & _data_start) >> 12,			/* number of page table entries to adjust */
				kernel_process_contexts[active_process]				/* jump buffer address to use for longjmp */
			);

	}
	else
		do_console_refresh();
}


