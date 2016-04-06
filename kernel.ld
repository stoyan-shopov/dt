SECTIONS
{
	.	=	0x50000;
	.text		: { *(.text) }
	.init_startup : 
	{
		. = ALIGN(8);
		_init_startup		= .;
		*(.ctors)
		_init_startup_end	= .;
	}
	.	=	ALIGN(8);
	.x86_idt	: { *(.x86_idt) }
	.	=	ALIGN(8);
	.data		: { *(.data) }

	.bss :
	{
		. = ALIGN(8);
		__bss_start = . ;
		_bss_start = . ;
		*(.bss .bss*)
		_bss_end = . ;
		__bss_end = . ;
	}

	/DISCARD/ : 
	{
			*(.eh_frame*)
	}
}
