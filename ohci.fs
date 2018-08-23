\ initial experimental usb ohci support
\ consult document 'Open Host Controller Interface Specification for USB'
\ release 1.0a; no official link to download this document was found;
\ in search results, these files were found: 'open_hci_1.pdf', 'hcir1_0a.pdf'


\ usb ohci operational registers offsets in the ohci memory-mapped
\ area; the address of this memory mapped area is in the BAR0 (base address 0)
\ register in the pci register space for the ohci controller present in a system;
\ a usb ohci controller has a pci base class $C (serial bus controller), and
\ a subclass 3, and an interface $10
$0	constant HcRevision
$4	constant HcControl
\ fields in the HcControl register
	2 bit constant PLE \ periodic list enable
	3 bit constant PLE \ isochronous enable
	4 bit constant CLE \ control list enable
	5 bit constant BLE \ bulk list enable
	\ hc usb functional state - a 2 bit field
	\ use word HCFS-get ( HcControl-value -- hcfs) to get the state
	\ use word HCFS-set ( HcControl-value new-hcfs-value -- new-HcControl-value)
	\ to update the value
	0 constant HCFS-UsbReset
	1 constant HCFS-UsbResume
	2 constant HCFS-UsbOperational
	3 constant HCFS-UsbSuspend
	: HCFS-get ( HcControl-value -- hcfs)
		6 rshift 3 and ;
	: HCFS-set ( HcControl-value new-hcfs-value -- new-HcControl-value)
		swap [ 3 6 lshift invert ] literal and swap
		3 and 6 lshift or ;
	8 bit constant IR \ interrupt routing

$8	constant HcCommandStatus \ this is a 'write-1-to-set, zeroes-ignored' register
\ fields in the HcCommandStatus register
	0 bit constant HCR \ hc reset
	1 bit constant CLF \ control list filled
	2 bit constant BLF \ bulk list filled
	3 bit constant OCR \ ownership change reguest
$C	constant HcInterruptStatus
\ fields in the HcInterruptStatus register
	0 bit constant SO \ scheduling overrun
	1 bit constant WDH \ writeback done for done queue head in the HCCA area
	2 bit constant SF \ start of frame
	3 bit constant RD \ resume detected
	4 bit constant UE \ unrecoverable error
	5 bit constant FNO \ frame number overflow
	6 bit constant RHSC \ root hub status change
	30 bit constant OC \ ownership change

$10	constant HcInterruptEnable
\ fields in the HcInterruptEnable register
	31 bit constant MIE \ master interrupt enable
$14	constant HcInterruptDisable
\ fields in the HcInterruptDisable register
	\ 31 bit constant MIE - same as the MIE bit in the HcInterruptEnable register
$18	constant HcHCCA \ hc communications area, alignment is 256 bytes
$1C	constant HcPeriodCurrentED
$20	constant HcControlHeadED \ alignment is 16 bytes
$24	constant HcControlCurrentED
$28	constant HcBulkHeadED \ alignment is 16 bytes
$2C	constant HcBulkCurrentED
$30	constant HcDoneHead
$34	constant HcFmInterval
$38	constant HcFmRemaining
$3C	constant HcFmNumber
$40	constant HcPeriodicStart
$44	constant HcLSThreshold
$48	constant HcRhDescriptorA
$4c	constant HcRhDescriptorB
$50	constant HcRhStatus
$54	constant HcRhPortStatus

$54	constant HcRhPortStatus[1]
\ fields in the HcRhPortStatus[xxx] register
	0 bit constant CCS	\ current connection status
	1 bit constant PES	\ port enable status
	2 bit constant PSS	\ port suspend status
	3 bit constant POCI	\ port overcurrent indicator
	4 bit constant PRS	\ port reset status

	8 bit constant PPS	\ port power status
	9 bit constant LSDA	\ low-speed device attached

	16 bit constant CSC	\ conneciton status change
	17 bit constant PESC	\ port enable status change
	18 bit constant PSSC	\ port suspend status change
	19 bit constant OCIC	\ port overcurrent indication change
	20 bit constant PRSC	\ current connection status
