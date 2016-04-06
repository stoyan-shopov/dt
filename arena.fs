\ Copyright (c) 2014-2016 stoyan shopov
\ 
\ Permission is hereby granted, free of charge, to any person obtaining a copy
\ of this software and associated documentation files (the "Software"), to deal
\ in the Software without restriction, including without limitation the rights
\ to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
\ copies of the Software, and to permit persons to whom the Software is
\ furnished to do so, subject to the following conditions:
\ 
\ The above copyright notice and this permission notice shall be included in
\ all copies or substantial portions of the Software.
\ 
\ THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
\ IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
\ FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
\ AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
\ LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
\ OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
\ THE SOFTWARE.

base @ here decimal
cr .( welcome to the death track sforth arena) cr
.( remember to regularly clean the arena when you are done hacking on something) cr
.( the arena is your friend; be nice and treat it friendly) cr

variable buf 512 allot
: id ( -- t=success|f=failure) buf ata-identify-drive if
	." drive identified successfully" else ." COULD NOT IDENTIFY DRIVE" then cr ;
: rs ( buffer sector -- t=success|f=failure) buf swap ata-28lba-read-sector if
	." sector read successfully" else ." COULD NOT READ SECTOR" then cr ;

: dl buf 256 dump ;
: dh buf 256 + 256 dump ;

\ video memory base address
$b8000 constant vmem-base
\ video rows
25 constant vrows
\ video columns
80 constant vcols

: colors ( --)
	16 0 do
		\ establish video base address
		vmem-base i 4 + 2* vcols * +
		16 0 do
			\ compute address of video character attribute
			i 2 * over + 1+ 16 +
			\ store a test character
			[char] * over 1- c!
			\ compute video attribute, and store it to video memory
			j 4 lshift i or swap c!
		loop drop
	loop
	;

: glyphs ( --)
	16 0 do
		\ establish video base address
		vmem-base i 4 + 2* vcols * +
		16 0 do
			\ compute address of video character attribute
			i 2 * over + 1+ 16 +
			\ store current glyph
			j 16 * i + over 1- c!
			7 swap c!
		loop drop
	loop
	;

.( use 'dl' to dump the lower half of the disk buffer, 'dh', for the second one)cr



.( the arena currently occupies ) here swap - decimal . .( bytes) cr cr

base !
