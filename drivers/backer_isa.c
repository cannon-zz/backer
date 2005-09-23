/*
 * backer_isa
 *
 * Linux device driver for Danmere's Backer 16/32 video tape backup cards.
 *
 *                            ISA Device I/O
 *
 * Copyright (C) 2000,2001  Kipp C. Cannon
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

#include "backer.h"
#include "backer_device.h"
#include "backer_unit.h"


/*
 * ========================================================================
 *                          Module info and options
 * ========================================================================
 */

MODULE_AUTHOR("Kipp Cannon (kcannon@users.sourceforge.net)");
MODULE_DESCRIPTION("Backer 16 & 32 device driver --- ISA device support");
MODULE_SUPPORTED_DEVICE("backer_isa");

static char  *units = "";
MODULE_PARM(units, "s");
MODULE_PARM_DESC(units, "units=port:dma[:port:dma...]");

static int  adjust = 0;
MODULE_PARM(adjust, "i");

EXPORT_NO_SYMBOLS;


/*
 * ========================================================================
 *                         Constants/macros/etc.
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
#define  DMA_BUFFER_SIZE        65536   /* must match dma_offset_t */
#define  DMA_HOLD_OFF           0x01ff  /* stay this far back from transfer */
#define  MIN_SYNC_FREQUENCY     50      /* min horiz sync frequency in Hz */
#define  MIN_FRAME_FREQUENCY    20      /* min frame rate in Hz */
#define  MAX_ADJUST_MAGNITUDE   (4 * BKR_LINE_PERIOD)
#define  INTERNAL_BUFFER        4

#if (MIN_SYNC_FREQUENCY > HZ/2) || (MIN_FRAME_FREQUENCY > HZ/2)
#error "One of the *_FREQUENCY parameters is too high"
#endif

#define  STRUCT_FROM_MEMBER(type, member, addr)  (type *) ((void *) addr - ((void *) &((type *) NULL)->member - NULL))


/*
 * ========================================================================
 *                                 Data
 * ========================================================================
 */

typedef unsigned long  jiffies_t;
typedef u_int16_t  dma_offset_t;        /* unsigned 16 bit integer */

static char bkr_isa_resource_name[] = "Danmere Technologies Inc. Backer Tape Interface";

typedef struct
	{
	unsigned char  *dma_buffer;     /* DMA buffer */
	unsigned int  dma_size;         /* DMA buffer in use */
	dma_offset_t  dma_head;         /* DMA buffer head */
	dma_offset_t  dma_tail;         /* DMA buffer tail */
	struct resource  ioresource;    /* I/O port */
	unsigned int  dma;              /* DMA channel number */
	dma_addr_t  dma_addr;           /* DMA buffer's bus address */
	struct timer_list  timer;       /* data mover timer entry */
	int  adjust;                    /* adjustment to start-up pause */
	} bkr_isa_private_t;


/*
 * ========================================================================
 *
 *                            MISC FUNCTIONS
 *
 * ========================================================================
 */

/*
 * get_dreq_status()
 *
 * Return the status of a DMA channel's DREQ line.  1 == active, 0 ==
 * inactive.
 */

static inline int get_dreq_status(unsigned int dmanr)
{
	int  result;

	if(dmanr <= 3)
		result = dma_inb(DMA1_STAT_REG) >> 4;
	else
		result = dma_inb(DMA2_STAT_REG);

	return((result >> dmanr) & 1);
}


/*
 * update_dma_offset()
 *
 * Waits for a high-to-low transition of the DREQ status bit then retrieves
 * the offset within the DMA buffer at which the next transfer will occur.
 * During read operations this is the buffer's head; during write
 * operations it is the tail.
 *
 * NOTE:  the low byte retrieved by this function is not reliable and this
 * should be compensated for by leaving a "hold off" allowance.
 */

static void update_dma_offset(bkr_isa_private_t *private, dma_offset_t *offset)
{
	unsigned long  flags;
	jiffies_t  bailout;
	int  dma = private->dma;

	bailout = jiffies + HZ/MIN_SYNC_FREQUENCY;

	while(!get_dreq_status(dma) && time_before(jiffies, bailout));
	while(get_dreq_status(dma) && time_before(jiffies, bailout));

	flags = claim_dma_lock();
	clear_dma_ff(dma);
	*offset = private->dma_size - get_dma_residue(dma);
	release_dma_lock(flags);
}


/*
 * bkr_isa_do_move()
 *
 * Handles the movement of data between the primary (DMA) buffer and the
 * secondary (I/O) buffer.  In both the reading and writing cases, the game
 * is to determine how many bytes we can move with a single memcpy().  For
 * example, when reading this is whichever is smallest:  (i) the number of
 * bytes in the DMA buffer (ii) the amount of free space in the I/O buffer
 * (iii) the number of bytes from the tail to the top of the DMA buffer
 * (iv) the amount of free space between the head and the top of the I/O
 * buffer.  The last two cases need to be considered because memcpy() can't
 * "loop around" for us.  We then just move that much and leave whatever
 * is left over 'til next time.
 */

static void bkr_isa_do_move(unsigned long data)
{
	int  tmp, count;
	bkr_unit_t  *unit = (bkr_unit_t *) data;
	bkr_device_t  *device = &unit->device;
	bkr_isa_private_t  *private = (bkr_isa_private_t *) device->private;

	switch(device->state)
		{
		case BKR_READING:
		update_dma_offset(private, &private->dma_head);
		count = bytes_in_buffer(private->dma_head, private->dma_tail, private->dma_size) - DMA_HOLD_OFF;
		if(count < 0)
			count = 0;
		tmp = space_in_buffer(device->io_head, device->io_tail, device->io_size);
		if(count > tmp)
			count = tmp;
		tmp = private->dma_size - private->dma_tail;
		if(count > tmp)
			count = tmp;
		tmp = device->io_size - device->io_head;
		if(count > tmp)
			count = tmp;
		memcpy(device->io_buffer + device->io_head, private->dma_buffer + private->dma_tail, count);
		private->dma_tail += count;
		device->io_head = (device->io_head + count) & BKR_OFFSET_MASK;
		break;

		case BKR_WRITING:
		update_dma_offset(private, &private->dma_tail);
		count = space_in_buffer(private->dma_head, private->dma_tail, private->dma_size) - DMA_HOLD_OFF;
		if(count < 0)
			count = 0;
		tmp = bytes_in_buffer(device->io_head - device->io_head % device->frame_size, device->io_tail, device->io_size);
		if(count > tmp)
			count = tmp;
		tmp = private->dma_size - private->dma_head;
		if(count > tmp)
			count = tmp;
		tmp = device->io_size - device->io_tail;
		if(count > tmp)
			count = tmp;
		memcpy(private->dma_buffer + private->dma_head, device->io_buffer + device->io_tail, count);
		private->dma_head += count;
		if(private->dma_head == private->dma_size)
			private->dma_head = 0;
		device->io_tail += count;
		if(device->io_tail == device->io_size)
			device->io_tail = 0;
		break;

		default:
		return;
		}

	wake_up_interruptible(&unit->io_queue);
	private->timer.expires = jiffies + HZ/MIN_FRAME_FREQUENCY;
	add_timer(&private->timer);
}


/*
 * ========================================================================
 *
 *                              DEVICE OPS
 *
 * ========================================================================
 */

/*
 * bkr_isa_start()
 *
 * Start the tape <---> memory data transfer.
 */

static int bkr_isa_start(bkr_device_t *device, bkr_state_t direction)
{
	bkr_isa_private_t  *private = (bkr_isa_private_t *) device->private;
	bkr_unit_t  *unit = STRUCT_FROM_MEMBER(bkr_unit_t, device, device);
	unsigned long  flags;
	int  dma_mode, pause;

	/*
	 * Do first 'cause vmalloc() might allow a task switch.
	 */

	device->state = direction;

	/*
	 * Allocate and initialize the secondary buffer.
	 */

	device->io_buffer = (unsigned char *) vmalloc(device->io_size);
	if(device->io_buffer == NULL)
		{
		device->state = BKR_STOPPED;
		return(-ENOMEM);
		}
	memset(device->io_buffer, 0, device->io_size);

	/*
	 * Set some direction-specific things.
	 */

	private->dma_size = DMA_BUFFER_SIZE;
	if(direction == BKR_WRITING)
		{
		private->dma_size -= DMA_BUFFER_SIZE % device->frame_size;
		dma_mode = DMA_MEM_TO_IO;
		}
	else
		dma_mode = DMA_IO_TO_MEM;

	/*
	 * Initialize the DMA channel and buffer.
	 */

	if(request_dma(private->dma, bkr_isa_resource_name) < 0)
		{
		device->state = BKR_STOPPED;
		vfree(device->io_buffer);
		return(-EBUSY);
		}

	private->dma_head = private->dma_tail = 0;
	memset(private->dma_buffer, 0, DMA_BUFFER_SIZE);

	flags = claim_dma_lock();
	disable_dma(private->dma);
	clear_dma_ff(private->dma);
	set_dma_mode(private->dma, dma_mode);
	set_dma_addr(private->dma, private->dma_addr);
	set_dma_count(private->dma, private->dma_size);
	enable_dma(private->dma);
	release_dma_lock(flags);

	/*
	 * Work the card's control bits.
	 */

	if(BKR_VIDEOMODE(device->mode) == BKR_NTSC)
		pause = BKR_LINE_PERIOD * (BKR_FIRST_LINE_NTSC - 1);
	else
		pause = BKR_LINE_PERIOD * (BKR_FIRST_LINE_PAL - 1);
	pause += private->adjust;
	outb(device->control & ~(BKR_BIT_TRANSMIT | BKR_BIT_RECEIVE), private->ioresource.start);
	if(device->mode == BKR_WRITING)
		udelay(pause);
	outb(device->control, private->ioresource.start);

	/*
	 * Force an immediate update of the DMA buffer and add the data
	 * mover function to the kernel timer list.  Both birds are killed
	 * by simply calling the function directly.
	 */

	init_timer(&private->timer);
	private->timer.function = bkr_isa_do_move;
	private->timer.data = (unsigned long) unit;
	bkr_isa_do_move((unsigned long) unit);

	return(0);
}


/*
 * bkr_isa_stop()
 *
 * Stop the tape <---> memory transfer.  The DMA channel is left fully
 * configured so that if a bug in someone else's code starts a transfer
 * without setting up a proper buffer then at least it will occur in a safe
 * region of memory (our buffer).
 */

static void bkr_isa_stop(bkr_device_t *device)
{
	bkr_isa_private_t  *private = (bkr_isa_private_t *) device->private;

	/*
	 * Stop and reset the ISA card.
	 */

	outb(0, private->ioresource.start);
	device->state = BKR_STOPPED;

	/*
	 * Stop the data mover.
	 */

	del_timer_sync(&private->timer);

	/*
	 * Free resources.
	 */

	vfree(device->io_buffer);
	free_dma(private->dma);

	return;
}


/*
 * bkr_isa_read()
 *
 * Doesn't actually "read" data... just makes sure the requested length of
 * data starting at device.tail is available in the I/O buffer.  Returns 0
 * on success, -EAGAIN if the data isn't available.
 */

static int bkr_isa_read(bkr_device_t *device, unsigned int length)
{
	int  result = 0;

	if(bytes_in_buffer(device->io_head, device->io_tail, device->io_size) < length)
		result = -EAGAIN;

	return(result);
}


/*
 * bkr_isa_write()
 *
 * Doesn't actually "write" data... just makes sure the requested amount of
 * space is available starting at the I/O buffer head.  Returns 0 on
 * success, -EAGAIN if the space isn't available.
 */

static int bkr_isa_write(bkr_device_t *device, unsigned int length)
{
	int  result = 0;

	if(space_in_buffer(device->io_head, device->io_tail, device->io_size) < length)
		result = -EAGAIN;

	return(result);
}


/*
 * bkr_isa_flush()
 *
 * Flushes outbound data from the I/O and DMA buffers.  Returns 0 if the
 * data has all be written to tape or -EAGAIN if data is still in the
 * buffers.
 */

static int bkr_isa_flush(bkr_device_t *device)
{
	bkr_isa_private_t  *private = (bkr_isa_private_t *) device->private;
	jiffies_t  bailout;
	int  count, result;

	/*
	 * Fill the I/O buffer to a frame boundary.
	 */

	count = device->io_head % device->frame_size;
	if(count != 0)
		{
		count = device->frame_size - count;
		result = bkr_isa_write(device, count);
		if(result < 0)
			return(result);
		memset(device->io_buffer + device->io_head, 0, count);
		device->io_head += count;
		if(device->io_head == device->io_size)
			device->io_head = 0;
		}

	/*
	 * Don't proceed until the I/O buffer is empty.
	 */

	result = bkr_isa_write(device, device->io_size - 1);
	if(result < 0)
		return(result);

	/*
	 * Pad out the DMA buffer by the size of the card's internal
	 * buffer.
	 */

	count = INTERNAL_BUFFER - private->dma_head % device->frame_size;
	if(count > 0)
		{
		if(space_in_buffer(private->dma_head, private->dma_tail, private->dma_size) < count)
			return(-EAGAIN);
		memset(private->dma_buffer + private->dma_head, 0, count);
		private->dma_head += count;
		}

	/*
	 * Wait for the DMA buffer to empty.
	 */

	result = 0;
	bailout = jiffies + HZ/MIN_FRAME_FREQUENCY;
	do
		{
		update_dma_offset(private, &private->dma_tail);
		if((bytes_in_buffer(private->dma_head, private->dma_tail, private->dma_size) >= device->frame_size) || time_after_eq(jiffies, bailout))
			{
			result = -EAGAIN;
			break;
			}
		}
	while(bytes_in_buffer(private->dma_head, private->dma_tail, private->dma_size) >= 7*INTERNAL_BUFFER);

	return(result);
}


/*
 * ========================================================================
 *
 *                            ENTRY/EXIT CODE
 *
 * ========================================================================
 */

static bkr_device_ops_t  bkr_isa_dev_ops =
	{
	THIS_MODULE,
	bkr_isa_start,
	bkr_isa_stop,
	bkr_isa_read,
	bkr_isa_write,
	bkr_isa_flush
	};


/*
 * bkr_isa_new()
 *
 * Creates a new ISA Backer device.
 */

static bkr_unit_t * __init bkr_isa_new(int ioport, int dma, char *msg)
{
	bkr_unit_t  *unit;
	bkr_isa_private_t  *private;

	unit = bkr_unit_register(PM_ISA_DEV, &bkr_isa_dev_ops, sizeof(bkr_isa_private_t));
	if(unit == NULL)
		goto no_unit;
	private = (bkr_isa_private_t *) unit->device.private;

	private->ioresource = (struct resource) { bkr_isa_resource_name, ioport, ioport };
	if(request_resource(&ioport_resource, &private->ioresource))
		goto no_ioport;

	private->dma = dma;
	private->adjust = adjust;

	private->dma_buffer = (unsigned char *) pci_alloc_consistent(NULL, DMA_BUFFER_SIZE, &private->dma_addr);
	if(private->dma_buffer == NULL)
		{
		printk(KERN_INFO MODULE_NAME ": %s: can't allocate DMA buffer", unit->name);
		goto no_dma_buffer;
		}

	printk(KERN_INFO MODULE_NAME ": %s: %s I/O port %#x, DMA channel %u", unit->name, msg, ioport, dma);
	if(private->adjust)
		printk(", adjusted %+d µs", private->adjust);
	printk("\n");

	return(unit);

	no_dma_buffer:
		release_resource(&private->ioresource);
	no_ioport:
		bkr_unit_unregister(unit);
	no_unit:
		return(NULL);
}


/*
 * backer_isa_init()
 *
 * Autoprobes for ISA Backer cards.  If no ISA devices get registered then
 * it exits with -ENODEV otherwise it returns 0.
 */

static int __init backer_isa_init(void)
{
	unsigned long  flags;
	jiffies_t  bailout;
	int  count = 0;
	int  ioport;
	int  i, dma[2], num_dma;

	/*
	 * Do some initialization.
	 */

	if(adjust < -MAX_ADJUST_MAGNITUDE)
		adjust = -MAX_ADJUST_MAGNITUDE;
	else if(adjust > +MAX_ADJUST_MAGNITUDE)
		adjust = +MAX_ADJUST_MAGNITUDE;

	/*
	 * Register any user-requested devices.
	 */

	while(*units != '\0')
		{
		ioport = simple_strtoul(units, &units, 0);
		if(*(units++) != ':')
			break;
		dma[0] = simple_strtoul(units, &units, 0);
		if(bkr_isa_new(ioport, dma[0], "using") != NULL)
			count++;
		if(*units == ':')
			units++;
		}
	if(count)
		return(0);

	/*
	 * Grab and disable DMA channels
	 */

	num_dma = 0;
	if(request_dma(1, bkr_isa_resource_name) >= 0)
		dma[num_dma++] = 1;
	if(request_dma(3, bkr_isa_resource_name) >= 0)
		dma[num_dma++] = 3;

	flags = claim_dma_lock();
	for(i = 0; i < num_dma; i++)
		disable_dma(dma[i]);
	release_dma_lock(flags);

	/*
	 * Probe for I/O port and DMA channel.
	 */

	for(ioport = 0x300; ioport <= 0x33c; ioport += 4)
		{
		/*
		 * Do a write-read test.
		 */

		if(check_resource(&ioport_resource, ioport, 1))
			continue;
		outb(BKR_BIT_NTSC_VIDEO, ioport);
		if((inb(ioport) & ~BKR_BIT_DATA & ~BKR_BIT_SYNC & ~BKR_BIT_FRAME_BUSY) != 0)
			continue;

		/*
		 * Try starting a transfer and watch for DREQ activity.
		 */

		outb(BKR_BIT_DMA_REQUEST, ioport);
		udelay(BKR_LINE_PERIOD * (BKR_FIRST_LINE_PAL - 1) + adjust);
		outb(BKR_BIT_DMA_REQUEST | BKR_BIT_TRANSMIT, ioport);
		for(i = 0, bailout = jiffies + HZ/MIN_SYNC_FREQUENCY; time_before(jiffies, bailout); )
			{
			if(get_dreq_status(dma[i]))
				{
				outb(0, ioport);
				if(bkr_isa_new(ioport, dma[i], "found at") != NULL)
					count++;
				break;
				}
			if(++i >= num_dma)
				i = 0;
			}
		}

	for(i = 0; i < num_dma; i++)
		free_dma(dma[i]);

	if(count)
		return(0);
	printk(KERN_INFO MODULE_NAME ": no devices found.\n");
	return(-ENODEV);
}


/*
 * backer_isa_exit()
 *
 * Unregister all ISA devices when we get unloaded.
 */

static void __exit backer_isa_exit(void)
{
	bkr_unit_t  *unit;
	bkr_isa_private_t  *private;
	struct list_head  *curr;

	for(curr = bkr_unit_list.next; curr != &bkr_unit_list; )
		{
		unit = list_entry(curr, bkr_unit_t, list);
		curr = curr->next;
		if(unit->device.ops->owner != THIS_MODULE)
			continue;
		private = (bkr_isa_private_t *) unit->device.private;
		release_resource(&private->ioresource);
		pci_free_consistent(NULL, DMA_BUFFER_SIZE, private->dma_buffer, private->dma_addr);
		bkr_unit_unregister(unit);
		}
}


module_init(backer_isa_init);
module_exit(backer_isa_exit);
