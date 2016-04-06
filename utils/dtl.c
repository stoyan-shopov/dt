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
#include <stdio.h>
#include <process.h>
#include <string.h>

#define PHYSICAL_KERNEL_BASE_ADDRESS	0x50000000
#define PHYSICAL_KINIT_BASE_ADDRESS	0x48000000

static void bail_out(void)
{
	printf("usage: dtloader [-i init-filename] [-k kernel-filename] [-d] [-h]\n");
	printf("-h - display this help text\n");
	printf("-d - display welcome screen and halt kernel\n");
	printf("if not specified, 'init-filename' defaults to 'kinit.bin'\n");
	printf("if not specified, 'kernel-filename' defaults to 'kernel.bin'\n");
	exit(1);
}

int main(int argc, char * argv[])
{
char huge * load_buffer = (char huge *) PHYSICAL_KINIT_BASE_ADDRESS;
FILE * kfile, * init_file;
const char * kfile_name = "kernel.bin", * init_name = "kinit.bin";
unsigned i, display_image = 0;
unsigned long total;

	printf("Death Track kernel loader\n");

	for (i = 0; i < argc; i ++)
	{
		if (!strcmp(argv[i], "-h"))
			bail_out();
		else if (!strcmp(argv[i], "-d"))
			display_image = 1;
		else if (!strcmp(argv[i], "-i"))
		{
			if (++i == argc)
				bail_out();
			init_name = argv[i];
		}
		else if (!strcmp(argv[i], "-k"))
		{
			if (++i == argc)
				bail_out();
			kfile_name = argv[i];
		}
	}

	if (!kfile_name || !init_name)
	{
		printf("error: no init/kernel file name specified\n");
		bail_out();
	}
	if (!(kfile = fopen(kfile_name, "r+b")))
	{
		printf("error opening kernel file\n");
		bail_out();
	}
	if (!(init_file = fopen(init_name, "r+b")))
	{
		printf("error opening kernel file\n");
		bail_out();
	}

	printf("loading initialization code...");
	fread(load_buffer, 1, 1024 * 32, init_file);
	printf("done\n");

	printf("loading kernel...");

	total = 0;
	load_buffer = (char huge *) PHYSICAL_KERNEL_BASE_ADDRESS;
	do
	{
		total += i = fread(load_buffer, 1, 1024 * 32, kfile);
		load_buffer += i;
	}
	while (i);
	printf("done. %lu bytes loaded\n", total);
	_asm
	{
		push	display_image;
		xor	ax, ax;
		push	ax;
		/* generate a far call */
		//db	0x9a, 0, 0, (PHYSICAL_KINIT_BASE_ADDRESS shr 16), (PHYSICAL_KINIT_BASE_ADDRESS shr 24);
		db	0x9a, 0, 0, 0, 0x48;
	}
	return 1;
}
