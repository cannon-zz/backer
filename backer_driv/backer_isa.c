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

#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/list.h>
#include <linux/malloc.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

#include <asm/dma.h>
#include <asm/io.h>

#include "backer.h"
#include "backer_device.h"
#include "backer_unit.h"


/*
 * WORKAROUND defined == enable work-arounds & disable experimental code
 */

#define  WORKAROUND


/*
 * ========================================================================
 *                          Module info and options
 * ========================================================================
 */

MODULE_AUTHOR("Kipp Cannon (kcannon@users.sourceforge.net)");
MODULE_DESCRIPTION("Backer 16 & 32 device driver --- ISA device support");
MODULE_SUPPORTED_DEVICE("backer_isa");

#define  BKR_MAX_INSMOD_DEVICES  3
#define  BKR_MAX_INSMOD_OPTIONS  6	/* must do this manually */
static int  isa[2*BKR_MAX_INSMOD_DEVICES] __initdata = { };
MODULE_PARM(isa, "2-" __MODULE_STRING(BKR_MAX_INSMOD_OPTIONS) "i");
MODULE_PARM_DESC(isa, "isa=port,dma[,port,dma...]");

EXPORT_NO_SYMBOLS;


/*
 * ========================================================================
 *                         Constants/macros/etc.
 * ========================================================================
 */

#define  MODULE_NAME            "backer_isa"
#define  DMA_IO_TO_MEM          0x14    /* demand mode, inc addr, auto-init */
#define  DMA_MEM_TO_IO          0x18    /* demand mode, inc addr, auto-init */
#define  DMA_HOLD_OFF           0x01ff  /* stay this far back from transfer */
#define  DMA_BUFFER_SIZE        65536   /* must match dma_offset_t */
#define  MAX_UPDATE_FREQ        10      /* max DMA update rate in Hz */
#define  MIN_SYNC_FREQ          50      /* min sync frequency in Hz */
#define  INTERNAL_BUFFER        4

#if (MAX_UPDATE_FREQ > HZ) || (MIN_SYNC_FREQ > HZ)
#error "One of the FREQ parameters is too high"
#endif


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
	unsigned char  *buffer;         /* location of DMA buffer */
	unsigned int  size;             /* DMA buffer in use */
	dma_offset_t  head;             /* DMA buffer head */
	dma_offset_t  tail;             /* DMA buffer tail */
	unsigned int  ioport;           /* I/O port */
	unsigned int  dma;              /* DMA channel number */
	dma_addr_t  dma_addr;           /* DMA buffer's bus address */
	int  error;                     /* pending error */
	struct timer_list  timer;       /* data mover timer entry */
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
 * Retrieves the offset within the DMA buffer at which the next transfer
 * will occur.  During read operations this is the buffer's head; during
 * write operations it is the tail.  Returns 0 on success, -EAGAIN if no
 * DMA activity is detected.
 *
 * NOTE:  the low byte retrieved by this function is not reliable and this
 * should be compensated for by leaving a "hold off" allowance.
 */

static int update_dma_offset(bkr_isa_private_t *isa, dma_offset_t *offset)
{
	unsigned long  flags;
	jiffies_t  bailout;

	bailout = jiffies + HZ/MIN_SYNC_FREQ;

	while(!get_dreq_status(isa->dma))
		 if(time_after_eq(jiffies, bailout))
			return(-EAGAIN);

	while(get_dreq_status(isa->dma))
		if(time_after_eq(jiffies, bailout))
			return(-EAGAIN);

	flags = claim_dma_lock();
	clear_dma_ff(isa->dma);
	*offset = isa->size - get_dma_residue(isa->dma);
	release_dma_lock(flags);

	return(0);
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
	bkr_device_t  *device = (bkr_device_t *) data;
	bkr_isa_private_t  *isa = (bkr_isa_private_t *) device->private;

	switch(device->state)
		{
		case BKR_READING:
		isa->error = update_dma_offset(isa, &isa->head);

		count = bytes_in_buffer(isa->head, isa->tail, isa->size);
		count -= DMA_HOLD_OFF;
		if(count < 0)
			count = 0;
		tmp = space_in_buffer(device->head, device->tail, device->size);
		if(count > tmp)
			count = tmp;
		tmp = isa->size - isa->tail;
		if(count > tmp)
			count = tmp;
		tmp = device->size - device->head;
		if(count > tmp)
			count = tmp;

		memcpy(device->buffer + device->head, isa->buffer + isa->tail, count);

		isa->tail += count;
		device->head = (device->head + count) & BKR_OFFSET_MASK;
		break;

		case BKR_WRITING:
		isa->error = update_dma_offset(isa, &isa->tail);

		count = space_in_buffer(isa->head, isa->tail, isa->size);
		count -= DMA_HOLD_OFF;
		if(count < 0)
			count = 0;
		tmp = bytes_in_buffer(device->head - device->head % device->frame_size, device->tail, device->size);
		if(count > tmp)
			count = tmp;
		tmp = isa->size - isa->head;
		if(count > tmp)
			count = tmp;
		tmp = device->size - device->tail;
		if(count > tmp)
			count = tmp;

		memcpy(isa->buffer + isa->head, device->buffer + device->tail, count);

		isa->head += count;
		if(isa->head == isa->size)
			isa->head = 0;
		device->tail += count;
		if(device->tail == device->size)
			device->tail = 0;
		break;

		default:
		return;
		}

	isa->timer.expires = jiffies + HZ/MAX_UPDATE_FREQ;
	add_timer(&isa->timer);
}


/*
 * ========================================================================
 *
 *                              DEVICE OPS
 *
 * ========================================================================
 */

/*
 * bkr_isa_*_use_count()
 *
 * Lock and unlock module.
 */

static void  bkr_isa_inc_use_count(void)
{
	MOD_INC_USE_COUNT;
}

static void  bkr_isa_dec_use_count(void)
{
	MOD_DEC_USE_COUNT;
}


/*
 * bkr_isa_start()
 *
 * Start the tape <---> memory data transfer.
 */

static int bkr_isa_start(bkr_device_t *device, bkr_state_t direction)
{
	unsigned long  flags;
	bkr_isa_private_t  *isa = (bkr_isa_private_t *) device->private;

	device->state = direction;

	/*
	 * Allocate and initialize the secondary buffer.
	 */

	device->buffer = (unsigned char *) vmalloc(device->size);
	if(device->buffer == NULL)
		{
		device->state = BKR_STOPPED;
		return(-ENOMEM);
		}
	device->head = 0;
	device->tail = 0;
	memset(device->buffer, 0, device->size);

	/*
	 * Initialize the DMA channel and buffer.
	 */

	if(request_dma(isa->dma, bkr_isa_resource_name) < 0)
		{
		device->state = BKR_STOPPED;
		vfree(device->buffer);
		return(-EBUSY);
		}

	isa->size = DMA_BUFFER_SIZE;
	if(direction == BKR_WRITING)
		isa->size -= DMA_BUFFER_SIZE % device->frame_size;
	isa->head = 0;
	isa->tail = 0;
	memset(isa->buffer, 0, DMA_BUFFER_SIZE);

	flags = claim_dma_lock();
	disable_dma(isa->dma);
	clear_dma_ff(isa->dma);
	set_dma_mode(isa->dma, (direction == BKR_WRITING) ? DMA_MEM_TO_IO : DMA_IO_TO_MEM);
	set_dma_addr(isa->dma, isa->dma_addr);
	set_dma_count(isa->dma, isa->size);
	enable_dma(isa->dma);
	release_dma_lock(flags);

	/*
	 * Work the card's control bits.
	 */

	outb(device->control, isa->ioport);
	device->control |= (direction == BKR_WRITING) ? BKR_BIT_TRANSMIT : BKR_BIT_RECEIVE;
	outb(device->control, isa->ioport);

	/*
	 * Force an immediate update of the DMA buffer and add the data
	 * mover function to the kernel timer list.  Both birds are killed
	 * by simply calling the function directly.
	 */

	isa->error = 0;
	init_timer(&isa->timer);
	isa->timer.function = bkr_isa_do_move;
	isa->timer.data = (unsigned long) device;
	isa->timer.function(isa->timer.data);

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
	bkr_isa_private_t  *isa = (bkr_isa_private_t *) device->private;

	/*
	 * Stop and reset the ISA card.
	 */

	outb(0, isa->ioport);

	device->control &= ~(BKR_BIT_TRANSMIT | BKR_BIT_RECEIVE);
	device->state = BKR_STOPPED;

	/*
	 * Stop the data mover.
	 */

	del_timer(&isa->timer);

	/*
	 * Free resources.
	 * FIXME:  make sure the timer func is not running before freeing
	 * device->buffer
	 */

	vfree(device->buffer);
	free_dma(isa->dma);

	return;
}


/*
 * bkr_isa_read()
 *
 * Doesn't actually "read" data... just makes sure the requested length of
 * data starting at device.tail is available in the I/O buffer.  Returns 0
 * on success, -EAGAIN if the data isn't available or any pending error
 * code.
 */

static int bkr_isa_read(bkr_device_t *device, unsigned int length)
{
	int  result = 0;
	bkr_isa_private_t  *isa = (bkr_isa_private_t *) device->private;

	if(isa->error)
		{
		result = isa->error;
		isa->error = 0;
		}
	else if(bytes_in_buffer(device->head, device->tail, device->size) < length)
		result = -EAGAIN;

	return(result);
}


/*
 * bkr_isa_write()
 *
 * Doesn't actually "write" data... just makes sure the requested amount of
 * space is available starting at the I/O buffer head.  Returns 0 on
 * success, -EAGAIN if the space isn't available or or any pending error
 * code.
 */

static int bkr_isa_write(bkr_device_t *device, unsigned int length)
{
	int  result = 0;
	bkr_isa_private_t  *isa = (bkr_isa_private_t *) device->private;

	if(isa->error)
		{
		result = isa->error;
		isa->error = 0;
		}
	else if(space_in_buffer(device->head, device->tail, device->size) < length)
		result = -EAGAIN;

	return(result);
}


/*
 * bkr_isa_flush()
 *
 * Pad the data stream to the next frame boundary then wait for the buffers
 * to empty.  Returns 0 when the data has been flushed, -EAGAIN if the data
 * cannot be immediately flushed or any error code returned by
 * update_dma_offset() or bkr_device_write().
 */

static int bkr_isa_flush(bkr_device_t *device)
{
	int  count, result;
	bkr_isa_private_t  *isa = (bkr_isa_private_t *) device->private;

	/*
	 * Fill the secondary buffer to a frame boundary.
	 */

	count = device->head % device->frame_size;
	if(count != 0)
		{
		count = device->frame_size - count;
		result = bkr_isa_write(device, count);
		if(result < 0)
			return(result);
		memset(device->buffer + device->head, 0, count);
		device->head += count;
		if(device->head == device->size)
			device->head = 0;
		}

	/*
	 * Don't proceed until the secondary buffer is empty.
	 */

	result = bkr_isa_write(device, device->size - 1);
	if(result < 0)
		return(result);

	/*
	 * Pad out the DMA buffer by the size of the card's internal
	 * buffer.  FIXME: what if count < 0?
	 */

	count = INTERNAL_BUFFER - isa->head % device->frame_size;
	if(count > 0)
		{
		if(space_in_buffer(isa->head, isa->tail, isa->size) < count)
			return(-EAGAIN);
		memset(isa->buffer + isa->head, 0, count);
		isa->head += count;
		}

	/*
	 * Wait for the DMA buffer to empty.
	 */

#ifdef WORKAROUND
	while((bytes_in_buffer(isa->head, isa->tail, isa->size) >= device->bytes_per_line) && !result)
#else
	while(bytes_in_buffer(isa->head, isa->tail, isa->size) && !result)
#endif
		{
		result = update_dma_offset(isa, &isa->tail);
		if(bytes_in_buffer(isa->head, isa->tail, isa->size) >= device->frame_size)
			result = -EAGAIN;
		}

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
	bkr_isa_inc_use_count,
	bkr_isa_dec_use_count,
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
	bkr_isa_private_t  *isa;

	if(request_region(ioport, 1, bkr_isa_resource_name) == NULL)
		goto no_ioport;

	unit = bkr_device_register(BKR_ISA_DEVICE, &bkr_isa_dev_ops);
	if(unit == NULL)
		goto cant_register;

	isa = (bkr_isa_private_t *) kmalloc(sizeof(bkr_isa_private_t), GFP_KERNEL);
	if(isa == NULL)
		goto no_private_mem;
	unit->device.private = isa;

	isa->buffer = (unsigned char *) pci_alloc_consistent(NULL, DMA_BUFFER_SIZE, &isa->dma_addr);
	if(isa->buffer == NULL)
		{
		printk(KERN_WARNING MODULE_NAME ": %s: can't allocate DMA buffer\n", unit->name);
		goto no_dma_buffer;
		}

	isa->ioport = ioport;
	isa->dma = dma;

	printk(KERN_INFO MODULE_NAME ": %s: %s I/O port %#x, DMA channel %u\n", unit->name, msg, ioport, dma);

	return(unit);

	no_dma_buffer:
		kfree(isa);
	no_private_mem:
		bkr_device_unregister(unit);
	cant_register:
		release_region(ioport, 1);
	no_ioport:
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
	unsigned int  count = 0;
	unsigned int  ioport;
	unsigned int  i, dma[2], num_dma;

	/*
	 * Register any user-requested devices.
	 */

	for(i = 0; i < 2*BKR_MAX_INSMOD_DEVICES; )
		{
		ioport = isa[i++];
		dma[0] = isa[i++];
		if((dma[0] == 0) || (ioport == 0))
			break;
		if(check_region(ioport, 1))
			/* FIXME: we need to do this check because
			 * otherwise the kernel lets us request an I/O port
			 * that we already own */
			continue;
		if(bkr_isa_new(ioport, dma[0], "using") != NULL)
			count++;
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

		if(check_region(ioport, 1))
			continue;
		outb(BKR_BIT_NTSC_VIDEO, ioport);
		if((inb(ioport) & ~BKR_BIT_DATA & ~BKR_BIT_SYNC & ~BKR_BIT_FRAME_BUSY) != 0)
			continue;

		/*
		 * Try starting a transfer and watch for DREQ activity.
		 */

		outb(BKR_BIT_DMA_REQUEST, ioport);
		outb(BKR_BIT_DMA_REQUEST | BKR_BIT_TRANSMIT, ioport);
		for(i = 0, bailout = jiffies + HZ/MIN_SYNC_FREQ; 1; )
			{
			if(get_dreq_status(dma[i]))
				{
				outb(0, ioport);
				if(bkr_isa_new(ioport, dma[i], "found at") != NULL)
					count++;
				break;
				}
			if(time_after_eq(jiffies, bailout))
				{
				if(++i >= num_dma)
					break;
				bailout = jiffies + HZ/MIN_SYNC_FREQ;
				}
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
	bkr_isa_private_t  *isa;
	struct list_head  *curr;

	for(curr = bkr_unit_list.next; curr != &bkr_unit_list; )
		{
		unit = list_entry(curr, bkr_unit_t, list);
		curr = curr->next;
		if(unit->device.type != BKR_ISA_DEVICE)
			continue;

		isa = (bkr_isa_private_t *) unit->device.private;
		pci_free_consistent(NULL, DMA_BUFFER_SIZE, isa->buffer, isa->dma_addr);
		release_region(isa->ioport, 1);
		bkr_device_unregister(unit);
		}
}


module_init(backer_isa_init);
module_exit(backer_isa_exit);
