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

\ some optional 'facility' words for working with data structures
\ todo : maybe move these to the sforth engine


: begin-structure ( 'name' -- xxx )
	create here ( allocate space for size) 0 , ( initialize field offset) 0
	does> ( allocate and initialize space for the data structure)
		." creating structure definition"cr create @ here over allot 0 rot fill
	;
: +field ( xxx -- xxx )
	create over , + does> @ +
	;
: field: ( xxx -- xxx )
	aligned 1 cells +field
	;
: cfield ( xxx -- xxx )
	1 chars +field
	;
: end-structure ( xxx --)
	( store data structure size) swap !
	;
\ data structure operation test drive
begin-structure struct
	cfield		.a
	2 +field	.b
	field:		.c
end-structure
struct str
: struct-testdrive
	 .( run 'struct-testdrive' for data structure operation test drive)[ cr ]
		s" struct str-td str-td str-td .a str-td .b str-td .c .s abort"
		evaluate
	;



: hw@ ( address -- halfword)
	@ $ffff and ;

\ align 'buf' on a 16 byte boundary, for prettier dumps
16 here 15 and - 15 and allot
here 512 allot constant buf

: id ( -- t=success|f=failure) buf ata-identify-drive dup if
	." drive identified successfully" else ." COULD NOT IDENTIFY DRIVE" then cr ;
: rs ( sector-number -- t=success|f=failure) buf swap ata-28lba-read-sector dup if
	." sector read successfully" else ." COULD NOT READ SECTOR" then cr ;
: ws ( sector-number -- t=success|f=failure) buf swap ata-28lba-write-sector dup if
	." sector written successfully" else ." COULD NOT WRITE SECTOR" then cr ;

: ds buf 512 dump ;

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

.( use 'ds' to dump the disk buffer)cr
cr
.( the arena currently occupies ) here swap - decimal . .( bytes) cr cr

base !

\ this value was obtained by reading the BAR0 pci register of the virtualbox
\ ohci device
$f0804000 constant ohci-physical-mem-base
phys-mem-window-base constant ohci
ohci-physical-mem-base phys-mem-map

: ohci-dump ( --)
	\ print ohci connection status
	ohci $48 + @ $ff and 0 do
		." port "i . ." status: "
		ohci $54 + i cells + @ 1 and 0<> if ." connected" else ." not connected" then cr
	loop
;

cr .( ***********************************)
cr .( virtualbox OHCI test drive words)
cr .( ***********************************)cr

\ allocata OHCI HCCA area (256 bytes)
0 value OHCI-HCCA
\ the ohci hcca must be 256 byte-aligned
\ align
here negate $ff and allot
\ allocate hcca
here to OHCI-HCCA 256 allot
OHCI-HCCA 256 0 fill

: ohci-init ( --)
	\ sanity checks
	ohci HcControl + @ dup IR and 0<> abort" fatal: refusing to initialize ohci controller"
	HCFS-get HCFS-UsbReset <> abort" fatal: bad initial ohci state"
	\ initialize; refer to section 5.1.1.4 - 'Setup Host Controller'
	\ in the USB OHCI document
	\ save HcFmInterval
	ohci HcFmInterval + @
	HCR ohci HcCommandStatus + !
	\ wait for reset to complete
	begin ohci HcCommandStatus + @ HCR and 0= until
	\ make sure the host controller is now in a 'suspend' state
	ohci HcControl + @ HCFS-get HCFS-UsbSuspend <> abort" fatal: failed to reset ohci controller"
	\ restore HcFmInterval
	ohci HcFmInterval + !
	$3e67 ohci HcPeriodicStart + !
	\ The 'UsbOperational' state should be entered within 2 ms
	\ following reset completion, because the bus will then be in
	\ a 'UsbSuspend' state, and if the bus stays more than 3 ms in
	\ a 'UsbSuspend' state, then the bus will enter a 'Suspend' state.
	\ If the USB is in a 'Suspend' state, it needs to go through a
	\ USB 'Resume' state to wake up devices on the bus, and only then
	\ can enter a USB 'Operational' state. There is really no need
	\ for these complications on OHCI initialization, so make sure
	\ that the OHCI controller only stays for a short time (less than
	\ 2 ms) in the 'UsbSuspend' state
	ohci HcControl + dup @ HCFS-UsbOperational HCFS-set swap !
	." USB OHCI initialization complete"cr cr
	;
.( this is console number ) active-process . cr

