/*
 * Linux device driver for Danmere's Backer 16/32 video tape backup devices.
 *
 *                            ISA Device I/O
 *
 * Copyright (C) 2000,2001,2002,2010  Kipp C. Cannon
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/module.h>
#include <linux/init.h>

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>

#include <asm/dma.h>
#include <asm/io.h>

#include <backer.h>
#include <bkr_unit.h>
#include <bkr_ring_buffer.h>


/*
 * ========================================================================
 *                          Module info and options
 * ========================================================================
 */

MODULE_AUTHOR("Kipp Cannon (kcannon@users.sourceforge.net)");
MODULE_DESCRIPTION("Backer 16 & 32 device driver --- ISA device support");
MODULE_SUPPORTED_DEVICE("backer_isa");
MODULE_LICENSE("GPL");
MODULE_ALIAS("backer_lowlevel");

static char  *units = "";
module_param(units, charp, 0);
MODULE_PARM_DESC(units, "units=port:dma[:port:dma]...");

static int  adjust = 0;
module_param(adjust, int, 0);
MODULE_PARM_DESC(adjust, "adjustment to start delay in microseconds");


/*
 * ========================================================================
 *
 *                         Constants/macros/etc.
 *
 * ========================================================================
 *
 * In order to guarantee that data starts on the first available line of
 * video, the Backer hardware needs the driver to pause between writing the
 * configuration byte and starting the data transfer.  On my hardware this
 * pause must be between 546 +/- 2 microseconds and 608 +/- 2 microseconds
 * inclusively for NTSC.  This range corresponds, within error, to 8.5
 * through 9.5 line periods for NTSC.
 *							-Kipp
 */

#define  MODULE_NAME            "backer_isa"
#define  DMA_IO_TO_MEM          0x14    /* demand mode, inc addr, auto-init */
#define  DMA_MEM_TO_IO          0x18    /* demand mode, inc addr, auto-init */
#define  DMA_BUFFER_SIZE        65536
#define  BKR_FRAME_TIME         HZ/BKR_MIN_FRAME_FREQ   /* jiffies */
#define  BKR_ISA_TIMEOUT        (30*BKR_FRAME_TIME)     /* ~1 second */
#define  DMA_HOLD_OFF           0x0200  /* stay this far back from transfer */
#define  INTERNAL_BUFFER        4

#if BKR_FRAME_TIME < 2
#error "Your kernel's HZ parameter is too low for this driver.  To use this driver with your kernel, try decreasing BKR_MIN_FRAME_FREQ in backer.h"
#endif


/*
 * ========================================================================
 *
 *                                 Data
 *
 * ========================================================================
 */

typedef unsigned long  jiffies_t;

static char bkr_isa_resource_name[] = "Danmere Technologies Inc. Backer Tape Interface";

struct bkr_stream_private_t {
	struct resource  ioresource;    /* I/O port */
	unsigned int  dma;              /* DMA channel number */
	dma_addr_t  dma_addr;           /* DMA buffer's bus address */
	struct timer_list  timer;       /* data mover timer entry */
	int  adjust;                    /* adjustment to start-up pause */
};


/*
 * ========================================================================
 *
 *                            MISC FUNCTIONS
 *
 * ========================================================================
 */

/*
 * Return the status of a DMA channel's DREQ line.  1 == active, 0 ==
 * inactive.
 */

static inline int get_dma_dreq(unsigned int dmanr)
{
	if(dmanr <= 3)
		return (dma_inb(DMA1_STAT_REG) >> (dmanr + 4)) & 1;
	else
		return (dma_inb(DMA2_STAT_REG) >> dmanr) & 1;
}


/*
 * Waits for a high-to-low transition of the DREQ status bit then retrieves
 * the offset within the DMA buffer at which the next transfer will occur.
 */

static size_t get_dma_offset(struct bkr_stream_t *stream)
{
	unsigned long  flags;
	unsigned int  microsecs;
	int  dma = stream->private->dma;
	size_t  residue;

	flags = claim_dma_lock();
	clear_dma_ff(dma);
	if(!get_dma_dreq(dma))
		udelay(BKR_LINE_PERIOD/2);
	for(microsecs = 0; get_dma_dreq(dma) && (microsecs < 2*BKR_LINE_PERIOD); microsecs++)
		udelay(1);
	residue = get_dma_residue(dma);
	release_dma_lock(flags);

	return ring_offset_sub(stream->ring, stream->ring->size - residue, DMA_HOLD_OFF);
}


/*
 * Periodically checks and updates the hardware's side of the DMA ring
 * buffer.
 */

static void timer_tick(unsigned long data)
{
	struct bkr_stream_t  *stream = (struct bkr_stream_t *) data;
	struct ring  *ring = stream->ring;
	struct timer_list  *timer = &stream->private->timer;
	size_t  offset;

	switch(stream->direction) {
	case BKR_READING:
		offset = get_dma_offset(stream);
		ring_lock(ring);
		ring->head = offset;
		ring_unlock(ring);
		break;

	case BKR_WRITING:
		offset = get_dma_offset(stream);
		ring_lock(ring);
		ring->tail = offset;
		ring_unlock(ring);
		break;

	default:
		return;
	}
	timer->expires = jiffies + BKR_FRAME_TIME;
	add_timer(timer);
	bkr_stream_do_callback(stream);
}


/*
 * Ensure the DMA buffer is filled to a frame boundary, and check if it's
 * empty.
 */

static int flush(struct bkr_stream_t *stream)
{
	int result;

	ring_lock(stream->ring);
	result = ring_fill_to(stream->ring, stream->frame_size, BKR_FILLER) ? -EAGAIN : bytes_in_ring(stream->ring) >= 2 * stream->frame_size ? -EAGAIN : 0;
	ring_unlock(stream->ring);

	return result;
}


/*
 * ========================================================================
 *
 *                              Stream API
 *
 * ========================================================================
 */

static int start(struct bkr_stream_t *stream, bkr_direction_t direction)
{
	struct bkr_stream_private_t  *private = stream->private;
	unsigned long  flags;
	int  pause;
	unsigned char  control;         /* hardware control byte */

	/*
	 * Initialize the DMA channel and buffer.
	 */

	if(request_dma(private->dma, bkr_isa_resource_name) < 0)
		return -EBUSY;

	stream->direction = direction;

	flags = claim_dma_lock();
	disable_dma(private->dma);
	clear_dma_ff(private->dma);
	if(direction == BKR_WRITING) {
		stream->ring->size -= DMA_BUFFER_SIZE % stream->frame_size;
		set_dma_mode(private->dma, DMA_MEM_TO_IO);
	} else
		set_dma_mode(private->dma, DMA_IO_TO_MEM);
	set_dma_addr(private->dma, private->dma_addr);
	set_dma_count(private->dma, stream->ring->size);
	enable_dma(private->dma);
	release_dma_lock(flags);

	/*
	 * Work the card's control bits.
	 */

	if(BKR_VIDEOMODE(stream->mode) == BKR_NTSC)
		pause = BKR_LINE_PERIOD * (BKR_FIRST_LINE_NTSC - 1);
	else
		pause = BKR_LINE_PERIOD * (BKR_FIRST_LINE_PAL - 1);
	if((pause += private->adjust) < 0)
		pause = 0;
	control = bkr_control(stream->mode, stream->direction);
	outb(control & ~(BKR_BIT_TRANSMIT | BKR_BIT_RECEIVE), private->ioresource.start);
	udelay(pause);
	outb(control, private->ioresource.start);

	/*
	 * Force an immediate update of the DMA buffer and add the timer
	 * tick function to the kernel timer list.  Both birds are killed
	 * by simply calling the function directly.
	 */

	init_timer(&private->timer);
	private->timer.function = timer_tick;
	private->timer.data = (unsigned long) stream;
	timer_tick((unsigned long) stream);

	return 0;
}


static int release(struct bkr_stream_t *stream)
{
	struct bkr_stream_private_t  *private = stream->private;
	int  result;

	if(stream->direction != BKR_STOPPED) {
		if(stream->direction == BKR_WRITING) {
			result = flush(stream);
			if(result < 0)
				return result;
		}

		outb(0, private->ioresource.start);
		stream->direction = BKR_STOPPED;
		del_timer_sync(&private->timer);
		free_dma(private->dma);
		/* resize buffer after we know nobody else is listening */
		stream->ring->size = DMA_BUFFER_SIZE;
	}
	return 0;
}


static int read(struct bkr_stream_t *stream)
{
	int bytes = bytes_in_ring(stream->ring);

	return bytes ? bytes : -EAGAIN;
}


static int write(struct bkr_stream_t *stream)
{
	int space = space_in_ring(stream->ring);

	return space ? space : -EAGAIN;
}


static struct bkr_stream_t *ready(struct bkr_stream_t *stream, int mode, unsigned int frame_size)
{
	struct bkr_stream_private_t  *private = stream->private;
	struct ring  *ring = stream->ring;

	stream->mode = mode;
	outb(0, private->ioresource.start);
	stream->direction = BKR_STOPPED;
	stream->frame_size = frame_size;
	ring_reset(ring);
	memset_ring(ring, 0, ring->size);

	return stream;
}


/*
 * ========================================================================
 *
 *                              Entry/Exit
 *
 * ========================================================================
 */

/*
 * Utility wrappers for I/O port resource management.
 */

static struct bkr_stream_private_t __init *isa_alloc_private(int ioport)
{
	struct bkr_stream_private_t  *private = kmalloc(GFP_KERNEL, sizeof(*private));

	if(private) {
		private->ioresource = (struct resource) {
			.name = bkr_isa_resource_name,
			.start = ioport,
			.end = ioport,
		};
		if(request_resource(&ioport_resource, &private->ioresource)) {
			kfree(private);
			private = NULL;
		}
	}
	return private;
}

static void isa_free_private(struct bkr_stream_private_t *private)
{
	if(private)
		release_resource(&private->ioresource);
	kfree(private);
}


/*
 * Creates a new ISA Backer device.
 */

static struct bkr_unit_t * __init bkr_isa_new(struct bkr_stream_private_t *private, int dma, char *msg)
{
	struct bkr_unit_t  *unit;
	struct bkr_stream_t  *stream;
	char  err_msg[50];

	sprintf(err_msg, "out of memory");
	stream = kmalloc(sizeof(*stream), GFP_KERNEL);
	if(!stream)
		goto no_stream;

	private->dma = dma;
	private->adjust = adjust;

	stream->ops = (struct bkr_stream_ops_t) {
		.ready = ready,
		.start = start,
		.release = release,
		.read = read,
		.write = write,
	};
	stream->private = private;
	bkr_stream_set_callback(stream, NULL, NULL);
	stream->timeout = BKR_ISA_TIMEOUT;

	stream->ring = ring_new(NULL, DMA_BUFFER_SIZE);
	if(!stream->ring);
		goto no_stream;
	stream->ring->buffer = dma_alloc_coherent(NULL, stream->ring->size, &private->dma_addr, GFP_ATOMIC);
	if(!stream->ring->buffer) {
		sprintf(err_msg, "can't allocate DMA buffer");
		goto no_dma_buffer;
	}

	down(&bkr_unit_list_lock);
	unit = bkr_unit_register(stream);
	if(!unit) {
		sprintf(err_msg, "device creation failed");
		goto no_unit;
	}
	unit->owner = THIS_MODULE;
	up(&bkr_unit_list_lock);

	printk(KERN_INFO MODULE_NAME ": unit %s: %s I/O port %#x, DMA channel %u", unit->name, msg, (int) private->ioresource.start, private->dma);
	if(private->adjust)
		printk(", adjusted %+d µs", private->adjust);
	printk("\n");

	return unit;

no_unit:
	up(&bkr_unit_list_lock);
	dma_free_coherent(NULL, DMA_BUFFER_SIZE, stream->ring->buffer, private->dma_addr);
no_dma_buffer:
	ring_free(stream->ring);
no_stream:
	kfree(stream);
	printk(KERN_INFO MODULE_NAME ": %s\n", err_msg);
	return NULL;
}


/*
 * Autoprobes for ISA Backer cards.  If no ISA devices get registered then
 * it exits with -ENODEV otherwise it returns 0.
 */

static int __init bkr_isa_init(void)
{
	unsigned long  flags;
	jiffies_t  bailout;
	int  count = 0;
	int  ioport;
	int  i, dma[2], num_dma;
	int  found_card;
	struct bkr_stream_private_t  *private;

	/*
	 * Register any user-requested devices.
	 */

	while(*units != '\0') {
		ioport = simple_strtoul(units, &units, 0);
		if(*(units++) != ':')
			break;
		dma[0] = simple_strtoul(units, &units, 0);
		private = isa_alloc_private(ioport);
		if(private) {
			if(bkr_isa_new(private, dma[0], "using"))
				count++;
			else
				isa_free_private(private);
		}
		if(*units == ':')
			units++;
	}
	if(count)
		return 0;

	/*
	 * Grab and disable DMA channels
	 */

	num_dma = 0;
	if(request_dma(1, bkr_isa_resource_name) >= 0)
		dma[num_dma++] = 1;
	else
		printk(KERN_INFO MODULE_NAME ": can't probe on DMA channel 1: channel in use.\n");
	if(request_dma(3, bkr_isa_resource_name) >= 0)
		dma[num_dma++] = 3;
	else
		printk(KERN_INFO MODULE_NAME ": can't probe on DMA channel 3: channel in use.\n");

	flags = claim_dma_lock();
	for(i = 0; i < num_dma; i++)
		disable_dma(dma[i]);
	release_dma_lock(flags);

	/*
	 * Probe for I/O port and DMA channel.
	 */

	for(ioport = 0x300; ioport <= 0x33c; ioport += 4) {
		found_card = 0;

		/*
		 * Do a write-read test on the I/O port.
		 */

		private = isa_alloc_private(ioport);
		if(!private)
			continue;

		outb(BKR_BIT_NTSC_VIDEO, ioport);
		udelay(50);	/* FIXME: how much is really needed? */
		if(inb(ioport) & ~(BKR_BIT_DATA | BKR_BIT_SYNC | BKR_BIT_FRAME_BUSY)) {
			isa_free_private(private);
			continue;
		}

		/*
		 * Try starting a transfer and watch for DREQ activity.
		 */

		outb(BKR_BIT_DMA_REQUEST, ioport);
		udelay(BKR_LINE_PERIOD * (BKR_FIRST_LINE_PAL - 1) + adjust);
		outb(BKR_BIT_DMA_REQUEST | BKR_BIT_TRANSMIT, ioport);
		for(i = 0, bailout = jiffies + BKR_FRAME_TIME; time_before(jiffies, bailout); i = (i + 1) % num_dma)
			if(get_dma_dreq(dma[i])) {
				outb(0, ioport);
				if(bkr_isa_new(private, dma[i], "found at")) {
					count++;
					found_card = 1;
				}
				break;
			}

		if(!found_card)
			isa_free_private(private);
	}

	for(i = 0; i < num_dma; i++)
		free_dma(dma[i]);

	if(count)
		return 0;
	printk(KERN_INFO MODULE_NAME ": no devices found.\n");
	return -ENODEV;
}


/*
 * Unregister all ISA devices when we get unloaded.
 */

static void __exit bkr_isa_exit(void)
{
	struct bkr_unit_t  *unit;
	struct bkr_stream_t  *stream;
	struct bkr_stream_private_t  *private;
	struct list_head  *curr;

	down(&bkr_unit_list_lock);
	do
		list_for_each(curr, &bkr_unit_list) {
			unit = list_entry(curr, struct bkr_unit_t, list);
			if(unit->owner != THIS_MODULE)
				continue;
			stream = unit->stream;
			private = stream->private;
			bkr_unit_unregister(unit);
			dma_free_coherent(NULL, DMA_BUFFER_SIZE, stream->ring->buffer, private->dma_addr);
			ring_free(stream->ring);
			kfree(stream);
			isa_free_private(private);
			break;
		}
	while(curr != &bkr_unit_list);
	up(&bkr_unit_list_lock);
}


module_init(bkr_isa_init);
module_exit(bkr_isa_exit);
