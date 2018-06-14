\ console printing facilities

\ right-aligned text printing
:noname ( c-addr count alignment --)
\ right aligned printing
	2dup < invert if drop type exit then
	over - spaces type
	;
: r" ( count chars"... --)
	literal
	state @ 0= if
		\ interpreting
		>r postpone s" rot r> execute
	else
		\ compiling
		postpone s" postpone rot postpone literal postpone execute
	then ; immediate

