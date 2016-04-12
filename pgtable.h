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

enum
{
	/* number of page directory entries in a page directory */
	NR_PG_DIRECTORY_ENTRIES		= 1024,
	/* number of page table entries in a page table */
	NR_PG_TABLE_ENTRIES		= 1024,
};

/* page directory entry constants */
enum
{
	PGDE_NOT_PRESENT		= 0,
	PGDE_PRESENT			= 1,
	PGDE_READ_ONLY			= 0,
	PGDE_READ_WRITE			= 1,
	PGDE_USER_ACCESS_NOT_ALLOWED	= 0,
	PGDE_USER_ACCESS_ALLOWED	= 1,
	PGDE_PAGE_WRITE_BACK		= 0,
	PGDE_PAGE_WRITE_THROUGH		= 1,
	PGDE_PAGE_LEVEL_CACHE_ENABLED	= 0,
	PGDE_PAGE_LEVEL_CACHE_DISABLED	= 1,
	PGDE_NOT_ACCESSED		= 0,
	PGDE_ACCESSED			= 1,
};
/* page directory entry */
struct pgde
{
	/* present flag; must be set (1) to reference a page table */
	uint32_t	present : 1;
	/* read-write flag - if reset (0), writes may not be allowed to the 4-MByte
	 * region controlled by this entry */
	uint32_t	read_write : 1;
	/* user-supervisor flag - if reset (0), user-mode accesses are not allowed
	 * to the 4 MByte region controlled by this entry */
	uint32_t	user_supervisor : 1;
	/* page-level write-through flag - indirectly determines the memory type used to
	 * access the page table referenced by this entry */
	uint32_t	page_write_through : 1;
	/* page-level cache disable flag - indirectly determines the memory type used to
	 * access the page table referenced by this entry */
	uint32_t	page_level_cache_disable : 1;
	/* accessed flag - indicates whether this entry has been used for
	 * linear-address translation */
	uint32_t	accessed : 1;
	/* ignored */
	uint32_t	: 1;
	/* page size flag - if cr4.pse is set (1), this must be reset (0), otherwise
	 * this entry maps a 4-MByte page */
	uint32_t	page_size : 1;
	/* ignored */
	uint32_t	: 4;
	/* physical address of 4-KByte aligned page table referenced by this entry */
	uint32_t	physical_address	: 20;
};

/* page directory entry constants */
enum
{
	PGTE_NOT_PRESENT		= 0,
	PGTE_PRESENT			= 1,
	PGTE_READ_ONLY			= 0,
	PGTE_READ_WRITE			= 1,
	PGTE_USER_ACCESS_NOT_ALLOWED	= 0,
	PGTE_USER_ACCESS_ALLOWED	= 1,
	PGTE_PAGE_WRITE_BACK		= 0,
	PGTE_PAGE_WRITE_THROUGH		= 1,
	PGTE_PAGE_LEVEL_CACHE_ENABLED	= 0,
	PGTE_PAGE_LEVEL_CACHE_DISABLED	= 1,
	PGTE_NOT_ACCESSED		= 0,
	PGTE_CLEAN			= 0,
	PGTE_DIRTY			= 1,
};
/* page table entry */
struct pgte
{
	/* present flag; must be set (1) to map a 4 KByte page */
	uint32_t	present : 1;
	/* read-write flag - if reset (0), writes may not be allowed
	 * to the 4-KByte page referenced by this entry */
	uint32_t	read_write : 1;
	/* user-supervisor flag - if reset (0), user-mode accesses are not allowed
	 * to the 4-KByte page referenced by this entry */
	uint32_t	user_supervisor : 1;
	/* page-level write-through flag - indirectly determines the memory type used to
	 * access the 4-KByte page referenced by this entry */
	uint32_t	page_write_through : 1;
	/* page-level cache disable flag - indirectly determines the memory type used to
	 * access the 4-KByte page referenced by this entry */
	uint32_t	page_level_cache_disable : 1;
	/* accessed flag - indicates whether software has accessed
	 * the 4-KByte page referenced by this entry */
	uint32_t	accessed : 1;
	/* dirty flag - indicates whether software has written
	 * to the 4-KByte page referenced by this entry */
	uint32_t	dirty : 1;
	/* if the page attribute table (PAT) is supported, indirectly determines
	 * the memory type used to access the 4-KByte page referenced by this entry */
	uint32_t	pat : 1;
	/* global flag - if cr4.pge is set (1), determines whether the translation is global;
	 * ignored otherwise */
	uint32_t	global : 1;
	/* ignored */
	uint32_t	: 3;
	/* physical address of the 4-KByte page referenced by this entry */
	uint32_t	physical_address	: 20;
};
