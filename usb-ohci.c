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
	uint32_t	HcRhPortStatus;
};

/* ohci transfer descriptor */
struct ohci_td
{	struct
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
} __attribute__ ((aligned (16)));
static uint32_t bitmap_used_tds;
static volatile struct ohci_td tds[32];
static volatile struct ohci_td * allot_td(void)
{
int x;
	if ((x = find_first_clear(bitmap_used_tds)) == -1)
		return 0;
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
} __attribute__ ((aligned (16)));

/* HCCA - host controller communications area */
struct ohci_hcca
{
	struct ohci_ed	interrupt_table[32];
	/* note: this field is 16 bit, the most significant 16 bits are set to zero by the hardware */
	uint32_t	frame_number;
	struct ohci_td	done_head;
	uint32_t	reserved[116 / 4];
} __attribute__ ((aligned (4)));
static struct ohci_hcca ohci_hcca;

static volatile struct ohci_registers * ohci;


void init_ohci(void)
{
	sf_eval(SFORTH_OHCI_PHYSICAL_MEM_BASE " " "phys-mem-map phys-mem-window-base");
	ohci = (void *) sf_pop();
	if (ohci->HcRevision != 0x10)
	{
		print_str("bad usb ohci address\n");
		return;
	}
}

