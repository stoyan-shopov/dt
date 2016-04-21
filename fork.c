/*
Copyright (c) 2016 stoyan shopov

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
#include "simple-console.h"
#include "common-data.h"

extern int _data_start;

void fork(void)
{
int i;
	if (NUMBER_OF_KERNEL_PROCESSES < 2)
		return;
	for (i = 1; i < NUMBER_OF_KERNEL_PROCESSES; i ++)
		xmemcpy((void *) ((unsigned) & _data_start + (i  << 20)), & _data_start, 0x200000 - (unsigned) & _data_start);
	if (setjmp(kernel_process_contexts[1]))
	{
		do_console_refresh();
		* (unsigned char *) 0xb8002 = '!';
	}
	else
		for (i = 2; i < NUMBER_OF_KERNEL_PROCESSES; * kernel_process_contexts[i ++] = * kernel_process_contexts[1]);
}

