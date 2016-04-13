MEMORY
{
	rom (rx)	:	ORIGIN = 0x50000, LENGTH = 0xa0000 - 0x50000
	ram (rwx)	:	ORIGIN = 0x100000, LENGTH = 0x100000
}

SECTIONS
{
	.text		: { *(.text) } > rom
	.	=	ALIGN(8);
	.init_startup : 
	{
		_init_startup		= .;
		*(.ctors)
		_init_startup_end	= .;
	} > rom
	.	=	ALIGN(8);
	.x86_idt	: { *(.x86_idt) } > rom
	.	=	ALIGN(8);
	.rodata		: {
		*(.rodata)
		.	=	ALIGN(8);
		_idata_contents_start	= .;
	} > rom

	.init_pgdir_tab	:
	{
		.	=	ALIGN(4096);
		*(.init_pgdir)
	} > ram

	.common-data	:
	{
		.	=	ALIGN(4096);
		*(.common-data)
	} > ram

	.data	:
	{
		.	=	ALIGN(4096);
		_data_start	= .;
		*(.data)
		_data_end	= .;
	} > ram AT> rom

	.	=	ALIGN(8);
	.bss :
	{
		_bss_start = . ;
		*(.bss .bss*)
		_bss_end = . ;
	} > ram

	/DISCARD/ : 
	{
			*(.eh_frame*)
			*(.drectve*)
	}
}
