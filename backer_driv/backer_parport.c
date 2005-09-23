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

/*
 * A note about the "Linux parport and Backers suck" comments throughout
 * this code.  It comes down to the fact that the external Backer device is
 * not IEEE-1284 compliant despite the fact that it uses an IEEE-1284
 * protocol (namely "ECP") to transfer data between itself and the
 * computer.  In particular, on the basis of I/O traces of Danmere's MS-DOS
 * utilities it appears that the external Backer does not understand
 * IEEE-1284 daisy-chain protocols or negotiation phases.  The device
 * requires a command byte to be written to it in order to configure,
 * start, and stop data transfers and this is done using a custom
 * "pass-through" addressing protocol rather than the industry-standard
 * IEEE-1284 daisy-chain protocol.  When a transfer has been started, data
 * is read and written via the ECP protocol but the device immediately
 * starts transfering data in this protocol without first passing through
 * the (required) IEEE-1284 protocol negotiation phase.  Finally, contrary
 * to the IEEE-1284 specifications, the device requires the data transfer
 * to continue uninterrupted until such time as the host computer stops it
 * by transmitting the appropriate command byte.
 *
 * All of these short-comings of the Backer hardware are, somewhat,
 * excusable since Backer devices were both manufactured and subsequently
 * discontinued all before the IEEE-1284 standard was finalized.  However,
 * drafts were readily available as well as early one-chip hardware
 * implementations so there is no reason why the protocol could not have
 * been properly adhered to.
 *
 * Unfortunately, these shortcomings put the external Backer device beyond
 * the limits of what can be tolerated by the Linux parport subsystem.  The
 * result is that in order to talk to the device, this driver must (almost)
 * completely by-pass the parport subsystem.  The only functions of the
 * subsystem that are used are the port identification and setup routines.
 * All low-level hardware I/O is handled by this driver.  This is
 * unfortunate because the Linux parport subsystem includes a complete
 * software implementation of the IEEE-1284 ECP protocol which could be
 * used to allow Backer devices to be attached to any computer having a
 * somewhat standard parallel port.  As things stand, however, this is not
 * possible without a tremendous amount of wheel-reinvention.
 *
 * The one "feature" of the parport subsystem that is most problematic is
 * its inability to maintain an uninterrupted data stream into or out of
 * the port.  The design flaw that prevents it from doing this is its
 * insistance on reading and writing data in a piece-meal fashion:  it sits
 * in IEEE-1284 "compatibility" mode until asked to read or write some
 * quantity of data at which point it puts the port through a negotiation
 * phase into ECP mode, transfers the data, waits for the transfer to
 * complete and then negotiates back to compatibility mode before returning
 * control to the calling function.  Ideally, the subsystem should support
 * a "protocol request" method that is used by device drivers to
 * specifically request protocol negotiations only when needed.  Once in a
 * protocol, the subsystem should remain there until forced out of it due
 * to an IEEE-1284 protocol failure or another change protocol request from
 * a driver.  This would elliminate the superfluous ECP negotiations that
 * the Backer device cannot handle AND would result in an across-the-board
 * performance improvement in the parport subsytem.
 *
 * Thence the comments.  Between the shortcomings of both, the Linux
 * parport subsystem and the external Backer conspire to be incapable of
 * functioning together.
 */

#include <linux/module.h>
#include <linux/init.h>

#include <linux/errno.h>
#include <linux/list.h>
#include <linux/parport.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/tqueue.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>

#include <asm/dma.h>

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

static char  *units = "";
MODULE_PARM(units, "s");
MODULE_PARM_DESC(units, "units=parport[:parport:...]");

static int  adjust = 0;
MODULE_PARM(adjust, "i");

EXPORT_NO_SYMBOLS;


/*
 * ========================================================================
 *                         Constants/macros/etc.
 * ========================================================================
 */

#define  MODULE_NAME            "backer_parport"
#define  DMA_IO_TO_MEM          0x54    /* single mode, inc addr, auto-init */
#define  DMA_MEM_TO_IO          0x58    /* single mode, inc addr, auto-init */
#define  ECR_MODE               0x68    /* FIXME: Linux parport and Backers suck */
#define  DMA_BUFFER_SIZE        8192    /* a power of 2 >= frame_size */
#define  MIN_DMA_FREQUENCY      5       /* Hz */
#define  MAX_ADJUST_MAGNITUDE   (4 * BKR_LINE_PERIOD)
#define  REQUIRED_MODES    (PARPORT_MODE_PCSPP | PARPORT_MODE_ECP | PARPORT_MODE_DMA)


/*
 * ========================================================================
 *                                 Data.
 * ========================================================================
 */

typedef unsigned long  jiffies_t;

static DECLARE_TASK_QUEUE(detach_queue);

typedef struct
	{
	struct pardevice  *dev;         /* our parport device */
	unsigned char  *dma_buffer;     /* DMA buffer */
	unsigned int  dma_size;         /* DMA buffer in use */
	dma_addr_t  dma_addr;           /* DMA buffer's bus address */
	struct tq_struct  queue_entry;  /* detach queue entry */
	struct parport_state  state;    /* FIXME:  Linux parport and Backers suck */
	int  adjust;                    /* adjustment to start-up pause */
	} bkr_parport_private_t;


/*
 * ========================================================================
 *
 *                            MISC FUNCTIONS
 *
 * ========================================================================
 */

/*
 * write_ecr()
 *
 * Write a byte to a parallel port's ECR.
 */

static void write_ecr(struct parport *p, char byte)
{
	outb(byte, p->base_hi + 0x02);
}


/*
 * bkr_parport_irq()
 *
 * Handles data movement between the I/O buffer and the parallel port's DMA
 * buffer by responding to terminal count interrupts.  The trick used here
 * is to only ever move an entire buffer's worth of data at a time while
 * making sure the DMA buffer's size is set so as to not require us to
 * check for wrap-around of the I/O buffer.
 */

static void bkr_parport_irq(int irq, void *handle, struct pt_regs *regs)
{
	bkr_unit_t  *unit = (bkr_unit_t *) handle;
	bkr_device_t  *device = &unit->device;
	bkr_parport_private_t  *private = (bkr_parport_private_t *) device->private;

	switch(device->state)
		{
		case BKR_READING:
		if(space_in_buffer(device->io_head, device->io_tail, device->io_size) >= private->dma_size)
			{
			memcpy(device->io_buffer + device->io_head, private->dma_buffer, private->dma_size);
			device->io_head = (device->io_head + private->dma_size) & BKR_OFFSET_MASK;
			}
		break;

		case BKR_WRITING:
		if(bytes_in_buffer(device->io_head, device->io_tail, device->io_size) >= private->dma_size)
			{
			memcpy(private->dma_buffer, device->io_buffer + device->io_tail, private->dma_size);
			device->io_tail += private->dma_size;
			if(device->io_tail == device->io_size)
				device->io_tail = 0;
			}
		break;

		default:
		return;
		}

	wake_up_interruptible(&unit->io_queue);

	/*
	 * FIXME: Linux parport and Backers suck
	 */

	write_ecr(private->dev->port, ECR_MODE);
}


/*
 * transmit_control()
 *
 * Transmits the control byte to the external Backer device.
 */

static void transmit_control(struct parport *p, unsigned char byte)
{
	void (*write_control)(struct parport *, unsigned char) = p->ops->write_control;
	void (*write_data)(struct parport *, unsigned char) = p->ops->write_data;

	write_control(p, 0);
	write_control(p, 1);
	write_data(p, 0xd4);
	write_data(p, 0xd5);
	write_data(p, byte);
	write_control(p, 0);
}


/*
 * ========================================================================
 *
 *                              DEVICE OPS
 *
 * ========================================================================
 */

/*
 * bkr_parport_start()
 *
 * Start the tape <---> memory data transfer.
 */

static int bkr_parport_start(bkr_device_t *device, bkr_state_t direction)
{
	bkr_parport_private_t  *private = (bkr_parport_private_t *) device->private;
	struct parport  *p = private->dev->port;
	void  (*set_direction)(struct parport *);
	unsigned long  flags;
	int  pause, dma_mode, result;

	/*
	 * Do first 'cause vmalloc() might allow a task switch.
	 */

	device->state = direction;

	/*
	 * Grab the parallel port.
	 */

	if(parport_claim(private->dev))
		{
		result = -EBUSY;
		goto cant_claim_port;
		}

	/*
	 * Allocate and initialize the secondary buffer.
	 */

	device->io_buffer = (unsigned char *) vmalloc(device->io_size);
	if(device->io_buffer == NULL)
		{
		result = -ENOMEM;
		goto no_mem;
		}
	memset(device->io_buffer, 0, device->io_size);

	/*
	 * Set direction-specific things.
	 */

	if(direction == BKR_WRITING)
		{
		dma_mode = DMA_MEM_TO_IO;
		set_direction = p->ops->data_forward;
		private->dma_size = device->frame_size;
		}
	else
		{
		dma_mode = DMA_IO_TO_MEM;
		set_direction = p->ops->data_reverse;
		private->dma_size = DMA_BUFFER_SIZE;
		}

	/*
	 * Initialize the DMA channel and buffer.
	 */

	memset(private->dma_buffer, 0, DMA_BUFFER_SIZE);
	flags = claim_dma_lock();
	disable_dma(p->dma);
	clear_dma_ff(p->dma);
	set_dma_mode(p->dma, dma_mode);
	set_dma_addr(p->dma, private->dma_addr);
	set_dma_count(p->dma, private->dma_size);
	enable_dma(p->dma);
	release_dma_lock(flags);

	/*
	 * Start the transfer.  FIXME:  Linux parport and Backers suck
	 */

#if 0	/* FIXME:  figure out the correct default pause */
	if(BKR_VIDEOMODE(device->mode) == BKR_NTSC)
		pause = BKR_LINE_PERIOD * (BKR_FIRST_LINE_NTSC - 1);
	else
		pause = BKR_LINE_PERIOD * (BKR_FIRST_LINE_PAL - 1);
#else
	pause = 0;
#endif
	pause += private->adjust;
	p->ops->init_state(private->dev, &private->state);
	p->ops->save_state(p, &private->state);
	transmit_control(p, device->control);
	set_direction(p);
	udelay(pause);
	write_ecr(p, ECR_MODE);

	/*
	 * All done.
	 */

	return(0);

	/*
	 * Something went wrong.
	 */

	no_mem:
		parport_release(private->dev);
	cant_claim_port:
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
	struct parport  *p = private->dev->port;

	/*
	 * FIXME:  Linux parport and Backers suck
	 */

	device->state = BKR_STOPPED;
	parport_wait_event(p, HZ/MIN_DMA_FREQUENCY);
	p->ops->restore_state(p, &private->state);
	transmit_control(p, 0);
	parport_negotiate(p, IEEE1284_MODE_COMPAT);
	parport_release(private->dev);
	vfree(device->io_buffer);

	return;
}


/*
 * bkr_parport_read()
 *
 * Doesn't actually "read" data... just makes sure the requested length of
 * data is available starting at the I/O buffer tail.  Returns 0 on
 * success, -EAGAIN if the data isn't available.
 */

static int bkr_parport_read(bkr_device_t *device, unsigned int length)
{
	int  result = 0;

	if(bytes_in_buffer(device->io_head, device->io_tail, device->io_size) < length)
		result = -EAGAIN;

	return(result);
}


/*
 * bkr_parport_write()
 *
 * Doesn't actually "write" data... just makes sure the requested amount of
 * space is available starting at the I/O buffer head.  Returns 0 on
 * success, -EAGAIN if the space isn't available.
 */

static int bkr_parport_write(bkr_device_t *device, unsigned int length)
{
	int  result = 0;

	if(space_in_buffer(device->io_head, device->io_tail, device->io_size) < length)
		result = -EAGAIN;

	return(result);
}


/*
 * bkr_parport_flush()
 *
 * Flushes outbound data from the I/O buffer.  Returns 0 if the data has
 * all be written to the DMA buffer or -EAGAIN if not.  Note that for
 * logistical reasons, the DMA buffer is flushed on the call to stop().
 */

static int bkr_parport_flush(bkr_device_t *device)
{
	int  count, result;

	/*
	 * Fill the I/O buffer to a frame boundary.
	 */

	count = device->io_head % device->frame_size;
	if(count != 0)
		{
		count = device->frame_size - count;
		result = bkr_parport_write(device, count);
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

	result = bkr_parport_write(device, device->io_size - 1);

	return(result);
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
	THIS_MODULE,
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
	pci_free_consistent(NULL, DMA_BUFFER_SIZE, private->dma_buffer, private->dma_addr);
	bkr_unit_unregister(unit);
}


static void bkr_parport_attach(struct parport *p)
{
	bkr_unit_t  *unit;
	bkr_parport_private_t  *private;
	char  *option = units;
	int  length;
	char  msg[100];

	while(1)
		{
		for(length = 0; (option[length] != ':') && (option[length] != '\0'); length++);
		if(!strncmp(option, p->name, length))
			break;
		option += length;
		if(*option == '\0')
			goto wrong_port;
		option++;
		}

	if((p->modes & REQUIRED_MODES) != REQUIRED_MODES)
		{
		sprintf(msg, "%s does not support the required features", p->name);
		goto port_not_suitable;
		}

	unit = bkr_unit_register(PM_UNKNOWN_DEV, &bkr_parport_dev_ops, sizeof(bkr_parport_private_t));
	if(unit == NULL)
		goto no_unit;
	private = (bkr_parport_private_t *) unit->device.private;

	private->dma_buffer = (unsigned char *) pci_alloc_consistent(NULL, DMA_BUFFER_SIZE, &private->dma_addr);
	if(private->dma_buffer == NULL)
		{
		sprintf(msg, "%s: can't allocate DMA buffer", unit->name);
		goto no_dma_buffer;
		}

	INIT_TQUEUE(&private->queue_entry, bkr_parport_do_detach, unit);

	private->adjust = adjust;

	private->dev = parport_register_device(p, MODULE_NAME, NULL, NULL, bkr_parport_irq, 0, unit);
	if(private->dev == NULL)
		{
		sprintf(msg, "%s: can't register a device on %s", unit->name, p->name);
		goto no_parport;
		}

	printk(KERN_INFO MODULE_NAME ": %s: attached to %s", unit->name, p->name);
	if(private->adjust)
		printk(", adjusted %+d µs", private->adjust);
	printk("\n");

	return;

	no_parport:
		pci_free_consistent(NULL, DMA_BUFFER_SIZE, private->dma_buffer, private->dma_addr);
	no_dma_buffer:
		bkr_unit_unregister(unit);
	no_unit:
	port_not_suitable:
	wrong_port:
		printk(KERN_INFO MODULE_NAME ": %s\n", msg);
		return;
}


static void bkr_parport_detach(struct parport *p)
{
	bkr_unit_t  *unit;
	bkr_parport_private_t  *private;
	struct list_head  *next;

	for(next = bkr_unit_list.next; next != &bkr_unit_list; )
		{
		unit = list_entry(next, bkr_unit_t, list);
		next = next->next;
		if(unit->device.ops->owner != THIS_MODULE)
			continue;
		private = (bkr_parport_private_t *) unit->device.private;
		if(private->dev->port == p)
			queue_task(&private->queue_entry, &detach_queue);
		}
	if(!MOD_IN_USE)
		run_task_queue(&detach_queue);
	return;
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
	if(units == '\0')
		{
		printk(KERN_INFO MODULE_NAME ": no parallel ports specified!\n");
		return(-ENODEV);
		}

        if(adjust < -MAX_ADJUST_MAGNITUDE)
                adjust = -MAX_ADJUST_MAGNITUDE;
        else if(adjust > +MAX_ADJUST_MAGNITUDE)
                adjust = +MAX_ADJUST_MAGNITUDE;

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
