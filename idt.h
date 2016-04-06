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
/* x86 interrupt descriptor table definitions */
#include <stdint.h>

enum
{
	/* this is the value that must be written to the 'type'
	 * field for 32 bit interrupt gate descriptors; the 'type'
	 * field is in the 'x86_idt_gate_descriptor' data structure
	 * below */
	INTERRUPT_GATE_32_BITS	= 0xe,
};

struct x86_idt_gate_descriptor
{
	uint16_t	offset_15_0;
	uint16_t	segment_selector;
	struct
	{
		//unsigned	: 5;
		uint16_t	set_to_zero : 8; 
		uint16_t	type : 5;
		/* descriptor privilege level */
		uint16_t	dpl : 2;
		/* segment present flag */
		uint16_t	present : 1;
	};
	uint16_t	offset_31_16;

};

