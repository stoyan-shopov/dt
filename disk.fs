.( reading MBR of drive 0...)cr

id
[if]
.( first ata drive identified successfully)cr

\ read and dump the master boot record
marker discard
: dump-mbr ( --)
	$1be \ offset of the first mbr entry
	4 0 do
		buf over + 4 + @ if
			\ valid entry found
			buf over + c@ $80 and if ." [boot] " then
			." starting sector: " buf over + 8 + @ .
			." ; total sectors: " buf over + 12 + @ dup .
			." ("
			512 * 1024 1024 * / . ." MB)"cr
		then
		16 +
	loop drop
	;
0 rs drop dump-mbr
discard

-1 value mount.partition-number
-1 value mount.first-lba-sector
-1 value mount.sector-count
false value mount.?partition-mounted

: >partition-table-entry ( partition-number -- partition-table-entry-address)
	16 * ( partition table offset in the mbr) $1be + buf +
	;

: ?partition-valid ( partition-number -- t=partition valid|f=partition invalid)
	\ note: it is expected that the master boot record is already
	\ present in the disk read buffer
	\ sanity checks
	dup 3 u> if drop ." invalid partition number"cr false exit then
	>partition-table-entry 4 + @ 0<>
	;

: mount ( partition-number --)
	0 rs false = if drop ." failed to read master boot record; aborting"cr exit then
	dup ?partition-valid false = if drop ." invalid partition entry requested"cr exit then
	dup to mount.partition-number
	>partition-table-entry
	dup 8 + @ to mount.first-lba-sector
	12 + @ to mount.sector-count
	true to mount.?partition-mounted
	." partition mounted successfully"cr
	;

[else]
.( failed to identify first ata drive)cr
[then]
