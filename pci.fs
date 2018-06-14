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

\ pci support words

\ pci constants
base @ decimal
here

255 constant MAX-PCIBUS
31 constant MAX-PCIDEV
7 constant MAX-PCIFUNC

\ x86 pci io ports
$cf8 constant IO-PCICFG
$cfc constant IO-PCIDATA

\ utility words
: make-pci-config-address ( bus-nr device-nr function-nr -- config-value)
	MAX-PCIFUNC and 8 lshift
	swap MAX-PCIDEV and 11 lshift or
	swap MAX-PCIBUS and 16 lshift or
	31 bit or
	;
: read-id ( bus-nr device-nr function-nr -- id-register)
	make-pci-config-address
	IO-PCICFG outpl
	IO-PCIDATA inpl
	;

: ?pci-dev-present ( bus-nr device-nr function-nr -- t=device found|f=device not found)
	read-id
	$ffffffff <>
	;

0 value pci-dev-total
0 value pci-bus-iterator-xt
false value abort-scanning

0 value current-bus-nr
0 value current-device-nr
0 value current-function-nr

: pci-scan ( -- iterator-xt )
\ execute iterator-xt for every pci device found
\ stack picture of iterator-xt ( -- t:abort scanning|f:continue scanning)
\ current device bus device and function are stored in the values above

	to pci-bus-iterator-xt
	false to abort-scanning
	base @ hex
	0 to pci-dev-total
	\ scans all pci buses for devices
	MAX-PCIBUS 1+ 0 do
		i to current-bus-nr
		MAX-PCIDEV 1+ 0 do
			i to current-device-nr
			MAX-PCIFUNC 1+ 0 do
				i to current-function-nr
				current-bus-nr current-device-nr current-function-nr ?pci-dev-present
				if
					pci-dev-total 1+ to pci-dev-total
					pci-bus-iterator-xt execute to abort-scanning
				then
				abort-scanning if leave then
			loop
			abort-scanning if leave then
		loop
		abort-scanning if leave then
	loop
	base !
	;
0 [if]

bus | dev | func | vendor id | device id
xxx | xxx | xxxx | xxxxxxxxx | xxxxxxxxx
0         1         2         3         4
01234567890123456789012345678901234567890
[then]

: vertical-delimiter ( --) [char] | emit space ;
: horizontal-delimiter ( __)
	." ----+-----+------+-----------+-----------+"cr ;
: pci-dump-device ( -- t:abort scanning|f:continue scanning)
	current-bus-nr 3 .r vertical-delimiter
	current-device-nr 3 .r vertical-delimiter
	current-function-nr 4 .r vertical-delimiter
	current-bus-nr current-device-nr current-function-nr read-id
	( vendor id: ) dup $ffff and 9 .r vertical-delimiter
	( device id: ) 16 rshift 9 .r vertical-delimiter cr
	horizontal-delimiter
	false
	;

: pci-list-devices ( --)
	." scanning all pci buses for devices..." cr cr
	." bus | dev | func | vendor id | device id"cr
	horizontal-delimiter
	[ ' pci-dump-device literal ] pci-scan
	cr ." a total of " pci-dev-total . ." pci devices found" cr
	;
0 value searched-vendor-id
0 value searched-product-id

\ locate a pci device by vendor and product ids
:noname ( -- t:abort scanning|f:continue scanning)
	current-bus-nr current-device-nr current-function-nr read-id
	dup $ffff and searched-vendor-id = swap
	16 rshift searched-product-id =
	and
	;

: pci-locate-device ( vendor-id product-id -- pci-device-base-io-address t|f)
	to searched-product-id to searched-vendor-id
	base @ hex
	." searching all pci buses for device with vendor-id:product-id: "
	searched-vendor-id . searched-product-id . ." ..."
	base !
	literal pci-scan
	abort-scanning if
		\ device found
		current-bus-nr current-device-nr current-function-nr make-pci-config-address
		true
		." device found"
	else
		false
		." device NOT found"
	then
	cr
	;
here swap - .( pci module loaded, ) . .( bytes used) cr
base !
