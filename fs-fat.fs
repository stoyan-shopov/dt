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

false value fatfs.?mounted

512 constant FAT-SECTOR-SIZE

: >absolute-fatfs-sector ( relative-sector-number -- absolute-sector-number)
	mount.first-lba-sector + ;

: fatfs-mount ( partition-number -- t:success|f:failure)
	false to fatfs.?mounted
	mount
	mount.?partition-mounted false = if ." error: no partition mounted; "
		." mount a partition first with 'mount'"cr false exit then
	mount.first-lba-sector rs false = if ." error reading boot sector"cr false exit then

	\ sanity checks
	buf 510 + hw@ $aa55 <> if ." bad signature"cr false exit then
	buf 11 + hw@ FAT-SECTOR-SIZE <> if ." unsupported bytes per sector value"cr false exit then
	buf 16 + c@ 2 <> if ." unsupported number of file allocation tables"cr false exit then
	buf 22 + hw@ to fatfs.sectors-per-fat
	fatfs.sectors-per-fat 0= if ." unsupported sectors per fat table (fat32?)"cr false exit then
	buf 13 + c@ to fatfs.sectors-per-cluster
	\ make sure the number of sectors per cluster is a power of two
	fatfs.sectors-per-cluster dup 1- and 0<> fatfs.sectors-per-cluster 0= or
	if ." bad number of sectors per cluster (not a power of 2)"cr false exit then
	
	buf 19 + hw@ ?dup 0= if buf 32 + @ then to fatfs.total-sector-count
	buf 17 + hw@ to fatfs.direntries-count
	buf 14 + hw@ to fatfs.first-fat-start-sector
	fatfs.first-fat-start-sector fatfs.sectors-per-fat + to fatfs.second-fat-start-sector

	\ compute number of sectors in the root directory
	fatfs.direntries-count 32 * FAT-SECTOR-SIZE 1- + FAT-SECTOR-SIZE /
	fatfs.second-fat-start-sector fatfs.sectors-per-fat +
	+ to fatfs.first-data-sector
	fatfs.total-sector-count fatfs.first-data-sector - to fatfs.total-data-sectors
	fatfs.total-data-sectors fatfs.sectors-per-cluster / to fatfs.total-cluster-count

	\ make sure this is a fat16 system; only fat16 is supported at the moment
	fatfs.total-cluster-count dup 4085 < swap 65525 < invert or
	if ." fat16 not detected! this is a currently unsupported fat filesystem"cr false exit then

	cr
	." fat16 filesystem detected" cr
	." total sectors in partition: " fatfs.total-sector-count . cr
	." number of clusters: " fatfs.total-cluster-count . cr
	cr
	true to fatfs.?mounted
	true
	;

