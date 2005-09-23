/*
 * backer_parport
 *
 * Linux device driver for Danmere's Backer 16/32 video tape backup cards.
 *
 *                       Parallel Port Device I/O
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
#include <linux/list.h>
#include <linux/malloc.h>
#include <linux/parport.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/tqueue.h>
#include <linux/vmalloc.h>

#include "backer.h"
#include "backer_device.h"
#include "backer_unit.h"


/*
 * ========================================================================
 *                          Module info and options
 * ========================================================================
 */

MODULE_AUTHOR("Kipp Cannon (kcannon@users.sourceforge.net)");
MODULE_DESCRIPTION("Backer 16 & 32 device driver --- parport device support");
MODULE_SUPPORTED_DEVICE("backer_parport");

EXPORT_NO_SYMBOLS;


/*
 * ========================================================================
 *                         Constants/macros/etc.
 * ========================================================================
 */

#define  MODULE_NAME       "backer_parport"
#define  DMA_IO_TO_MEM     0x54         /* single mode, inc addr, auto-init */
#define  DMA_MEM_TO_IO     0x58         /* single mode, inc addr, auto-init */
#define  BKR_DATA_ADDR     0            /* data channel address */
#define  BKR_COMMAND_ADDR  1            /* command channel address */
#define  MAX_UPDATE_FREQ   10           /* buffer update frequency in Hz */


/*
 * ========================================================================
 *                          Function Prototypes
 * ========================================================================
 */

static void bkr_parport_do_move(unsigned long);


/*
 * ========================================================================
 *                                 Data.
 * ========================================================================
 */

typedef unsigned long  jiffies_t;

DECLARE_TASK_QUEUE(detach_queue);

typedef struct
	{
	struct pardevice  *dev;
	struct timer_list  timer;
	struct tq_struct  detach_queue;
	int  error;
	} bkr_parport_private_t;


/*
 * ========================================================================
 *
 *                            MISC FUNCTIONS
 *
 * ========================================================================
 */


/*
 * ========================================================================
 *
 *                              DEVICE OPS
 *
 * ========================================================================
 */

/*
 * bkr_parport_*_use_count()
 *
 * Maintain module locking and flush out back-logged hot-plug requests.
 */

static void bkr_parport_inc_use_count(void)
{
	MOD_INC_USE_COUNT;
}

static void bkr_parport_dec_use_count(void)
{
	MOD_DEC_USE_COUNT;
	if(!MOD_IN_USE)
		run_task_queue(&detach_queue);
}


/*
 * bkr_parport_reset()
 *
 * Initialize device-specific data.
 */

static int bkr_parport_reset(bkr_device_t *device, int mode, bkr_state_t direction)
{
	device->mode = mode;

	if(BKR_DENSITY(mode) == BKR_HIGH)
		device->bytes_per_line = BYTES_PER_LINE_HIGH;
	else
		device->bytes_per_line = BYTES_PER_LINE_LOW;

	if(BKR_VIDEOMODE(mode) == BKR_NTSC)
		device->frame_size = device->bytes_per_line * (LINES_PER_FIELD_NTSC * 2 + 1);
	else
		device->frame_size = device->bytes_per_line * LINES_PER_FIELD_PAL * 2;

	device->size = BKR_BUFFER_SIZE;
	if(direction == BKR_WRITING)
		device->size -= BKR_BUFFER_SIZE % device->frame_size;

	return(0);
}


/*
 * bkr_parport_start()
 *
 * Start the tape <---> memory data transfer.
 */

static int bkr_parport_start(bkr_device_t *device, bkr_state_t direction)
{
	int  result;
	bkr_parport_private_t  *private = (bkr_parport_private_t *) device->private;

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
	 * Grab and initialize the parallel port.
	 */

	if(parport_claim(private->dev))
		{
		result = -EBUSY;
		goto cant_claim;
		}

	if(parport_negotiate(private->dev->port, IEEE1284_MODE_ECP))
		{
		result = -EIO;
		goto cant_negotiate;
		}

#if 0	/* FIXME: how do we move data back and forth!? */
	parport_enable_irq(private->dev->port);
#endif

	/*
	 * FIXME: Tell Backer to start.
	 */

	/*
	 * Add the data mover to the kernel timer list.  Force an update
	 * immediately.
	 */

	private->error = 0;
	init_timer(&private->timer);
	private->timer.function = bkr_parport_do_move;
	private->timer.data = (unsigned long) device;
	private->timer.function(private->timer.data);

	/*
	 * All OK
	 */

	return(0);

	/*
	 * Something went wrong.
	 */

	cant_negotiate:
		parport_release(private->dev);
	cant_claim:
		vfree(device->buffer);
		device->state = BKR_STOPPED;
		return(result);
}


/*
 * bkr_parport_stop()
 *
 * Stop the tape <---> memory transfer.
 */

static void bkr_parport_stop(bkr_device_t *device)
{
	bkr_parport_private_t  *private = (bkr_parport_private_t *) device->private;

	device->state = BKR_STOPPED;

	del_timer(&private->timer);

	/* FIXME: tell Backer to stop */

	parport_disable_irq(private->dev->port);

	parport_release(private->dev);

	vfree(device->buffer);

	return;
}


/*
 * bkr_parport_read()
 *
 * Doesn't actually "read" data... just makes sure the requested length of
 * data starting at device.tail is available in the I/O buffer.  Returns 0
 * on success, -EAGAIN if the data isn't available or any pending error
 * code.
 */

static int bkr_parport_read(bkr_device_t *device, unsigned int length)
{
	bkr_parport_private_t  *private = (bkr_parport_private_t *) device->private;
	int  result = 0;

	if(private->error)
		{
		result = private->error;
		private->error = 0;
		}
	if(bytes_in_buffer(device->head, device->tail, device->size) < length)
		result = -EAGAIN;

	return(result);
}


/*
 * bkr_parport_write()
 *
 * Doesn't actually "write" data... just makes sure the requested amount of
 * space is available starting at the I/O buffer head.  Returns 0 on
 * success, -EAGAIN if the space isn't available or or any pending error
 * code.
 */

static int bkr_parport_write(bkr_device_t *device, unsigned int length)
{
	bkr_parport_private_t  *private = (bkr_parport_private_t *) device->private;
	int  result = 0;

	if(private->error)
		{
		result = private->error;
		private->error = 0;
		}
	if(space_in_buffer(device->head, device->tail, device->size) < length)
		result = -EAGAIN;

	return(result);
}


/*
 * bkr_parport_flush()
 */

static int bkr_parport_flush(bkr_device_t *device)
{
	int  count, result;

	/*
	 * Fill the I/O buffer to a frame boundary.
	 */

	count = device->head % device->frame_size;
	if(count != 0)
		{
		count = device->frame_size - count;
		result = bkr_parport_write(device, count);
		if(result < 0)
			return(result);
		memset(device->buffer + device->head, 0, count);
		device->head += count;
		if(device->head == device->size)
			device->head = 0;
		}

	/*
	 * Don't proceed until the I/O buffer is empty.
	 */

	result = bkr_parport_write(device, device->size - 1);
	if(result < 0)
		return(result);

	return(0);
}


/*
 * ========================================================================
 *
 *                       PARPORT SUPPORT ROUTINES
 *
 * ========================================================================
 */

static bkr_device_ops_t  bkr_parport_dev_ops =
	{
	bkr_parport_inc_use_count,
	bkr_parport_dec_use_count,
	bkr_parport_start,
	bkr_parport_stop,
	bkr_parport_read,
	bkr_parport_write,
	bkr_parport_flush
	};

/*
 * bkr_parport_do_detach() bkr_parport_attach() bkr_parport_detach()
 *
 * Handle the addition and removal of parallel ports.
 */

static void bkr_parport_do_detach(void *data)
{
	bkr_unit_t  *unit = (bkr_unit_t *) data;
	bkr_parport_private_t  *private = (bkr_parport_private_t *) unit->device.private;

	parport_unregister_device(private->dev);
	bkr_device_unregister(unit);
}


static void bkr_parport_attach(struct parport *port)
{
	bkr_unit_t  *unit;
	bkr_parport_private_t  *private;

	unit = bkr_device_register(BKR_PARPORT_DEVICE, &bkr_parport_dev_ops);
	if(unit == NULL)
		goto no_unit;

	private = (bkr_parport_private_t *) kmalloc(sizeof(bkr_parport_private_t), GFP_KERNEL);
	if(private == NULL)
		{
		printk(KERN_INFO MODULE_NAME ": out of memory\n");
		goto no_private;
		}
	unit->device.private = private;

	INIT_TQUEUE(&private->detach_queue, bkr_parport_do_detach, unit);

	private->dev = parport_register_device(port, MODULE_NAME, NULL, NULL, NULL, 0, &unit->device);
	if(private->dev == NULL)
		{
		printk(KERN_INFO MODULE_NAME ": %s: can't register as a parport device\n", unit->name);
		goto no_parport;
		}

	printk(KERN_INFO MODULE_NAME ": %s: found on %s\n", unit->name, private->dev->port->name);

	return;

	no_parport:
		kfree(private);
	no_private:
		bkr_device_unregister(unit);
	no_unit:
		return;
}


static void bkr_parport_detach(struct parport *port)
{
	bkr_unit_t  *unit;
	bkr_parport_private_t  *private;
	struct list_head  *next;

	for(next = bkr_unit_list.next; next != &bkr_unit_list; )
		{
		unit = list_entry(next, bkr_unit_t, list);
		next = next->next;
		if(unit->device.type != BKR_PARPORT_DEVICE)
			continue;

		private = (bkr_parport_private_t *) unit->device.private;
		if(private->dev->port != port)
			continue;
		if(MOD_IN_USE)
			queue_task(&private->detach_queue, &detach_queue);
		else
			bkr_parport_do_detach(unit);
		}
	return;
}


/*
 * bkr_parport_do_move()
 *
 * Handles data movement between the I/O buffer and the parallel port.
 */

static void bkr_parport_do_move(unsigned long data)
{
	unsigned int  tmp, count;
	bkr_device_t  *device = (bkr_device_t *) data;
	bkr_parport_private_t  *private = (bkr_parport_private_t *) device->private;
	struct parport  *port = private->dev->port;

	switch(device->state)
		{
		case BKR_READING:
		count = space_in_buffer(device->head, device->tail, device->size);
		tmp = device->size - device->head;
		if(count > tmp)
			count = tmp;
		count = parport_read(port, device->buffer + device->head, count);
		if(count >= 0)
			{
			device->head += count;
			if(device->head == device->size)
				device->head = 0;
			}
		else
			private->error = count;
		break;

		case BKR_WRITING:
		count = bytes_in_buffer(device->head - device->head % device->frame_size, device->tail, device->size);
		tmp = device->size - device->tail;
		if(count > tmp)
			count = tmp;
		count = parport_write(port, device->buffer + device->tail, count);
		if(count >= 0)
			{
			device->tail += count;
			if(device->tail == device->size)
				device->tail = 0;
			}
		else
			private->error = count;
		break;

		default:
		return;
		}

	private->timer.expires = jiffies + HZ/MAX_UPDATE_FREQ;
	add_timer(&private->timer);
}


/*
 * ========================================================================
 *
 *                            ENTRY/EXIT CODE
 *
 * ========================================================================
 */

static struct parport_driver bkr_parport_driver =
	{
	MODULE_NAME,
	bkr_parport_attach,
	bkr_parport_detach
	};

/*
 * backer_parport_init(), backer_parport_exit()
 *
 * Simply add and remove ourselves to/from parport's driver list.  Actual
 * device probing/adding/removal/etc. takes place in the attach() and
 * detach() functions that are call-backs from the parport driver.
 */

static int __init backer_parport_init(void)
{
	if(parport_register_driver(&bkr_parport_driver) < 0)
		return(-ENODEV);
	return(0);
}

static void __exit backer_parport_exit(void)
{
	parport_unregister_driver(&bkr_parport_driver);
}

module_init(backer_parport_init);
module_exit(backer_parport_exit);
