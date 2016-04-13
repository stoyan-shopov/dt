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

#include <stdint.h>

enum
{
	CONSOLE_ROWS			=	50,
	CONSOLE_COLUMNS			=	80,
	CONSOLE_RING_BUFFER_SIZE	=	1024,

	CHARACTER_ATTRIBUTE_NORMAL	=	6 + 8,
	CHARACTER_ATTRIBUTE_CURSOR	=	19,
};

struct video_console
{
	struct
	{
		union
		{
			struct video_memory
			{
				uint8_t	character;
				uint8_t attributes;
			}
			video_memory[CONSOLE_ROWS][CONSOLE_COLUMNS];
			uint16_t raw_video_contents[CONSOLE_ROWS * CONSOLE_COLUMNS];
		};
		volatile struct video_memory (* raw_video_memory)[CONSOLE_ROWS][CONSOLE_COLUMNS];
	};
	int	cursor_row, cursor_column;
	int	cursor_lock_position;

};
