/*
Copyright (c) 2018 stoyan shopov

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <stdint.h>
#include "utils.h"
#include "engine.h"

/*
 * this value was obtained by reading the BAR0 pci register of the virtualbox
 * ohci device
 */
enum
{
#define SFORTH_OHCI_PHYSICAL_MEM_BASE	"$f0804000"
	OHCI_PHYSICAL_MEM_BASE	=	0xf0804000,
	/*! \todo	init this from the root hub status register */
	OHCI_NR_HUB_PORTS	=	12,
	OHCI_MAX_NR_HUB_PORTS	=	12,
	/* number of endpoint descriptor groups to statically allocate;
	 * each group contains 32 endpoint descriptors */
	OHCI_NR_ED_GROUPS,
};


struct ohci_registers
{
	uint32_t	HcRevision;
	uint32_t	HcControl;
	uint32_t	HcCommandStatus;
	uint32_t	HcInterruptStatus;
	uint32_t	HcInterruptEnable;
	uint32_t	HcInterruptDisable;
	uint32_t	HcHCCA;
	uint32_t	HcPeriodCurrentED;
	uint32_t	HcControlHeadED;
	uint32_t	HcControlCurrentED;
	uint32_t	HcBulkHeadED;
	uint32_t	HcBulkCurrentED;
	uint32_t	HcDoneHead;
	uint32_t	HcFmInterval;
	uint32_t	HcFmRemaining;
	uint32_t	HcFmNumber;
	uint32_t	HcPeriodicStart;
	uint32_t	HcLSThreshold;
	uint32_t	HcRhDescriptorA;
	uint32_t	HcRhDescriptorB;
	uint32_t	HcRhStatus;
	uint32_t	HcRhPortStatus[OHCI_MAX_NR_HUB_PORTS];
};

/* ohci transfer descriptor */
struct ohci_td
{	
	union
	{
		struct
		{
			uint32_t			:	18;
			uint32_t	buffer_rounding	:	1;
			/* direction/pid values:
			 * %00 - SETUP
			 * %01 - OUT
			 * %10 - IN
			 * %11 - reserved */
			uint32_t	direction_pid	:	2;
			uint32_t	delay_interrupt	:	3;
			uint32_t	data_toggle	:	2;
			uint32_t	error_count	:	2;
			uint32_t	condition_code	:	2;
		};
		uint32_t	flags;
	};
	/* points to the first byte of the buffer;
	 * hardware sets this to zero when the transfer completes */
	void	* current_buffer;
	/* IMPORTANT: the last entry in the list is an empty/unused
	 * placeholder element; it seems what is stored in this
	 * element does not matter, as the documentation says that
	 * if the host controller (HC) sees that the endpoint descriptor
	 * head equals the endpoint descriptor tail, it decides
	 * that the list is empty */
	struct ohci_td * next;
	/* points to the last byte of the buffer */
	void	* buffer_end;
} __attribute__ ((aligned (16), packed));
static uint32_t bitmap_used_tds;
static volatile struct ohci_td tds[32];
static volatile struct ohci_td * allot_td(void)
{
int x;
	if ((x = find_first_clear(bitmap_used_tds)) == -1)
	{
		*(int*)0=0;
		return 0;
	}
	bitmap_used_tds |= 1 << x;
	return tds + x;
}
static void free_td(volatile struct ohci_td * td)
{
	bitmap_used_tds &=~ (1 << (td - tds));
}

/* ohci endpoint descriptor */
struct ohci_ed
{
	union
	{
		struct
		{
			uint32_t	function_address	:	7;
			uint32_t	endpoint_number		:	4;
			/* direction values:
			 * %00 - get direction from transfer descriptor (TD)
			 * %01 - OUT
			 * %10 - IN
			 * %11 - get direction from transfer descriptor (TD)
			 */
			uint32_t	direction		:	2;
			/* speed values:
			 * 0 - full speed
			 * 1 - low speed
			 */
			uint32_t	speed		:	1;
			/* if 1, the usb host controller skips this endpoint descriptor */
			uint32_t	skip		:	1;
			/* format of the descriptors linked to this ED:
			 * 0 - general transfer descriptor format - used for bulk, control, interrupt endpoints
			 * 1 - isochronous transfer descriptor	 
			 */
			uint32_t	format		:	1;
			uint32_t	max_packet_size	:	11;
		};
		uint32_t	flags;
	};
	/* below, if head == tail, then the list is empty */
	struct ohci_td	* tail;
	union
	{
		struct
		{
			/* set by hardware */
			uint32_t	halted		:	1;
			/* updated by hardware */
			uint32_t	toggle_carry	:	1;
		};
		struct ohci_td	* head;
	};
	/* this is 0 for the last entry in the list */
	struct ohci_ed	* next;
} __attribute__ ((aligned (16), packed));
static uint32_t bitmap_used_eds[OHCI_NR_ED_GROUPS];
static volatile struct ohci_ed eds[32 * OHCI_NR_ED_GROUPS];
static volatile struct ohci_ed * allot_ed(void)
{
int x, i;
struct ohci_ed * ed;
	for (i = 0; i < OHCI_NR_ED_GROUPS; i ++)
		if ((x = find_first_clear(bitmap_used_eds[i])) != -1)
			break;
	if (i == OHCI_NR_ED_GROUPS)
	{
		print_str("OUT OF ENDPOINT DESCRIPTORS");
		while (1)
			asm("hlt");
		return 0;
	}
	bitmap_used_eds[i] |= 1 << x;

	ed = eds + x + (i << 5);
	ed->flags = /* skip */ bit(14);
	ed->next = 0;
	ed->tail = 0;
	ed->head = 0;
	return ed;
}
static void free_ed(volatile struct ohci_ed * ed)
{
int i = ed - eds;
	bitmap_used_eds[i >> 5] &=~ (1 << (i & ((1 << 5) - 1)));
}

/* HCCA - host controller communications area */
struct ohci_hcca
{
	volatile struct ohci_ed	* interrupt_table[32];
	/* note: this field is 16 bit, the most significant 16 bits are set to zero by the hardware */
	uint32_t	frame_number;
	struct ohci_td	done_head;
	uint32_t	reserved[116 / 4];
} __attribute__ ((aligned (256), packed));
static volatile struct ohci_hcca ohci_hcca;

static volatile struct ohci_registers * ohci;

static uint8_t device_descriptor[18];
static const struct
{
	uint8_t set_address[8];
	uint8_t get_device_descriptor[8];
}
usb_request_packets =
{
	.set_address = { 0x00, 0x05, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, },
	.get_device_descriptor = { 0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x12, 0x00, },
};


void init_ohci(void)
{
volatile struct ohci_ed * ed = 0, * control_ed = 0;
volatile struct ohci_td * td = 0;
int i;

	/* enable usb ohci bus mastering */
	/*! \todo	this is specific for virtual box, fix it to be more generic */
	sf_eval("0 6 0 make-pci-config-address 4 + dup");
	sf_eval("IO-PCICFG outpl IO-PCIDATA inpw 6 ( memory space, and bus master) or swap");
	sf_eval("IO-PCICFG outpl IO-PCIDATA outpw");

	/* disable caching for the ohci data page */
	sf_push(((cell) & ohci_hcca) & ~ ((1 << 12) - 1));
	sf_eval("mem-page-disable-caching");

	sf_eval(SFORTH_OHCI_PHYSICAL_MEM_BASE " " "phys-mem-map phys-mem-window-base");
	ohci = (void *) sf_pop();
	if (ohci->HcRevision != 0x10)
	{
		print_str("bad usb ohci address\n");
		return;
	}
	print_str("detected usb ohci 1.0\n");
	ohci->HcInterruptDisable = 0xc000007f;
	/*! \todo only make sure here that the usb functional state is USBReset */ 
	if (ohci->HcControl != 0x200) 
	{
abort:		
		print_str("unexpected usb ohci register value, aborting\n");
		return;
	}
	ohci->HcCommandStatus = 1;
	while (ohci->HcCommandStatus & 1);
	ohci->HcFmInterval = 0x27792edf;
	ohci->HcPeriodicStart = 0x2a2f;
	/* initialize ohci hcca */
	ed = allot_ed();
	for (i = 0; i < 32; ohci_hcca.interrupt_table[i ++] = ed);

	ohci->HcHCCA = (uint32_t) & ohci_hcca;

	/* move to USBOperational state */
	ohci->HcControl = (ohci->HcControl & ~(3 << 6)) | (2 << 6);

	/* fill control list head */
	control_ed = allot_ed();
	control_ed->flags = (/* max packet size */ 0x40 << 16) | /* skip */ bit(14);
	control_ed->next = 0;
	control_ed->head = td = allot_td();

	/*
	   set address:

transfer descriptor ready:
hardware descriptor fields: flags: $f2000000; CBP: $004f59a0; NextTD: $007ab030; BE: $004f59a7; 
; #8 bytes in transfer:
0005010000000000
transfer descriptor ready:
hardware descriptor fields: flags: $f3100000; CBP: $00000000; NextTD: $007ab060; BE: $00000000; 
; #0 bytes in transfer:
*/
	/* setup stage of control transfer */
	td->flags = 0xf2000000;
	td->current_buffer = usb_request_packets.set_address;
	td->buffer_end = usb_request_packets.set_address + sizeof usb_request_packets.set_address - 1;
	td = td->next = allot_td();
	/* no data stage for control trasfer */
	{{{{{{{{{{{{{{{;;;;;;;;;;;;;;;;;}}}}}}}}}}}}}}}
	/* status stage of control trasfer */
	td->flags = 0xf3100000;
	td->current_buffer = td->buffer_end = 0;
	td->next = /* empty place holder transfer descriptor */ control_ed->tail = allot_td();
#if 0
	/* issue a 'read device descriptor' control transfer */
	/* control setup stage */
	td = td->next;
	td->flags = 0xf2000000;
	td->current_buffer = usb_request_packets.get_device_descriptor;
	td->buffer_end = usb_request_packets.get_device_descriptor + sizeof usb_request_packets.get_device_descriptor - 1;
	td = td->next = allot_td();
	/* control data IN stage */
	td->flags = 0xf3100000;
	td->current_buffer = device_descriptor;
	td->buffer_end = device_descriptor + sizeof device_descriptor - 1;
	td->next = allot_td();
	td = td->next;
	/* control handshake stage */
	td->flags = 0xf3080000;
	td->current_buffer = td->buffer_end = 0;
	td->next = /* empty place holder transfer descriptor */ control_ed->tail = allot_td();
#endif

	ohci->HcControlHeadED = control_ed;

	ohci->HcControl &=~ /* control list enable bit */ bit(4);
	ohci->HcControlCurrentED = 0;
	ohci->HcBulkCurrentED = 0;
	ohci->HcControl |= /* control list enable bit */ bit(4);

	if (ohci->HcRhDescriptorA != 0x20c) 
		goto abort;
	/* power on hub ports */
	ohci->HcRhStatus = 0x10000;

	/* try to drive the first port */
	if (!(ohci->HcRhPortStatus[0] & 1))
	{
		print_str("usb port 0 not connected, giving up\n");
		goto abort;
	}
	/* reset port 0 */
	ohci->HcRhPortStatus[0] = 1 << 4;
	while (!(ohci->HcRhPortStatus[0] & (1 << 20)))
		;
	ohci->HcRhPortStatus[0] = 0x001f0000;
	if (ohci->HcRhPortStatus[0] != 0x103)
		goto abort;
	control_ed->flags = 0x40 << 16;
	/* set CLF (control list filled flag) - issue a usb host controller transfer request */
	ohci->HcCommandStatus = bit(1);

	print_str("usb ohci initialization successful\n");

	print_str("waiting for set address transfer to complete...");
	while (control_ed->head != control_ed->tail)
		;
	print_str("done\n");
	if (ohci->HcCommandStatus & bit(1))
		goto abort;

	{
		struct ohci_td * t = ohci->HcDoneHead;
		while (t)
			print_str("retired transfer descriptor at: "), sf_push(t), sf_eval("base @ hex swap u. cr base !"), t = t->next;
	}

	///////xxxxxxxxxx??????????????
	ohci->HcInterruptStatus = ohci->HcInterruptStatus;
	ohci->HcControl &=~ /* control list enable bit */ bit(4);
	ohci->HcControlCurrentED = 0;
	/* set function address */
	control_ed->flags |= 1;

#if 1

	/* issue a 'read device descriptor' control transfer */
	/* control setup stage */
	td = td->next;
	control_ed->head = td;
	//////// redundant control_ed->head = td;
	td->flags = 0xf2000000;
	td->current_buffer = usb_request_packets.get_device_descriptor;
	td->buffer_end = usb_request_packets.get_device_descriptor + sizeof usb_request_packets.get_device_descriptor - 1;
	td = td->next = allot_td();
	/* control data IN stage */
	td->flags = 0xf3100000;
	td->current_buffer = device_descriptor;
	td->buffer_end = device_descriptor + sizeof device_descriptor - 1;
	td->next = allot_td();
	td = td->next;
	/* control handshake stage */
	td->flags = 0xf3080000;
	td->current_buffer = td->buffer_end = 0;
	td->next = /* empty place holder transfer descriptor */ control_ed->tail = allot_td();

	ohci->HcControl |= /* control list enable bit */ bit(4);
	/* set CLF (control list filled flag) - issue a usb host controller transfer request */
	ohci->HcCommandStatus = bit(1);

#endif

#if 1
	print_str("waiting for read device descriptor transfer to complete...");
	while (control_ed->head != control_ed->tail)
		;
	print_str("done\n");
#endif
	print_str("xxx???\n");
	sf_push((cell) device_descriptor);


/*
   read device descriptor:
transfer descriptor ready:
hardware descriptor fields: flags: $f2000000; CBP: $002dc724; NextTD: $007ab000; BE: $002dc72b; 
; #8 bytes in transfer:
8006000100001200
transfer descriptor ready:
hardware descriptor fields: flags: $f3100000; CBP: $002dc724; NextTD: $007ab030; BE: $002dc735; 
; #18 bytes in transfer:
800600010000120000000000000000000000
transfer descriptor ready:
hardware descriptor fields: flags: $f3080000; CBP: $00000000; NextTD: $007ab060; BE: $00000000; 
; #0 bytes in transfer:
*/
}

#if 0
struct ohci_registers
{
	uint32_t	HcRevision;
	uint32_t	HcControl;
	uint32_t	HcCommandStatus;
	uint32_t	HcInterruptStatus;
	uint32_t	HcInterruptEnable;
	uint32_t	HcInterruptDisable;
	uint32_t	HcHCCA;
	uint32_t	HcPeriodCurrentED;
	uint32_t	HcControlHeadED;
	uint32_t	HcControlCurrentED;
	uint32_t	HcBulkHeadED;
	uint32_t	HcBulkCurrentED;
	uint32_t	HcDoneHead;
	uint32_t	HcFmInterval;
	uint32_t	HcFmRemaining;
	uint32_t	HcFmNumber;
	uint32_t	HcPeriodicStart;
	uint32_t	HcLSThreshold;
	uint32_t	HcRhDescriptorA;
	uint32_t	HcRhDescriptorB;
	uint32_t	HcRhStatus;
	uint32_t	HcRhPortStatus;



----------------------------------------------------------------------------
----------------------------------------------------------------------------
----------------------------------------------------------------------------
KolibriOS usb log capture with no usb devices attached
----------------------------------------------------------------------------
----------------------------------------------------------------------------
----------------------------------------------------------------------------

[done] $c000007f ohci HcInterruptDisable + ! 		\ HcInterruptDisable: write : $c000007f: SO - disable scheduling overrun interrupt; WDH - disable DoneHead Writeback interrupt; SF - disable start of frame interrupt; RD - disable resume detect interrupt; UE - disable unrecoverable error interrupt; FNO - disable frame number overflow interrupt; RHSC - disable root hub status change interrupt; OC - disable ownership change interrupt; MIE - MASTER INTERRUPT DISABLE; 
[done] INIT start
[done] ohci HcControl + @ 		\ HcControl: read : $00000200: CBSR (control-to-bulk service ration): 0; PLE: periodic list disabled; IE: isochronous disabled; CLE: control list disabled; BLE: bulk list disabled; HCFS (host controller functional state): USBReset; IR (interrupt routing): none; RWC (remote wakeup connected): yes; RWE (remote wakeup enable): no; 
[done] $00000001 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000001: HCR (host controller reset): RESET ON; CLF (control list filled): no; BLF (bulk list filled): no; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
[done] ohci HcCommandStatus + @ 		\ HcCommandStatus: read : $00000000: HCR (host controller reset): 0; CLF (control list filled): no; BLF (bulk list filled): no; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
[done] $27792edf ohci HcFmInterval + ! 		\ HcFmInterval: write : $27792edf: FI (frame interval): 11999; FSMPS (largest data packet - bits): 10105; FIT (frame interval toggle): 0; 
[done] $00002a2f ohci HcPeriodicStart + ! 		\ HcPeriodicStart: write : $00002a2f: PS (periodic start): 10799; 
[done] $004f5000 ohci HcHCCA + ! 		\ HCCA: write : $004f5000: 
[done] HCCA load
[done] $00000080 ohci HcControl + ! 		\ HcControl: write : $00000080: CBSR (control-to-bulk service ration): 0; PLE: periodic list disabled; IE: isochronous disabled; CLE: control list disabled; BLE: bulk list disabled; HCFS (host controller functional state): USBOperational; IR (interrupt routing): none; RWC (remote wakeup connected): no; RWE (remote wakeup enable): no; 
[NOT DONE] $004f58e0 ohci HcControlHeadED + ! 		\ HcControlHeadED: write : $004f58e0: 
[NOT DONE] $004f5900 ohci HcBulkHeadED + ! 		\ HcBulkHeadED: write : $004f5900: 
[done] $00000000 ohci HcControlCurrentED + ! 		\ HcControlCurrentED: write : $00000000: 
[done] $00000000 ohci HcBulkCurrentED + ! 		\ HcBulkCurrentED: write : $00000000: 
[MODIFIED - skipped] $000000bc ohci HcControl + ! 		\ HcControl: write : $000000bc: CBSR (control-to-bulk service ration): 0; PLE: periodic list enabled; IE: isochronous enable; CLE: control list enabled; BLE: bulk list enabled; HCFS (host controller functional state): USBOperational; IR (interrupt routing): none; RWC (remote wakeup connected): no; RWE (remote wakeup enable): no; 
[done - check value] ohci HcRhDescriptorA + @ 		\ HcRhDescriptorA: read : $0000020c: NDP (number of downstream ports): 12; PSM (power switching mode): each port - individually; NPS (no power switching): ports are always powered when the HC is ON; DT (device type): hardwired to 0; OCPM (overcurrent protection mode): reported collectively; NOCP (no overcurrent protection): 0; POTPGT (power-on-to-power-good time; unit is *2 ms): 0; 
[NOT DONE] $80000042 ohci HcInterruptEnable + ! 		\ HcInterruptEnable: write : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
[done] $00010000 ohci HcRhStatus + ! 		\ HcRhStatus (see manual): write : $00010000: LPS (local power status): 0; OCI (overcurrent indicator): 0; DRWE (device remote wakeup enable): 0; LPSC (local power status change): 1; OCIC (overcurrent indicator change): 0; CRWE (clear remote wakeup enable): 0; 
$001f0101 ohci HcRhPortStatus 0 cells + + ! 		\ Root hub port status access, index 0: write : $001f0101: CCS: 1; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 1; PESC: 1; PSSC: 1; OCIC: 1; PRSC: 1; 
$001f0101 ohci HcRhPortStatus 1 cells + + ! 		\ Root hub port status access, index 1: write : $001f0101: CCS: 1; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 1; PESC: 1; PSSC: 1; OCIC: 1; PRSC: 1; 
$001f0101 ohci HcRhPortStatus 2 cells + + ! 		\ Root hub port status access, index 2: write : $001f0101: CCS: 1; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 1; PESC: 1; PSSC: 1; OCIC: 1; PRSC: 1; 
$001f0101 ohci HcRhPortStatus 3 cells + + ! 		\ Root hub port status access, index 3: write : $001f0101: CCS: 1; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 1; PESC: 1; PSSC: 1; OCIC: 1; PRSC: 1; 
$001f0101 ohci HcRhPortStatus 4 cells + + ! 		\ Root hub port status access, index 4: write : $001f0101: CCS: 1; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 1; PESC: 1; PSSC: 1; OCIC: 1; PRSC: 1; 
$001f0101 ohci HcRhPortStatus 5 cells + + ! 		\ Root hub port status access, index 5: write : $001f0101: CCS: 1; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 1; PESC: 1; PSSC: 1; OCIC: 1; PRSC: 1; 
$001f0101 ohci HcRhPortStatus 6 cells + + ! 		\ Root hub port status access, index 6: write : $001f0101: CCS: 1; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 1; PESC: 1; PSSC: 1; OCIC: 1; PRSC: 1; 
$001f0101 ohci HcRhPortStatus 7 cells + + ! 		\ Root hub port status access, index 7: write : $001f0101: CCS: 1; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 1; PESC: 1; PSSC: 1; OCIC: 1; PRSC: 1; 
$001f0101 ohci HcRhPortStatus 8 cells + + ! 		\ Root hub port status access, index 8: write : $001f0101: CCS: 1; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 1; PESC: 1; PSSC: 1; OCIC: 1; PRSC: 1; 
$001f0101 ohci HcRhPortStatus 9 cells + + ! 		\ Root hub port status access, index 9: write : $001f0101: CCS: 1; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 1; PESC: 1; PSSC: 1; OCIC: 1; PRSC: 1; 
$001f0101 ohci HcRhPortStatus 10 cells + + ! 		\ Root hub port status access, index 10: write : $001f0101: CCS: 1; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 1; PESC: 1; PSSC: 1; OCIC: 1; PRSC: 1; 
$001f0101 ohci HcRhPortStatus 11 cells + + ! 		\ Root hub port status access, index 11: write : $001f0101: CCS: 1; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 1; PESC: 1; PSSC: 1; OCIC: 1; PRSC: 1; 
ohci HcRhDescriptorA + @ 		\ HcRhDescriptorA: read : $0000020c: NDP (number of downstream ports): 12; PSM (power switching mode): each port - individually; NPS (no power switching): ports are always powered when the HC is ON; DT (device type): hardwired to 0; OCPM (overcurrent protection mode): reported collectively; NOCP (no overcurrent protection): 0; POTPGT (power-on-to-power-good time; unit is *2 ms): 0; 
ohci HcRhPortStatus 0 cells + + @ 		\ Root hub port status access, index 0: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 1 cells + + @ 		\ Root hub port status access, index 1: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 2 cells + + @ 		\ Root hub port status access, index 2: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 3 cells + + @ 		\ Root hub port status access, index 3: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 4 cells + + @ 		\ Root hub port status access, index 4: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 5 cells + + @ 		\ Root hub port status access, index 5: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 6 cells + + @ 		\ Root hub port status access, index 6: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 7 cells + + @ 		\ Root hub port status access, index 7: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 8 cells + + @ 		\ Root hub port status access, index 8: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 9 cells + + @ 		\ Root hub port status access, index 9: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 10 cells + + @ 		\ Root hub port status access, index 10: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 11 cells + + @ 		\ Root hub port status access, index 11: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
INIT end
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000004: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 0; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
IRQ exit





----------------------------------------------------------------------------
----------------------------------------------------------------------------
----------------------------------------------------------------------------
KolibriOS usb log capture with a mass storage usb device attached
----------------------------------------------------------------------------
----------------------------------------------------------------------------
----------------------------------------------------------------------------

$c000007f ohci HcInterruptDisable + ! 		\ HcInterruptDisable: write : $c000007f: SO - disable scheduling overrun interrupt; WDH - disable DoneHead Writeback interrupt; SF - disable start of frame interrupt; RD - disable resume detect interrupt; UE - disable unrecoverable error interrupt; FNO - disable frame number overflow interrupt; RHSC - disable root hub status change interrupt; OC - disable ownership change interrupt; MIE - MASTER INTERRUPT DISABLE; 
INIT start
ohci HcControl + @ 		\ HcControl: read : $00000200: CBSR (control-to-bulk service ration): 0; PLE: periodic list disabled; IE: isochronous disabled; CLE: control list disabled; BLE: bulk list disabled; HCFS (host controller functional state): USBReset; IR (interrupt routing): none; RWC (remote wakeup connected): yes; RWE (remote wakeup enable): no; 
$00000001 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000001: HCR (host controller reset): RESET ON; CLF (control list filled): no; BLF (bulk list filled): no; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
ohci HcCommandStatus + @ 		\ HcCommandStatus: read : $00000000: HCR (host controller reset): 0; CLF (control list filled): no; BLF (bulk list filled): no; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
$27792edf ohci HcFmInterval + ! 		\ HcFmInterval: write : $27792edf: FI (frame interval): 11999; FSMPS (largest data packet - bits): 10105; FIT (frame interval toggle): 0; 
$00002a2f ohci HcPeriodicStart + ! 		\ HcPeriodicStart: write : $00002a2f: PS (periodic start): 10799; 
$004f5000 ohci HcHCCA + ! 		\ HCCA: write : $004f5000: 
HCCA load
$00000080 ohci HcControl + ! 		\ HcControl: write : $00000080: CBSR (control-to-bulk service ration): 0; PLE: periodic list disabled; IE: isochronous disabled; CLE: control list disabled; BLE: bulk list disabled; HCFS (host controller functional state): USBOperational; IR (interrupt routing): none; RWC (remote wakeup connected): no; RWE (remote wakeup enable): no; 
$004f58e0 ohci HcControlHeadED + ! 		\ HcControlHeadED: write : $004f58e0: 
$004f5900 ohci HcBulkHeadED + ! 		\ HcBulkHeadED: write : $004f5900: 
$00000000 ohci HcControlCurrentED + ! 		\ HcControlCurrentED: write : $00000000: 
$00000000 ohci HcBulkCurrentED + ! 		\ HcBulkCurrentED: write : $00000000: 
$000000bc ohci HcControl + ! 		\ HcControl: write : $000000bc: CBSR (control-to-bulk service ration): 0; PLE: periodic list enabled; IE: isochronous enable; CLE: control list enabled; BLE: bulk list enabled; HCFS (host controller functional state): USBOperational; IR (interrupt routing): none; RWC (remote wakeup connected): no; RWE (remote wakeup enable): no; 
ohci HcRhDescriptorA + @ 		\ HcRhDescriptorA: read : $0000020c: NDP (number of downstream ports): 12; PSM (power switching mode): each port - individually; NPS (no power switching): ports are always powered when the HC is ON; DT (device type): hardwired to 0; OCPM (overcurrent protection mode): reported collectively; NOCP (no overcurrent protection): 0; POTPGT (power-on-to-power-good time; unit is *2 ms): 0; 
$80000042 ohci HcInterruptEnable + ! 		\ HcInterruptEnable: write : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00010000 ohci HcRhStatus + ! 		\ HcRhStatus (see manual): write : $00010000: LPS (local power status): 0; OCI (overcurrent indicator): 0; DRWE (device remote wakeup enable): 0; LPSC (local power status change): 1; OCIC (overcurrent indicator change): 0; CRWE (clear remote wakeup enable): 0; 
$001f0101 ohci HcRhPortStatus 0 cells + + ! 		\ Root hub port status access, index 0: write : $001f0101: CCS: 1; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 1; PESC: 1; PSSC: 1; OCIC: 1; PRSC: 1; 
$001f0101 ohci HcRhPortStatus 1 cells + + ! 		\ Root hub port status access, index 1: write : $001f0101: CCS: 1; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 1; PESC: 1; PSSC: 1; OCIC: 1; PRSC: 1; 
$001f0101 ohci HcRhPortStatus 2 cells + + ! 		\ Root hub port status access, index 2: write : $001f0101: CCS: 1; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 1; PESC: 1; PSSC: 1; OCIC: 1; PRSC: 1; 
$001f0101 ohci HcRhPortStatus 3 cells + + ! 		\ Root hub port status access, index 3: write : $001f0101: CCS: 1; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 1; PESC: 1; PSSC: 1; OCIC: 1; PRSC: 1; 
$001f0101 ohci HcRhPortStatus 4 cells + + ! 		\ Root hub port status access, index 4: write : $001f0101: CCS: 1; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 1; PESC: 1; PSSC: 1; OCIC: 1; PRSC: 1; 
$001f0101 ohci HcRhPortStatus 5 cells + + ! 		\ Root hub port status access, index 5: write : $001f0101: CCS: 1; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 1; PESC: 1; PSSC: 1; OCIC: 1; PRSC: 1; 
$001f0101 ohci HcRhPortStatus 6 cells + + ! 		\ Root hub port status access, index 6: write : $001f0101: CCS: 1; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 1; PESC: 1; PSSC: 1; OCIC: 1; PRSC: 1; 
$001f0101 ohci HcRhPortStatus 7 cells + + ! 		\ Root hub port status access, index 7: write : $001f0101: CCS: 1; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 1; PESC: 1; PSSC: 1; OCIC: 1; PRSC: 1; 
$001f0101 ohci HcRhPortStatus 8 cells + + ! 		\ Root hub port status access, index 8: write : $001f0101: CCS: 1; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 1; PESC: 1; PSSC: 1; OCIC: 1; PRSC: 1; 
$001f0101 ohci HcRhPortStatus 9 cells + + ! 		\ Root hub port status access, index 9: write : $001f0101: CCS: 1; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 1; PESC: 1; PSSC: 1; OCIC: 1; PRSC: 1; 
$001f0101 ohci HcRhPortStatus 10 cells + + ! 		\ Root hub port status access, index 10: write : $001f0101: CCS: 1; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 1; PESC: 1; PSSC: 1; OCIC: 1; PRSC: 1; 
$001f0101 ohci HcRhPortStatus 11 cells + + ! 		\ Root hub port status access, index 11: write : $001f0101: CCS: 1; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 1; PESC: 1; PSSC: 1; OCIC: 1; PRSC: 1; 
ohci HcRhDescriptorA + @ 		\ HcRhDescriptorA: read : $0000020c: NDP (number of downstream ports): 12; PSM (power switching mode): each port - individually; NPS (no power switching): ports are always powered when the HC is ON; DT (device type): hardwired to 0; OCPM (overcurrent protection mode): reported collectively; NOCP (no overcurrent protection): 0; POTPGT (power-on-to-power-good time; unit is *2 ms): 0; 
ohci HcRhPortStatus 0 cells + + @ 		\ Root hub port status access, index 0: read : $00000101: CCS: 1; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 1 cells + + @ 		\ Root hub port status access, index 1: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 2 cells + + @ 		\ Root hub port status access, index 2: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 3 cells + + @ 		\ Root hub port status access, index 3: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 4 cells + + @ 		\ Root hub port status access, index 4: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 5 cells + + @ 		\ Root hub port status access, index 5: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 6 cells + + @ 		\ Root hub port status access, index 6: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 7 cells + + @ 		\ Root hub port status access, index 7: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 8 cells + + @ 		\ Root hub port status access, index 8: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 9 cells + + @ 		\ Root hub port status access, index 9: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 10 cells + + @ 		\ Root hub port status access, index 10: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 11 cells + + @ 		\ Root hub port status access, index 11: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
INIT end
$00000010 ohci HcRhPortStatus 0 cells + + ! 		\ Root hub port status access, index 0: write : $00000010: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 1; PPS: 0; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000004: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 0; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
IRQ exit
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000044: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 0; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 1; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000040 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000040: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 0; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 1; OC (ownership change): 0; MIE (master interrupt enable): 0; 
root hub interrupt
ohci HcRhStatus + @ 		\ HcRhStatus (see manual): read : $00000000: LPS (local power status): 0; OCI (overcurrent indicator): 0; DRWE (device remote wakeup enable): 0; LPSC (local power status change): 0; OCIC (overcurrent indicator change): 0; CRWE (clear remote wakeup enable): 0; 
$80020000 ohci HcRhStatus + ! 		\ HcRhStatus (see manual): write : $80020000: LPS (local power status): 0; OCI (overcurrent indicator): 0; DRWE (device remote wakeup enable): 0; LPSC (local power status change): 0; OCIC (overcurrent indicator change): 1; CRWE (clear remote wakeup enable): 1; 
ohci HcRhPortStatus 0 cells + + @ 		\ Root hub port status access, index 0: read : $00100103: CCS: 1; PES: 1; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 1; 
$001f0000 ohci HcRhPortStatus 0 cells + + ! 		\ Root hub port status access, index 0: write : $001f0000: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 0; LSDA: 0; CSC: 1; PESC: 1; PSSC: 1; OCIC: 1; PRSC: 1; 
ohci HcRhPortStatus 0 cells + + @ 		\ Root hub port status access, index 0: read : $00000103: CCS: 1; PES: 1; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 1 cells + + @ 		\ Root hub port status access, index 1: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 2 cells + + @ 		\ Root hub port status access, index 2: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 3 cells + + @ 		\ Root hub port status access, index 3: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 4 cells + + @ 		\ Root hub port status access, index 4: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 5 cells + + @ 		\ Root hub port status access, index 5: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 6 cells + + @ 		\ Root hub port status access, index 6: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 7 cells + + @ 		\ Root hub port status access, index 7: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 8 cells + + @ 		\ Root hub port status access, index 8: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 9 cells + + @ 		\ Root hub port status access, index 9: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 10 cells + + @ 		\ Root hub port status access, index 10: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 11 cells + + @ 		\ Root hub port status access, index 11: read : $00000100: CCS: 0; PES: 0; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
IRQ exit
ohci HcRhPortStatus 0 cells + + @ 		\ Root hub port status access, index 0: read : $00000103: CCS: 1; PES: 1; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 
ohci HcRhPortStatus 0 cells + + @ 		\ Root hub port status access, index 0: read : $00000103: CCS: 1; PES: 1; PSS: 0; POCI: 0; PRS: 0; PPS: 1; LSDA: 0; CSC: 0; PESC: 0; PSSC: 0; OCIC: 0; PRSC: 0; 

transfer descriptor ready:
hardware descriptor fields: flags: $f2000000; CBP: $004f59a0; NextTD: $007ab030; BE: $004f59a7; 
; #8 bytes in transfer:
0005010000000000
transfer descriptor ready:
hardware descriptor fields: flags: $f3100000; CBP: $00000000; NextTD: $007ab060; BE: $00000000; 
; #0 bytes in transfer:

$00000002 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000002: HCR (host controller reset): 0; CLF (control list filled): yes; BLF (bulk list filled): no; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000006: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000002 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000002: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
interrupt transfer done
IRQ exit
$0000008c ohci HcControl + ! 		\ HcControl: write : $0000008c: CBSR (control-to-bulk service ration): 0; PLE: periodic list enabled; IE: isochronous enable; CLE: control list disabled; BLE: bulk list disabled; HCFS (host controller functional state): USBOperational; IR (interrupt routing): none; RWC (remote wakeup connected): no; RWE (remote wakeup enable): no; 
$00000004 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000004: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 0; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
$00000004 ohci HcInterruptEnable + ! 		\ HcInterruptEnable: write : $00000004: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): no change; SF (start of frame): ENABLE; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): no change; OC (ownership change): no change; MIE (master interrupt enable): no change; 
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000004: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 0; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000046: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): ENABLE; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000004 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000004: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 0; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
UNKNOWN PACKET: 84
$00000000 ohci HcControlCurrentED + ! 		\ HcControlCurrentED: write : $00000000: 
$00000000 ohci HcBulkCurrentED + ! 		\ HcBulkCurrentED: write : $00000000: 
$00000006 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000006: HCR (host controller reset): 0; CLF (control list filled): yes; BLF (bulk list filled): yes; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
$000000bc ohci HcControl + ! 		\ HcControl: write : $000000bc: CBSR (control-to-bulk service ration): 0; PLE: periodic list enabled; IE: isochronous enable; CLE: control list enabled; BLE: bulk list enabled; HCFS (host controller functional state): USBOperational; IR (interrupt routing): none; RWC (remote wakeup connected): no; RWE (remote wakeup enable): no; 
$00000004 ohci HcInterruptDisable + ! 		\ HcInterruptDisable: write : $00000004: SF - disable start of frame interrupt; 
IRQ exit
transfer descriptor ready:
hardware descriptor fields: flags: $f2000000; CBP: $002dc6d4; NextTD: $007ab030; BE: $002dc6db; 
; #8 bytes in transfer:
8006000100000800
transfer descriptor ready:
hardware descriptor fields: flags: $f3100000; CBP: $002dc6d4; NextTD: $007ab000; BE: $002dc6db; 
; #8 bytes in transfer:
8006000100000800
transfer descriptor ready:
hardware descriptor fields: flags: $f3080000; CBP: $00000000; NextTD: $007ab090; BE: $00000000; 
; #0 bytes in transfer:

$00000002 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000002: HCR (host controller reset): 0; CLF (control list filled): yes; BLF (bulk list filled): no; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000006: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000002 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000002: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
interrupt transfer done
IRQ exit
$0000008c ohci HcControl + ! 		\ HcControl: write : $0000008c: CBSR (control-to-bulk service ration): 0; PLE: periodic list enabled; IE: isochronous enable; CLE: control list disabled; BLE: bulk list disabled; HCFS (host controller functional state): USBOperational; IR (interrupt routing): none; RWC (remote wakeup connected): no; RWE (remote wakeup enable): no; 
$00000004 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000004: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 0; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
$00000004 ohci HcInterruptEnable + ! 		\ HcInterruptEnable: write : $00000004: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): no change; SF (start of frame): ENABLE; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): no change; OC (ownership change): no change; MIE (master interrupt enable): no change; 
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000004: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 0; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000046: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): ENABLE; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000004 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000004: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 0; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
UNKNOWN PACKET: 84
$00000000 ohci HcControlCurrentED + ! 		\ HcControlCurrentED: write : $00000000: 
$00000000 ohci HcBulkCurrentED + ! 		\ HcBulkCurrentED: write : $00000000: 
$00000006 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000006: HCR (host controller reset): 0; CLF (control list filled): yes; BLF (bulk list filled): yes; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
$000000bc ohci HcControl + ! 		\ HcControl: write : $000000bc: CBSR (control-to-bulk service ration): 0; PLE: periodic list enabled; IE: isochronous enable; CLE: control list enabled; BLE: bulk list enabled; HCFS (host controller functional state): USBOperational; IR (interrupt routing): none; RWC (remote wakeup connected): no; RWE (remote wakeup enable): no; 
$00000004 ohci HcInterruptDisable + ! 		\ HcInterruptDisable: write : $00000004: SF - disable start of frame interrupt; 
IRQ exit
transfer descriptor ready:
hardware descriptor fields: flags: $f2000000; CBP: $002dc724; NextTD: $007ab000; BE: $002dc72b; 
; #8 bytes in transfer:
8006000100001200
transfer descriptor ready:
hardware descriptor fields: flags: $f3100000; CBP: $002dc724; NextTD: $007ab030; BE: $002dc735; 
; #18 bytes in transfer:
800600010000120000000000000000000000
transfer descriptor ready:
hardware descriptor fields: flags: $f3080000; CBP: $00000000; NextTD: $007ab060; BE: $00000000; 
; #0 bytes in transfer:

$00000002 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000002: HCR (host controller reset): 0; CLF (control list filled): yes; BLF (bulk list filled): no; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000006: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000002 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000002: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
interrupt transfer done
IRQ exit
transfer descriptor ready:
hardware descriptor fields: flags: $f2000000; CBP: $002dc736; NextTD: $007ab000; BE: $002dc73d; 
; #8 bytes in transfer:
8006000200000800
transfer descriptor ready:
hardware descriptor fields: flags: $f3100000; CBP: $002dc736; NextTD: $007ab090; BE: $002dc73d; 
; #8 bytes in transfer:
8006000200000800
transfer descriptor ready:
hardware descriptor fields: flags: $f3080000; CBP: $00000000; NextTD: $007ab0c0; BE: $00000000; 
; #0 bytes in transfer:

$00000002 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000002: HCR (host controller reset): 0; CLF (control list filled): yes; BLF (bulk list filled): no; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000006: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000002 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000002: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
interrupt transfer done
IRQ exit
transfer descriptor ready:
hardware descriptor fields: flags: $f2000000; CBP: $002dc796; NextTD: $007ab000; BE: $002dc79d; 
; #8 bytes in transfer:
8006000200002000
transfer descriptor ready:
hardware descriptor fields: flags: $f3100000; CBP: $002dc796; NextTD: $007ab060; BE: $002dc7b5; 
; #32 bytes in transfer:
8006000200002000000000000000000000000000000000000000000000000000
transfer descriptor ready:
hardware descriptor fields: flags: $f3080000; CBP: $00000000; NextTD: $007ab030; BE: $00000000; 
; #0 bytes in transfer:

$00000002 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000002: HCR (host controller reset): 0; CLF (control list filled): yes; BLF (bulk list filled): no; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000006: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000002 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000002: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
interrupt transfer done
IRQ exit
transfer descriptor ready:
hardware descriptor fields: flags: $f2000000; CBP: $002dc7b8; NextTD: $007ab000; BE: $002dc7bf; 
; #8 bytes in transfer:
0009010000000000
transfer descriptor ready:
hardware descriptor fields: flags: $f3100000; CBP: $00000000; NextTD: $007ab0c0; BE: $00000000; 
; #0 bytes in transfer:

$00000002 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000002: HCR (host controller reset): 0; CLF (control list filled): yes; BLF (bulk list filled): no; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000006: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000002 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000002: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
interrupt transfer done
IRQ exit
transfer descriptor ready:
hardware descriptor fields: flags: $f2000000; CBP: $002dc7e0; NextTD: $007ab090; BE: $002dc7e7; 
; #8 bytes in transfer:
a1fe000000000100
transfer descriptor ready:
hardware descriptor fields: flags: $f3100000; CBP: $002dc7d4; NextTD: $007ab0f0; BE: $002dc7d4; 
; #1 bytes in transfer:
00
transfer descriptor ready:
hardware descriptor fields: flags: $f3080000; CBP: $00000000; NextTD: $007ab120; BE: $00000000; 
; #0 bytes in transfer:

$00000002 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000002: HCR (host controller reset): 0; CLF (control list filled): yes; BLF (bulk list filled): no; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000006: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000002 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000002: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
interrupt transfer done
IRQ exit
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000006: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000002 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000002: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
interrupt transfer done
IRQ exit
transfer descriptor ready:
hardware descriptor fields: flags: $f0000000; CBP: $002dc824; NextTD: $007ab090; BE: $002dc842; 
; #31 bytes in transfer:
55534243797878780000000080000c00000000000000000000000000000000
$00000004 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000004: HCR (host controller reset): 0; CLF (control list filled): no; BLF (bulk list filled): yes; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000006: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000002 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000002: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
interrupt transfer done
IRQ exit
transfer descriptor ready:
hardware descriptor fields: flags: $f0000000; CBP: $002dc844; NextTD: $007ab0f0; BE: $002dc850; 
; #13 bytes in transfer:
00000000000000000000000000
$00000004 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000004: HCR (host controller reset): 0; CLF (control list filled): no; BLF (bulk list filled): yes; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000006: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000002 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000002: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
interrupt transfer done
IRQ exit
transfer descriptor ready:
hardware descriptor fields: flags: $f0000000; CBP: $002dc824; NextTD: $007ab030; BE: $002dc842; 
; #31 bytes in transfer:
555342437a7878782400000080000c12000000240000000000000000000000
$00000004 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000004: HCR (host controller reset): 0; CLF (control list filled): no; BLF (bulk list filled): yes; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000006: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000002 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000002: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
interrupt transfer done
IRQ exit
transfer descriptor ready:
hardware descriptor fields: flags: $f0000000; CBP: $002dc800; NextTD: $007ab060; BE: $002dc823; 
; #36 bytes in transfer:
000000000000000000000000000000000000000000000000000000000000000000000000
$00000004 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000004: HCR (host controller reset): 0; CLF (control list filled): no; BLF (bulk list filled): yes; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
transfer descriptor ready:
hardware descriptor fields: flags: $f0000000; CBP: $002dc844; NextTD: $007ab0c0; BE: $002dc850; 
; #13 bytes in transfer:
55534253797878780000000000
$00000004 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000004: HCR (host controller reset): 0; CLF (control list filled): no; BLF (bulk list filled): yes; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000006: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000002 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000002: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
interrupt transfer done
IRQ exit
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000006: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000002 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000002: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
interrupt transfer done
IRQ exit
transfer descriptor ready:
hardware descriptor fields: flags: $f0000000; CBP: $002dc824; NextTD: $007ab060; BE: $002dc842; 
; #31 bytes in transfer:
555342437b7878780800000080000c25000000000000000000000000000000
$00000004 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000004: HCR (host controller reset): 0; CLF (control list filled): no; BLF (bulk list filled): yes; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000006: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000002 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000002: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
interrupt transfer done
IRQ exit
transfer descriptor ready:
hardware descriptor fields: flags: $f0000000; CBP: $007aef90; NextTD: $007ab0f0; BE: $007aef97; 
; #8 bytes in transfer:
0000000000000000
$00000004 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000004: HCR (host controller reset): 0; CLF (control list filled): no; BLF (bulk list filled): yes; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
transfer descriptor ready:
hardware descriptor fields: flags: $f0000000; CBP: $002dc844; NextTD: $007ab090; BE: $002dc850; 
; #13 bytes in transfer:
555342537a7878780000000000
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000006: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000002 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000002: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
interrupt transfer done
IRQ exit
$00000004 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000004: HCR (host controller reset): 0; CLF (control list filled): no; BLF (bulk list filled): yes; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000006: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000002 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000002: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
interrupt transfer done
IRQ exit
transfer descriptor ready:
hardware descriptor fields: flags: $f0000000; CBP: $002dc824; NextTD: $007ab0f0; BE: $002dc842; 
; #31 bytes in transfer:
555342437c7878780002000080000c28000000000000000100000000000000
$00000004 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000004: HCR (host controller reset): 0; CLF (control list filled): no; BLF (bulk list filled): yes; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000006: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000002 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000002: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
interrupt transfer done
IRQ exit
transfer descriptor ready:
hardware descriptor fields: flags: $f0000000; CBP: $0005c510; NextTD: $007ab0c0; BE: $0005c70f; 
; #512 bytes in transfer:
eb3c904b4f4c494252492000020101000100019258f85900200080000000000000000000800029038ef84c4e4f204e414d45202020204641543136202020cd19ebfe00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000055aa
$00000004 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000004: HCR (host controller reset): 0; CLF (control list filled): no; BLF (bulk list filled): yes; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
transfer descriptor ready:
hardware descriptor fields: flags: $f0000000; CBP: $002dc844; NextTD: $007ab030; BE: $002dc850; 
; #13 bytes in transfer:
555342537b7878780000000000
$00000004 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000004: HCR (host controller reset): 0; CLF (control list filled): no; BLF (bulk list filled): yes; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000006: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000002 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000002: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
interrupt transfer done
IRQ exit
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000006: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000002 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000002: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
interrupt transfer done
IRQ exit
transfer descriptor ready:
hardware descriptor fields: flags: $f0000000; CBP: $002dc824; NextTD: $007ab0c0; BE: $002dc842; 
; #31 bytes in transfer:
555342437d7878780020000080000c28000000008000001000000000000000
$00000004 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000004: HCR (host controller reset): 0; CLF (control list filled): no; BLF (bulk list filled): yes; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000006: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000002 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000002: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
interrupt transfer done
IRQ exit
transfer descriptor ready:
hardware descriptor fields: flags: $f0e00000; CBP: $007b2000; NextTD: $007ab090; BE: $007b2fff; 
; #8192 bytes in transfer:
ffffffffffffff00ffffff00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
transfer descriptor ready:
hardware descriptor fields: flags: $f0000000; CBP: $007b3000; NextTD: $007ab060; BE: $007b3fff; 
; #4096 bytes in transfer:
ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
$00000004 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000004: HCR (host controller reset): 0; CLF (control list filled): no; BLF (bulk list filled): yes; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
transfer descriptor ready:
hardware descriptor fields: flags: $f0000000; CBP: $002dc844; NextTD: $007ab000; BE: $002dc850; 
; #13 bytes in transfer:
555342537c7878780000000000
$00000004 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000004: HCR (host controller reset): 0; CLF (control list filled): no; BLF (bulk list filled): yes; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000006: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000002 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000002: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
interrupt transfer done
IRQ exit
transfer descriptor ready:
hardware descriptor fields: flags: $f0000000; CBP: $002dc824; NextTD: $007ab060; BE: $002dc842; 
; #31 bytes in transfer:
555342437e7878780020000080000c28000000408000001000000000000000
$00000004 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000004: HCR (host controller reset): 0; CLF (control list filled): no; BLF (bulk list filled): yes; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000006: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000002 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000002: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
interrupt transfer done
IRQ exit
transfer descriptor ready:
hardware descriptor fields: flags: $f0e00000; CBP: $007ae000; NextTD: $007ab090; BE: $007aefff; 
; #8192 bytes in transfer:
2073746f726167650d0a004b203a20696e76616c696420726573706f6e73652066726f6d206d6173732073746f72616765206465766963650d0a004b203a206d6173732073746f7261676520646576696365207265706f72747320666174616c206572726f720d0a004b203a20494e515549525920636f6d6d616e64206661696c65640d0a004b203a206661696c656420746f2067656e6572617465206469736b206e616d650d0a009090900c000000b7084000a40d400020000000040e400000000000430e4000680f4000640f4000000000000000000000000000140100003b335533b533cd33d333d833df33e533f133fa330034243448345f3489349e34c834e734fa3409352d35383545355f359435a035bb35ca35ed35153650365f3676369436bc3618372437323750377837f437fa3711382a38463864388c38d238d838e338e938f538fe3804395639b739d439e739043a103a2b3a493a673a853a9b3aa53aab3ad03ad93ae03a2e3b4f3b663b863b8c3ba23ba83bdc3bee3bf83b1f3c3d3c633c6c3c733c833c983c9e3cb33cb93cc23cd23cd83cde3ceb3cf53c283d2e3d3d3d483d573d5f3d6c3d723d7d3d833d943d9b3da73dad3dba3dc43ddf3df73dfe3d0b3e113e1c3e223e333e3a3e4f3e6b3e713e833e953eb53ed53e5a3f9c3fae3fb63fc83f000000100000160000008430f031f431fc31043208320c32000000000000000000000000ffffffffffffffffffffffffffffffff4b203a20466174616c206572726f7220647572696e6720657865637574696f6e206f6620636f6d6d616e6420000d0a004b203a20436f6d6d616e642000206661696c65640d0a004b203a206572726f722000207768696c6520726573657474696e67004b203a206572726f722000206166746572200020627974657320696e20726571756573742073746167650d0a004b203a206572726f722000206166746572200020627974657320696e20646174612073746167650d0a004b203a206661696c20647572696e6720524551554553542053454e53450d0a004b203a206572726f722000206166746572200020627974657320696e207374617475732073746167650d0a004b203a206572726f723a20696e76616c696420656e64706f696e742064657363726970746f720d0a004b203a204745544d41584c554e206661696c656420776974682073746174757320002c20617373756d696e67207a65726f0d0a004b203a2000206c6f676963616c20756e69742873290d0a004b203a207065726970686572616c20646576696365207479706520697320000d0a004b203a206469726563742d616363657373206d6173732073746f72616765206465766963652064657465637465640d0a004b203a206d656469612069732072656164790d0a004b203a200020617474656d7074732c2000207469636b730d0a004b203a206d65646961206e6f742072656164790d0a004b203a20736563746f722073697a6520697320002c206c61737420736563746f7220697320000d0a000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000007a4b01807c5a0180403000800000000098de748148de74810002088006000000000000000100000003000000061000008558018044d5a880c897828098de748174de74811f000000000000000471c180249882808ac4a880000000000010a98041c4a8800000000004000000c6c4a88004000000040000004612000046100000050000007c5a0180e03000800011088030df7481c8de748100070880060000000100000000000000030000001610000099580180fa0000000011088030df7481f4de7481ffffffffc010028004d18b8020320080461200001e10028046120000ccefc080000000002f00000004d18b8010000000000000002d7301802676018082120000001e7581c003808000002a80f4650480306004804cdf7481f4770180821200001edf028081000000000000007c5a018040300080d89682800000000074df748100020880070000000000000001000000030000000210000099580180dfdf7481d896828000000000a0df748170988280ffffffff0011088020320080461200000aa20380575b0180dfdf7481d896828000000000ccdf748170988280fffffffff4978280ffffffffa4edc08008000000861200007573626864300000000000000000000000000000abecc0800800000000120000d896828023000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
transfer descriptor ready:
hardware descriptor fields: flags: $f0000000; CBP: $007b0000; NextTD: $007ab030; BE: $007b0fff; 
; #4096 bytes in transfer:
ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
$00000004 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000004: HCR (host controller reset): 0; CLF (control list filled): no; BLF (bulk list filled): yes; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
transfer descriptor ready:
hardware descriptor fields: flags: $f0000000; CBP: $002dc844; NextTD: $007ab0f0; BE: $002dc850; 
; #13 bytes in transfer:
555342537d7878780000000000
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000006: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000002 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000002: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
interrupt transfer done
IRQ exit
$00000004 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000004: HCR (host controller reset): 0; CLF (control list filled): no; BLF (bulk list filled): yes; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000006: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000002 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000002: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
interrupt transfer done
IRQ exit
transfer descriptor ready:
hardware descriptor fields: flags: $f0000000; CBP: $002dc824; NextTD: $007ab030; BE: $002dc842; 
; #31 bytes in transfer:
555342437f7878780020000080000c2800000004a200001000000000000000
$00000004 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000004: HCR (host controller reset): 0; CLF (control list filled): no; BLF (bulk list filled): yes; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000006: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000002 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000002: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
interrupt transfer done
IRQ exit
transfer descriptor ready:
hardware descriptor fields: flags: $f0e00000; CBP: $007ae000; NextTD: $007ab090; BE: $007aefff; 
; #8192 bytes in transfer:
4e455720564f4c554d45200800000000000000000000b965dd4c00000000000042200049006e0066006f000f007272006d006100740069006f0000006e00000001530079007300740065000f00726d00200056006f006c00750000006d00650053595354454d7e31202020160003b865dd4cdd4c0000b965dd4c030000000000e56d0065006e0074002e000f009f7400780074000000ffffffff0000ffffffffe54e0065007700200054000f009f650078007400200044006f00000063007500e545575445587e3154585420003dbc66dd4cdd4c0000bd66dd4c000000000000e54553542020202054585420183dbc66dd4cdd4c0000bd66dd4c000000000000e52e0074006500730074000f00a12e007400780074002e007300000077007000e545535454587e3153575000009bc166dd4cdd4c0000c566dd4c060000100000e53931332020202020202000003ac466dd4cdd4c0000c566dd4c000000000000e574006500730074002e000f008d7400780074007e000000ffff0000ffffffffe54553547e31202054585400003dbc66dd4cdd4c0000bd66dd4c000000000000544553542020202054585420183dbc66dd4cdd4c0000c566dd4c070010000000432e0074006100720000000f009dffffffffffffffffffffffff0000ffffffff0268007000610064002d000f009d3000340030003700320030000000310038000176006d007700610072000f009d65002d007300630072006100000074006300564d574152457e3154415220004f4c85e44ce44c00005985e44c0800001a3c32000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000002e20202020202020202020100003b865dd4cdd4c0000b965dd4c0300000000002e2e202020202020202020100003b865dd4cdd4c0000b965dd4c0000000000004274000000ffffffffffff0f00ceffffffffffffffffffffffff0000ffffffff01570050005300650074000f00ce740069006e00670073002e000000640061005750534554547e31444154200055b865dd4cf84c0000b965dd4c04000c00000042470075006900640000000f00ffffffffffffffffffffffffff0000ffffffff0149006e006400650078000f00ff6500720056006f006c00750000006d006500494e444558457e31202020200060b965dd4cf84c0000ba65dd4c05004c000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
transfer descriptor ready:
hardware descriptor fields: flags: $f0000000; CBP: $007b0000; NextTD: $007ab000; BE: $007b0fff; 
; #4096 bytes in transfer:
2e20202020202020202020100003b865dd4cdd4c0000b965dd4c0300000000002e2e202020202020202020100003b865dd4cdd4c0000b965dd4c0000000000004274000000ffffffffffff0f00ceffffffffffffffffffffffff0000ffffffff01570050005300650074000f00ce740069006e00670073002e000000640061005750534554547e31444154200055b865dd4cf84c0000b965dd4c04000c00000042470075006900640000000f00ffffffffffffffffffffffffff0000ffffffff0149006e006400650078000f00ff6500720056006f006c00750000006d006500494e444558457e31202020200060b965dd4cf84c0000ba65dd4c05004c000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
$00000004 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000004: HCR (host controller reset): 0; CLF (control list filled): no; BLF (bulk list filled): yes; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
transfer descriptor ready:
hardware descriptor fields: flags: $f0000000; CBP: $002dc844; NextTD: $007ab0c0; BE: $002dc850; 
; #13 bytes in transfer:
555342537e7878780000000000
$00000004 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000004: HCR (host controller reset): 0; CLF (control list filled): no; BLF (bulk list filled): yes; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000006: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000002 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000002: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
interrupt transfer done
IRQ exit
transfer descriptor ready:
hardware descriptor fields: flags: $f0000000; CBP: $002dc824; NextTD: $007ab000; BE: $002dc842; 
; #31 bytes in transfer:
55534243807878780020000080000c2800000040a800001000000000000000
$00000004 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000004: HCR (host controller reset): 0; CLF (control list filled): no; BLF (bulk list filled): yes; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000006: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000002 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000002: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
interrupt transfer done
IRQ exit
transfer descriptor ready:
hardware descriptor fields: flags: $f0e00000; CBP: $015be000; NextTD: $007ab090; BE: $015befff; 
; #8192 bytes in transfer:
032f52442f312f74696e797061642e696e69002e6f626a000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
transfer descriptor ready:
hardware descriptor fields: flags: $f0000000; CBP: $015bf000; NextTD: $007ab0f0; BE: $015bffff; 
; #4096 bytes in transfer:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
$00000004 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000004: HCR (host controller reset): 0; CLF (control list filled): no; BLF (bulk list filled): yes; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
transfer descriptor ready:
hardware descriptor fields: flags: $f0000000; CBP: $002dc844; NextTD: $007ab060; BE: $002dc850; 
; #13 bytes in transfer:
555342537f7878780000000000
$00000004 ohci HcCommandStatus + ! 		\ HcCommandStatus: write : $00000004: HCR (host controller reset): 0; CLF (control list filled): no; BLF (bulk list filled): yes; OCR (ownership change request): no; SOC (scheduling overrun count): 0; 
IRQ entry
ohci HcInterruptStatus + @ 		\ HcInterruptStatus: read : $00000006: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 1; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
ohci HcInterruptEnable + @ 		\ HcInterruptEnable: read : $80000042: SO (scheduling overrun): no change; WDH (HCDoneHead writeback): ENABLE; SF (start of frame): no change; RD (resume detect): no change; UE (unrecoverable error): no change; FNO (frame number overflow): no change; RHSC (root hub status change): ENABLE; OC (ownership change): no change; MIE (master interrupt enable): ENABLE; 
$00000002 ohci HcInterruptStatus + ! 		\ HcInterruptStatus: write : $00000002: SO (scheduling overrun): 0; WDH (HCDoneHead writeback): 1; SF (start of frame): 0; RD (resume detect): 0; UE (unrecoverable error): 0; FNO (frame number overflow): 0; RHSC (root hub status change): 0; OC (ownership change): 0; MIE (master interrupt enable): 0; 
interrupt transfer done
IRQ exit

#endif
