\ fat filesystem support

\ all sector numbers are relative to the mounted partition and must be offset
\ by the partition's first sector number - this number is in 'mount.first-lba-sector'
0 value fatfs.sectors-per-cluster
0 value fatfs.first-fat-start-sector
0 value fatfs.second-fat-start-sector
0 value fatfs.direntries-count
0 value fatfs.total-sector-count
0 value fatfs.total-cluster-count
0 value fatfs.sectors-per-fat
0 value fatfs.sectors-per-cluster
0 value fatfs.first-data-sector
0 value fatfs.total-data-sectors
0 value fatfs.root-directory-start-sector

false value fatfs.?mounted

512 constant FAT-SECTOR-SIZE
\ fat16 end-of-clusterization mark
$fff8 constant FAT16-EOC-LOW-MARK
: fatfs.?eof ( cluster-number -- t|f)
	FAT16-EOC-LOW-MARK < invert
	;

$fff7 constant FAT16-BAD-CLUSTER
32 constant FATFS-DIRENTRY-SIZE

\ this buffer is only used to read sectors from the fat table
create fat-buffer FAT-SECTOR-SIZE allot
\ this is a generic buffer used to access the mounted fat partition
create fatfs-buffer FAT-SECTOR-SIZE allot


begin-structure fatfs.direntry

	11 +field	.fatfs.direntry.name
	cfield		.fatfs.direntry.attributes
		\ fat filesystem file attribute flags
		0 bit constant fatfs.file-attribute:read-only
		1 bit constant fatfs.file-attribute:hidden
		2 bit constant fatfs.file-attribute:system
		3 bit constant fatfs.file-attribute:volume-id
		4 bit constant fatfs.file-attribute:directory
		5 bit constant fatfs.file-attribute:archive
		\ this is a hack from microsoft
			fatfs.file-attribute:read-only
			fatfs.file-attribute:hidden
			fatfs.file-attribute:system
			fatfs.file-attribute:volume-id
			or or or
		constant fatfs.file-attribute:long-name
	cfield		.fatfs.direntry.nt-reserved
	cfield		.fatfs.direntry.creation-time-tenths
	2 +field	.fatfs.direntry.creation-time
	2 +field	.fatfs.direntry.creation-date
	2 +field	.fatfs.direntry.last-access-date
	2 +field	.fatfs.direntry.first-cluster-high
	2 +field	.fatfs.direntry.last-write-time
	2 +field	.fatfs.direntry.last-write-date
	2 +field	.fatfs.direntry.first-cluster-low
	4 +field	.fatfs.direntry.file-size

end-structure

\ current working directory data
0 value cwd.start-cluster
0 value cwd.current-cluster
0 value cwd.sector-in-cluster

: fatfs-chdir ( directory-cluster-number --)
	dup to cwd.start-cluster
	to cwd.current-cluster
	0 to cwd.sector-in-cluster
	;

: fatfs-dump-direntry ( direntry-buffer --)
	>r
	r@ 8 0 do count emit loop ." ." 3 0 do count emit loop drop
	." size " r@ .fatfs.direntry.file-size @ .
	."  " r@ .fatfs.direntry.attributes @
		dup fatfs.file-attribute:read-only and if ." R" then
		dup fatfs.file-attribute:hidden and if ." H" then
		dup fatfs.file-attribute:system and if ." S" then
		dup fatfs.file-attribute:volume-id and if ." V" then
		dup fatfs.file-attribute:archive and if ." A" then
		."  " fatfs.file-attribute:directory and if ." <dir> " then
	." first cluster: " r> .fatfs.direntry.first-cluster-low hw@ . cr
	;
: fatfs.?direntry-present ( direntry-buffer -- t|f)
	@ 0 over = over $e5 = or swap 5 = or invert
	;

: >absolute-fatfs-sector ( relative-sector-number -- absolute-sector-number)
	mount.first-lba-sector + ;
: fatfs.validate-cluster-number ( cluster-number --)
	dup 2 < swap fatfs.total-cluster-count 2 + < invert or abort" bad cluster number"
	;
: cluster>sector-number ( cluster-number -- start-sector-number)
	dup fatfs.validate-cluster-number
	2 - fatfs.sectors-per-cluster * fatfs.first-data-sector +
	;
: fatfs-read-sector ( buffer sector-number -- t:success|f:failure)
	\ sector-number is relative to the start of the partition mounted
	fatfs.?mounted false = if ." no fat partition mounted"cr false exit then
	>absolute-fatfs-sector ata-28lba-read-sector
	;
: >next-cluster ( cluster-number -- cluster-number)
	dup fatfs.validate-cluster-number
	\ compute sector and offset in the fat table of the given cluster
	2* dup FAT-SECTOR-SIZE / fatfs.first-fat-start-sector + fat-buffer swap fatfs-read-sector
	false = abort" cannot read sector"
	FAT-SECTOR-SIZE 1- and fat-buffer + hw@
	;

: >next-sector-in-cwd ( -- t:success|f:at end of directory cluster chain)
	cwd.sector-in-cluster
	cwd.sector-in-cluster 1+ fatfs.sectors-per-cluster over <> if to cwd.sector-in-cluster exit then
	0 to cwd.sector-in-cluster
	cwd.current-cluster >next-cluster dup fatfs.?eof if false exit then to cwd.current-cluster true
	;

: fatfs.directory-sector@ ( --)
	cwd.current-cluster cluster>sector-number cwd.sector-in-cluster +
	fatfs-buffer swap fatfs-read-sector false = abort" cannot read sector"
	;

: fatfs-mount ( partition-number -- t:success|f:failure)
	false to fatfs.?mounted
	mount
	mount.?partition-mounted false = if ." error: no partition mounted; "
		." mount a partition first with 'mount'"cr false exit then
	fatfs-buffer 0 >absolute-fatfs-sector ata-28lba-read-sector
	false = if ." error reading boot sector"cr false exit then

	\ sanity checks
	fatfs-buffer 510 + hw@ $aa55 <> if ." bad signature"cr false exit then
	fatfs-buffer 11 + hw@ FAT-SECTOR-SIZE <> if ." unsupported bytes per sector value"cr false exit then
	fatfs-buffer 16 + c@ 2 <> if ." unsupported number of file allocation tables"cr false exit then
	fatfs-buffer 22 + hw@ to fatfs.sectors-per-fat
	fatfs.sectors-per-fat 0= if ." unsupported sectors per fat table (fat32?)"cr false exit then
	fatfs-buffer 13 + c@ to fatfs.sectors-per-cluster
	\ make sure the number of sectors per cluster is a power of two
	fatfs.sectors-per-cluster dup 1- and 0<> fatfs.sectors-per-cluster 0= or
	if ." bad number of sectors per cluster (not a power of 2)"cr false exit then
	
	fatfs-buffer 19 + hw@ ?dup 0= if fatfs-buffer 32 + @ then to fatfs.total-sector-count
	fatfs-buffer 17 + hw@ to fatfs.direntries-count
	fatfs-buffer 14 + hw@ to fatfs.first-fat-start-sector
	fatfs.first-fat-start-sector fatfs.sectors-per-fat + to fatfs.second-fat-start-sector

	\ compute number of sectors in the root directory
	fatfs.direntries-count FATFS-DIRENTRY-SIZE * FAT-SECTOR-SIZE 1- + FAT-SECTOR-SIZE /
	fatfs.second-fat-start-sector fatfs.sectors-per-fat +
	dup to fatfs.root-directory-start-sector
	+ to fatfs.first-data-sector
	fatfs.total-sector-count fatfs.first-data-sector - to fatfs.total-data-sectors
	fatfs.total-data-sectors fatfs.sectors-per-cluster / to fatfs.total-cluster-count

	\ make sure this is a fat16 system; only fat16 is supported at the moment
	fatfs.total-cluster-count dup 4085 < swap 65525 < invert or
	if ." fat16 not detected! this is a currently unsupported fat filesystem"cr false exit then

	\ make sure the computed number of clusters fit in the fat table
	fatfs.total-cluster-count 2/ FAT-SECTOR-SIZE / fatfs.sectors-per-fat >
	if ." bad fat16 filesystem - number of computed clusters do not fit in the fat table"cr false exit then

	cr
	." fat16 filesystem detected" cr
	." total sectors in partition: " fatfs.total-sector-count . cr
	." number of clusters: " fatfs.total-cluster-count . cr
	cr
	true to fatfs.?mounted
	true
	;

\ test drive
0 fatfs-mount
[if]
	.( partition successfully mounted)cr
	fatfs-buffer 2 cluster>sector-number fatfs-read-sector drop
[then]
: read-cluster ( cluster-number)
	cluster>sector-number fatfs-buffer swap fatfs-read-sector drop
	;
: ls ( --)
	0
	begin
		fatfs.directory-sector@
		[ FAT-SECTOR-SIZE FATFS-DIRENTRY-SIZE / ] literal 0 do
			fatfs-buffer i FATFS-DIRENTRY-SIZE * +
			dup fatfs.?direntry-present if fatfs-dump-direntry else drop then
		1+ loop
	>next-sector-in-cwd until
	cr . ." sectors processed"cr
	;

