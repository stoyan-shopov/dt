\ vga-specific facilities

: translate-vga-table-glyph ( glyph-ascii-code -- translated-glyph-ascii-code)
	case
		[char] | of 179 endof
		[char] - of 196 endof
		[char] + of 197 endof
		[char] ^ of 193 endof
		[char] ] of 180 endof
		( ." warning: character not translated!!!"cr)
		dup
	endcase
	;
: print-table-glyphs ( c-addr count --)
	0 ?do 
		count translate-vga-table-glyph emit
	loop drop
	;

