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
#include <stdbool.h>

enum
{
	CONSOLE_ROWS	=	50,
	CONSOLE_COLUMNS	=	80,
	CONSOLE_RING_BUFFER_SIZE	=	1024,

	CHARACTER_ATTRIBUTE_NORMAL	=	6 + 8,
	CHARACTER_ATTRIBUTE_CURSOR	=	19,
};

static struct
{
	unsigned	shift_active	: 1;
	unsigned	control_active	: 1;
}
console_state =
{
	.shift_active	= 0,	
};

static struct
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

}
video_console;

static struct
{
	int		read_idx, write_idx;
	volatile int	level;
	uint8_t		chars[CONSOLE_RING_BUFFER_SIZE];
}
console_ring_buffer;

void do_console_refresh(void)
{
unsigned irqflag = get_irq_flag_and_disable_irqs();

	xmemcpy(video_console.raw_video_memory, video_console.raw_video_contents, sizeof video_console.raw_video_contents);
	restore_irq_flag(irqflag);
}

/* assumed is that this is called from within interrupt context, and thus cannot block */
static bool console_ring_buffer_try_push(int c)
{
	if (console_ring_buffer.level != CONSOLE_RING_BUFFER_SIZE)
	{
		console_ring_buffer.chars[console_ring_buffer.write_idx ++] = c;
		console_ring_buffer.write_idx %= CONSOLE_RING_BUFFER_SIZE;
		console_ring_buffer.level ++;
		return true;
	}
	return false;
}

/* assumed is that this is called from outside interrupt context */
static int console_ring_buffer_pull(void)
{
int c;
	while (1)
	{
		asm("cli");
		if (!console_ring_buffer.level)
		{
			asm("sti");
			asm("hlt");
			continue;
		}
		break;
	}
	c = console_ring_buffer.chars[console_ring_buffer.read_idx];
	console_ring_buffer.level --;
	asm("sti");

	console_ring_buffer.read_idx ++;
	console_ring_buffer.read_idx %= CONSOLE_RING_BUFFER_SIZE;
	return c;
}

static void do_video_memory_refresh(void)
{
	xmemcpy(video_console.raw_video_memory, video_console.video_memory, sizeof video_console.video_memory);
}
static void do_draw_cursor(void)
{
	video_console.raw_video_memory[0][video_console.cursor_row][video_console.cursor_column].attributes
		= video_console.video_memory[video_console.cursor_row][video_console.cursor_column].attributes = CHARACTER_ATTRIBUTE_CURSOR;
}
static void do_hide_cursor(void)
{
	video_console.raw_video_memory[0][video_console.cursor_row][video_console.cursor_column].attributes
		= video_console.video_memory[video_console.cursor_row][video_console.cursor_column].attributes = CHARACTER_ATTRIBUTE_NORMAL;
}

static void do_console_scroll(void)
{
int i;
	do_hide_cursor();
	xmemcpy(video_console.video_memory, video_console.video_memory[1], (CONSOLE_ROWS - 1) * sizeof * video_console.video_memory);
	for (i = 0; i < CONSOLE_COLUMNS; i ++)
		video_console.video_memory[CONSOLE_ROWS - 1][i].character = ' ';
	do_video_memory_refresh();
	do_draw_cursor();
}

void do_console_cleanup(void)
{
int i, j;

	do_hide_cursor();
	xmemcpy(video_console.video_memory, video_console.video_memory[video_console.cursor_row], sizeof * video_console.video_memory);
	for (i = 1; i < CONSOLE_ROWS; i ++)
		for (j = 0; j < CONSOLE_COLUMNS; j ++)
			video_console.video_memory[i][j].character = ' ';
	do_video_memory_refresh();
	do_draw_cursor();
}

/*

01 	ESC 	
02 	1 	
03 	2 	
04 	3 	
05 	4 	
06 	5 	
07 	6 	
08 	7 	
09 	8 	
0A 	9 	
0B 	0 	
0C 	- _ 	
0D 	= + 	
0E 	BKSP 	
0F 	Tab 	
10 	Q 	
11 	W 	
12 	E 	
13 	R 	
14 	T 	
15 	Y 	
16 	U 	
17 	I 	
18 	O 	
19 	P 	
1A 	[ { 	
1B 	] } 	
1C 	Enter 	
1D 	Ctrl 	
1E 	A 	
1F 	S 	
20 	D 	
21 	F 	
22 	G 	
23 	H 	
24 	J 	
25 	K 	
26 	L 	
27 	; : 	
28 	' " 	
29 	` ~ 	
2A 	L SH 	
2B 	\ | 	
2C 	Z 	
2D 	X 	
2E 	C 	
2F 	V 	
30 	B 	
31 	N 	
32 	M 	
33 	, < 	
34 	. > 	
35 	/ ? 	
36 	R SH 	
37 	PtScr 	
38 	Alt 	
39 	Spc 	
3A 	CpsLk 	
3B 	F1 	
3C 	F2 	
3D 	F3 	
3E 	F4 	
3F 	F5 	
40 	F6 	
41 	F7 	
42 	F8 	
43 	F9 	
44 	F10 	
45 	Num Lk 	
46 	Scrl Lk	
47 	Home 	
48 	Up Arrow
49 	Pg Up 	
4A 	- (num) 
4B 	4 Left A
4C 	5 (num) 
4D 	6 Rt Arr
4E 	+ (num) 
4F 	1 End 	
50 	2 Dn Arr
51 	3 Pg Dn 
52 	0 Ins 	
53 	Del . 	
54 	SH F1 	
55 	SH F2 	
56 	SH F3 	
57 	SH F4 	
58 	SH F5 	
59 	SH F6 	
5A 	SH F7 	
5B 	SH F8 	
5C 	SH F9 	
5D 	SH F10 	
5E 	Ctrl F1	
5F 	Ctrl F2	
60 	Ctrl F3 	
61 	Ctrl F4 	
62 	Ctrl F5 	
63 	Ctrl F6 	
64 	Ctrl F7 	
65 	Ctrl F8 	
66 	Ctrl F9 	
67 	Ctrl F10 	
68 	Alt F1 		
69 	Alt F2 		
6A 	Alt F3 		
6B 	Alt F4 		
6C 	Alt F5 		
6D 	Alt F6 		
6E 	Alt F7 		
6F 	Alt F8 		
70 	Alt F9 		
71 	Alt F10 	
72 	Ctrl PtScr 	
73 	Ctrl L Arrow 	
74 	Ctrl R Arrow 	
75 	Ctrl End 	
76 	Ctrl PgDn 	
77 	Ctrl Home 	
78 	Alt 1 		
79 	Alt 2 		
7A 	Alt 3 		
7B 	Alt 4 		
7C 	Alt 5 		
7D 	Alt 6 		
7E 	Alt 7 		
7F 	Alt 8 		
80 	Alt 9 	A0 	Alt Dn Arrow
81 	Alt 0 	A1 	Alt PgDn
82 	Alt -  	A2 	Alt Ins
82 	Alt = 	A3 	Alt Del
84 	Ctrl PgUp 	A4 	Alt / (num)
85 	F11 	A5 	Alt Tab
86 	F12 	A6 	Alt Enter (num)
87 	SH F11 	  	 
88 	SH F12 	  	 
89 	Ctrl F11 	  	 
8A 	Ctrl F12 	  	 
8B 	Alt F11 	  	 
8C 	Alt F12 	  	 
8C 	Ctrl Up Arrow 	  	 
8E 	Ctrl - (num) 	  	 
8F 	Ctrl 5 (num) 	  	 
90 	Ctrl + (num) 	  	 
91 	Ctrl Dn  Arrow 	  	 
92 	Ctrl Ins 	  	 
93 	Ctrl Del 	  	 
94 	Ctrl Tab 	  	 
95 	Ctrl / (num) 	  	 
96 	Ctrl * (num) 	  	 
97 	Alt Home 	  	 
98 	Alt Up Arrow 	  	 
99 	Alt PgUp 	  	 
9A 	  	  	 
9B 	Alt Left Arrow 	  	 
9C 	  	  	 
9D 	Alt Rt Arrow 	  	 
9E 	  	  	 
9F 	Alt End 	  	 

*/




static int handle_backspace(void)
{
int i, j;

	i = video_console.cursor_row;
	j = video_console.cursor_column;

	if (j > video_console.cursor_lock_position)
	{
		j --;
		(* video_console.raw_video_memory)[i][j].character
			= video_console.video_memory[i][j].character
				= ' ';
		do_hide_cursor();
		video_console.cursor_column = j;
		do_draw_cursor();
	}
	return 0;
}

static int handle_shift(void)
{
	console_state.shift_active ^= 1;
	return 0;
}

static int handle_control(void)
{
	console_state.control_active ^= 1;
	do_console_cleanup();
	do_hide_cursor();
	video_console.cursor_row = 0;
	do_draw_cursor();
	return 0;
}

static void put_enter(void)
{
int i;

	i = video_console.cursor_row;
	if (i != CONSOLE_ROWS - 1)
		i ++;
	else
		do_console_scroll();

	do_hide_cursor();
	video_console.cursor_row = i;
	video_console.cursor_lock_position = video_console.cursor_column = 0;
	do_draw_cursor();
}

static int handle_enter(void)
{
int i;

	for (i = video_console.cursor_lock_position; i < video_console.cursor_column; i ++)
		console_ring_buffer_try_push(video_console.video_memory[video_console.cursor_row][i].character);
	if (console_ring_buffer_try_push('\n'))
		put_enter();

	return 0;
}

static const struct keyboard_scancode_entry
{
	uint8_t sym;
	uint8_t shift_sym;
	int (* make_handler)(void);
	int (* break_handler)(void);
}
translation_table[256] =
{
	[0x02]	=	{ 	'1', '!', 0, },
	[0x03]	=	{ 	'2', '@', 0, },
	[0x04]	=	{ 	'3', '#', 0, },
	[0x05]	=	{ 	'4', '$', 0, },
	[0x06]	=	{ 	'5', '%', 0, },
	[0x07]	=	{ 	'6', '^', 0, },
	[0x08]	=	{ 	'7', '&', 0, },
	[0x09]	=	{ 	'8', '*', 0, },
	[0x0a]	=	{ 	'9', '(', 0, },
	[0x0b]	=	{ 	'0', ')', 0, },
	[0x0c]	=	{ 	'-', '_', 0, },
	[0x0d]	=	{ 	'=', '+', 0, },
	[0x10]	=	{ 	'q', 'Q', 0, },
	[0x11]	=	{ 	'w', 'W', 0, },
	[0x12]	=	{ 	'e', 'E', 0, },
	[0x13]	=	{ 	'r', 'R', 0, },
	[0x14]	=	{ 	't', 'T', 0, },
	[0x15]	=	{ 	'y', 'Y', 0, },
	[0x16]	=	{ 	'u', 'U', 0, },
	[0x17]	=	{ 	'i', 'I', 0, },
	[0x18]	=	{ 	'o', 'O', 0, },
	[0x19]	=	{ 	'p', 'P', 0, },
	[0x1a]	=	{ 	'[', '{', 0, },
	[0x1b]	=	{ 	']', '}', 0, },
	[0x1e]	=	{ 	'a', 'A', 0, },
	[0x1f]	=	{ 	's', 'S', 0, },
	[0x20]	=	{ 	'd', 'D', 0, },
	[0x21]	=	{ 	'f', 'F', 0, },
	[0x22]	=	{ 	'g', 'G', 0, },
	[0x23]	=	{ 	'h', 'H', 0, },
	[0x24]	=	{ 	'j', 'J', 0, },
	[0x25]	=	{ 	'k', 'K', 0, },
	[0x26]	=	{ 	'l', 'L', 0, },
	[0x27]	=	{ 	';', ':', 0, },
	[0x28]	=	{ 	'\'', '"', 0, },
	[0x29]	=	{ 	'`', '`', 0, },
	[0x2b]	=	{ 	'\\', '|', 0, },
	[0x2c]	=	{ 	'z', 'Z', 0, },
	[0x2d]	=	{ 	'x', 'X', 0, },
	[0x2e]	=	{ 	'c', 'C', 0, },
	[0x2f]	=	{ 	'v', 'V', 0, },
	[0x30]	=	{ 	'b', 'B', 0, },
	[0x31]	=	{ 	'n', 'N', 0, },
	[0x32]	=	{ 	'm', 'M', 0, },
	[0x33]	=	{ 	',', '<', 0, },
	[0x34]	=	{ 	'.', '>', 0, },
	[0x35]	=	{ 	'/', '?', 0, },


	[0x39]	=	{ 	' ', ' ', 0, },
	[0x0e]	=	{ 	0, 0, handle_backspace, },
	[0x1c]	=	{ 	0, 0, handle_enter, },
	[0x2a]	=	{ 	0, 0, handle_shift, handle_shift, },
	[0x36]	=	{ 	0, 0, handle_shift, handle_shift, },
	[0x1d]	=	{ 	0, 0, handle_control, handle_control, },
	
};

void console_character_input(int c)
{
int i, j;

	i = video_console.cursor_row;
	j = video_console.cursor_column;

	(* video_console.raw_video_memory)[i][j].character
		= video_console.video_memory[i][j].character
			= c;
	if (++ j == CONSOLE_COLUMNS)
		j --;
	do_hide_cursor();
	video_console.cursor_column = j;
	do_draw_cursor();
}

int translate_scancode(int scancode)
{
uint8_t c;
struct keyboard_scancode_entry e;

	scancode &= 255;
	e = translation_table[scancode & 0x7f];
	if (!(scancode & 0x80))
	{
		/* make code */
		if (e.make_handler)
			c = e.make_handler();
		else
			c = console_state.shift_active ? e.shift_sym : e.sym;
		if (c)
			console_character_input(c);
	}
	else
	{
		/* break code */
		if (e.break_handler)
			c = e.break_handler();
	}
	return c ? c : '?';
}

void init_console(void)
{
int i, j;

	video_console.raw_video_memory = (void *) 0xb8000;
	for (i = 0; i < CONSOLE_ROWS; i ++)
		for (j = 0; j < CONSOLE_COLUMNS; j ++)
		{
			(* video_console.raw_video_memory)[i][j].character
				= video_console.video_memory[i][j].character
					= ' ';
			(* video_console.raw_video_memory)[i][j].attributes
				= video_console.video_memory[i][j].attributes
					= CHARACTER_ATTRIBUTE_NORMAL;
		}
}

/*! \todo	this sucks... */
void user_putchar(int c)
{
unsigned irqflag = get_irq_flag_and_disable_irqs();

	c == '\n' ? (put_enter(), video_console.cursor_lock_position = 0)
		: (console_character_input(c), video_console.cursor_lock_position = video_console.cursor_column);
	restore_irq_flag(irqflag);
}
/*
int user_getchar(void)
{
static int idx = 0;
static const char cmd[] = "12 12 * . cr .( hello, world!) cr";

	return (idx < xstrlen(cmd)) ? cmd[idx ++] : -1;

}
*/

int user_getchar(void)
{
	return console_ring_buffer_pull();
}

