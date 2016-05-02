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

( pio ata driver)

\ this is qute messed up because of legacy considerations
\ i don't understand all of this right now (and i am not sure i want to)
\ but it basically seems to boil down to controlling legacy circuitries
\ designed for low pin count to achieve lower costs in the old days...

base @ decimal

here
.( compiling ata pio mode driver...) cr

512 constant ATA-SECTOR-BYTESIZE
2 constant ATA-WORD-BYTESIZE

$1f0 value ATA-PORT-BASE
$3f6 value DEVICE-CONTROL-REGISTER
: ALTERNATE-STATUS-REGISTER DEVICE-CONTROL-REGISTER ;

\ slave/master selection bit; must be used
\ when writing to the DRIVE-HEAD-PORT i/o port
4 bit constant SLAVE-SELECTION-FLAG
\ ata command codes
\ write these to the COMMAND-PORT
$ec constant ATA-COMMAND-IDENTIFY
$20 constant ATA-COMMAND-READ-SECTORS
$30 constant ATA-COMMAND-WRITE-SECTORS
$e7 constant ATA-COMMAND-CACHE-FLUSH

: DATA-PORT ( -- port-number)
	ATA-PORT-BASE 0 + ;
: SECTOR-COUNT-PORT ( -- port-number)
	ATA-PORT-BASE 2 + ;
: LBA-LO-PORT ( -- port-number)
	ATA-PORT-BASE 3 + ;
: LBA-MID-PORT ( -- port-number)
	ATA-PORT-BASE 4 + ;
: LBA-HI-PORT ( -- port-number)
	ATA-PORT-BASE 5 + ;
: DRIVE-HEAD-PORT ( -- port-number)
	ATA-PORT-BASE 6 + ;
: COMMAND-PORT ( -- port-number)
	ATA-PORT-BASE 7 + ;
: STATUS-PORT ( -- port-number)
	ATA-PORT-BASE 7 + ;
\ status byte bit definitions
0 bit constant ERR \ indicates an error occurred
		\ cleared by sending a new command or a software reset
3 bit constant DRQ \ data request; set when the drive has PIO data
		\ to transfer, or is ready to accept PIO data
4 bit constant SRV \ overlapped mode service request (whatever that means)
5 bit constant xDF \ drive fault error; does not set ERR
6 bit constant RDY \ bit is clear when drive is spun down, or after an error
		\ set otherwise
7 bit constant BSY \ indicates the drive is preparing to send/receice data
		\ (wait for it to clear); in case of a 'hang' (it never
		\ clears), do a software reset
: ata-select-lba ( lba-index --)
	dup LBA-LO-PORT outpb
	dup 8 rshift LBA-MID-PORT outpb
	16 rshift LBA-HI-PORT outpb
	;

: ata-wait-device-ready ( -- t=success|f=error)
\ (waiting for the drive to be ready to transfer data):
\ Read the Regular Status port until bit 7 (BSY, value = 0x80) clears,
\ and bit 3 (DRQ, value = 8) sets -- or until bit 0 (ERR, value = 1)
\ or bit 5 (DF, value = 0x20) sets.
\ If neither error bit is set, the device is ready right then. 
	begin
		STATUS-PORT inpb
		>r
		r@ BSY and 0= r@ DRQ and 0<> and
		r> ERR xDF or and 0<>
		or
	until
	\ set return code
	STATUS-PORT inpb ERR xDF or and 0=
	;

: ata-read-data-words ( data-buffer word-count -- )
	0 do
		DATA-PORT inpw
		2dup swap c!
		8 rshift over 1+ c!
		2 +
	loop drop
	;

: ata-write-data-words ( data-buffer word-count -- )
	0 do
		count swap count 8 lshift rot or
		DATA-PORT outpw
	loop drop
	;

: ata-identify-drive ( data-buffer -- t=success|f=error)
	\ todo: make sure what the $a0 below really does...
	$a0 DRIVE-HEAD-PORT outpb
	0 SECTOR-COUNT-PORT outpb
	0 ata-select-lba
	ATA-COMMAND-IDENTIFY COMMAND-PORT outpb
	ata-wait-device-ready false = if ." device error" cr drop false exit then
	ATA-SECTOR-BYTESIZE ATA-WORD-BYTESIZE / ata-read-data-words
	true
	;

: ata-28lba-read-sector ( data-buffer lba-sector-nr -- t=success|f=error)
	\ todo: make sure what the $e0 below really does...
	$e0 over 24 rshift $f and or DRIVE-HEAD-PORT outpb
	1 SECTOR-COUNT-PORT outpb
	ata-select-lba
	ATA-COMMAND-READ-SECTORS COMMAND-PORT outpb
	ata-wait-device-ready false = if ." device error" cr drop false exit then
	ATA-SECTOR-BYTESIZE ATA-WORD-BYTESIZE / ata-read-data-words
	true
	;

: ata-28lba-write-sector ( data-buffer lba-sector-nr -- t=success|f=error)
	\ todo: make sure what the $e0 below really does...
	$e0 over 24 rshift $f and or DRIVE-HEAD-PORT outpb
	1 SECTOR-COUNT-PORT outpb
	ata-select-lba
	ATA-COMMAND-WRITE-SECTORS COMMAND-PORT outpb
	ata-wait-device-ready false = if ." device error" cr drop false exit then
	." writing data..." cr
	ATA-SECTOR-BYTESIZE ATA-WORD-BYTESIZE / ata-write-data-words
	\ send an ata 'cache flush' command
	\ todo: check if this is correct (i don't have documentation and network access now)
	ATA-COMMAND-CACHE-FLUSH COMMAND-PORT outpb
	true
	;

.( selecting first master ata drive) cr
$e0 DRIVE-HEAD-PORT outpb
.( drive status: $) base @ hex STATUS-PORT inpb . base ! cr

here swap -
.( ata pio mode driver compiled: ) . .( bytes used) cr
base !

