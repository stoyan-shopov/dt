MEMORY
{
	rom (rx)	:	ORIGIN = 0x100000, LENGTH = 256K
	ram (rwx)	:	ORIGIN = 0x100000 + 256K, LENGTH = 0x100000 - 256K
}

SECTIONS
{
	.text		: { *(.text) } > rom
	.init_startup ALIGN(8) : 
	{
		_init_startup		= .;
		*(.ctors)
		_init_startup_end	= .;
	} > rom
	.x86_idt ALIGN(8)	: { *(.x86_idt) } > rom
	.rodata ALIGN(8)	: {
		*(.rodata)
		_common_data_contents_start	= .;
	} > rom

	.init-pgdir-tab :
	{
		*(.init-pgdir)
	} > ram

	.common-data ALIGN(4096)	:
	{
		_common_data_start	= .;
		*(.common-data)
		_common_data_end	= .;
	} > ram AT> rom

	.data ALIGN(4096)	:
	{
		_data_start	= .;
		*(.data)
		_data_end	= .;
	} > ram AT> rom

	.bss ALIGN(1024) :
	{
		_bss_start = . ;
		*(.bss .bss*)
		_bss_end = . ;
	} > ram

	/DISCARD/ : 
	{
			*(.eh_frame*)
	}
}
