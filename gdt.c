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

/* segment descriptor types; interpretation depends on whether
 * the system-segment flag in the corresponding segment descriptor is set or not;
 * only interesting values are enumerated, unused ones are not */
enum SEGMENT_TYPE_ENUM
{
	/* segment types when the system segment flag is set (1) */
	DATA_SEGMENT_READ_ONLY	=	0,
	DATA_SEGMENT_READ_WRITE	=	2,
	CODE_SEGMENT_EXECUTE_ONLY	= 8,
	CODE_SEGMENT_EXECUTE_READ	= 10,
	/* segment types when the system segment flag is reset (0) */
	TASK_GATE	= 5,
	TASK_STATE_SEGMENT
};


/* segment descriptor data structure; these reside in the gdt and ldt */
struct segment_descriptor
{
	struct
	{
		/* bits 15-0 of the segment limit */
		uint32_t	limit_15_0	: 16;
		/* bits 15-0 of the segment base address */
		uint32_t	base_15_0	: 16;
	};
	struct
	{
		/* bits 23-16 of the segment base address */
		uint32_t	base_23_16	: 8;
		/* segment type; interpretation depends on whether
		 * the system segment flag (below) is set or not */
		uint32_t	type : 4;
		/* system segment flag; if this bit is reset(0), this is a system
		 * segment, otherwise it is a code/data segment */
		uint32_t	s : 1;
		/* descriptor privilege level */
		uint32_t	dpl : 2;
		/* segment present flag */
		uint32_t	p : 1;
		/* available for use by system software */
		uint32_t	avl : 1;
		/* 64-bit code segment flag (ia-32e mode only) */
		uint32_t	l : 1;
		/* default operation size: 0 - 16-bit segment; 1 - 32-bit segment */
		uint32_t	d_b : 1;
		/* granularity */
		uint32_t	g : 1;
		/* bits 31-24 of the segment base address */
		uint32_t	base_31_24 : 8;
	};
};
