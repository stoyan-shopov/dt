\ fat filesystem support

0 value fatfs.sectors-per-cluster
0 value fatfs.first-fat-start-sector
0 value fatfs.second-fat-start-sector
0 value fatfs.direntries-count
0 value fatfs.total-sector-count
0 value fatfs.total-cluster-count
0 value fatfs.sectors-per-fat
0 value fatfs.first-data-sector

: fatfs-dump ( --)
	mount.?partition-mounted false = if ." error: no partition mounted; "
		." mount a partition first with 'mount'"cr exit then
	mount.first-lba-sector rs false = if ." error reading boot sector"cr exit then

	\ sanity checks
	buf 510 + hw@ $aa55 <> if ." bad signature"cr exit then
	buf 11 + hw@ $200 <> if ." unsupported bytes per sector value"cr exit then
	buf 16 + c@ 2 <> if ." unsupported number of file allocation tables"cr exit then
	buf 22 + hw@ to fatfs.sectors-per-fat
	fatfs.sectors-per-fat 0= if ." unsupported sectors per fat table (fat32?)"cr exit then
	
	buf 19 + hw@ ?dup 0= if buf 32 + @ then to fatfs.total-sector-count
	buf 17 + c@ to fatfs.direntries-count
	buf 14 + hw@ to fatfs.first-fat-start-sector
	fatfs.first-fat-start-sector fatfs.sectors-per-fat + to fatfs.second-fat-start-sector
	;

