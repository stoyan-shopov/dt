SECTIONS
{
	.	=	0;
	.text		: { 
				kinit.o (.text)
			}
	.rdata		: { 
				kinit.o (.rdata)
				kinit.o (.rodata)
			}
	/DISCARD/ : 
	{
			*(.eh_frame*)
	}
}
