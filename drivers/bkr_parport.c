/*
 * Linux device driver for Danmere's Backer 16/32 video tape backup devices.
 *
 *                       Parallel Port Device I/O
 *
 * Copyright (C) 2000,2001,2002  Kipp C. Cannon
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
 * A note about the interaction of this code with the Linux parport
 * subsystem.  It comes down to the fact that the external Backer device is
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
 * control to the calling function.
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
#include <linux/vmalloc.h>
#include <linux/wait.h>

#include <asm/dma.h>

#include <backer.h>
#include <bkr_unit.h>
#include <bkr_ring_buffer.h>


/*
 * ========================================================================
 *
 *                          Module info and options
 *
 * ========================================================================
 */

MODULE_AUTHOR("Kipp Cannon (kcannon@users.sourceforge.net)");
MODULE_DESCRIPTION("Backer 16 & 32 device driver --- parport device support");
MODULE_SUPPORTED_DEVICE("backer_parport");
MODULE_LICENSE("GPL");
MODULE_ALIAS("backer_lowlevel");

static char  *units = "";
module_param(units, charp, 0);
MODULE_PARM_DESC(units, "units=parport[:parport]...");

static int  adjust = 0;
module_param(adjust, int, 0);


/*
 * ========================================================================
 *
 *                         Constants/Macros/Etc.
 *
 * ========================================================================
 */

#define  MODULE_NAME            "backer_parport"
#define  DMA_IO_TO_MEM          0x54    /* single mode, inc addr, auto-init */
#define  DMA_MEM_TO_IO          0x58    /* single mode, inc addr, auto-init */
#define  ECR_MODE               0x68    /* Linux parport and Backers suck */
#define  DMA_BUFFER_SIZE        65536
#define  FRAME_TIME             HZ/BKR_MIN_FRAME_FREQ	/* jiffies */
#define  BKR_PARPORT_TIMEOUT    (30*FRAME_TIME)         /* ~1 second */
#define  REQUIRED_MODES         (PARPORT_MODE_PCSPP | PARPORT_MODE_ECP | PARPORT_MODE_DMA)

#if FRAME_TIME < 2
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

static struct workqueue_struct *detach_queue;

typedef struct {
	struct pardevice  *dev;         /* our parport device */
	dma_addr_t  dma_addr;           /* DMA buffer's bus address */
	struct bkr_unit_t *unit;        /* link back to unit for do_detach() */
	struct work_struct detach;      /* detach work queue info */
	struct parport_state  state;    /* Linux parport and Backers suck */
	jiffies_t  last_io;             /* jiffies counter at last I/O */
	int  adjust;                    /* adjustment to start-up pause */
} bkr_parport_private_t;


/*
 * ========================================================================
 *
 *                           Support Functions
 *
 * ========================================================================
 */

/*
 * Write a byte to a parallel port's ECR.
 */

static inline void write_ecr(struct parport *port, char byte)
{
	outb(byte, port->base_hi + 0x02);
}


/*
 * Round-robins the DMA controller through sequential frame-sized chunks of
 * the ring buffer and updates the hardware side of the ring.
 */

static void bkr_parport_irq(void *handle)
{
	struct bkr_stream_t  *stream = handle;
	struct ring  *ring = stream->ring;
	bkr_parport_private_t  *private = (bkr_parport_private_t *) stream->private;
	struct parport  *port = private->dev->port;
	unsigned long  flags;

	flags = claim_dma_lock();
	ring_lock(ring);	/* FIXME:  ok in an interrupt? */
	clear_dma_ff(port->dma);
	switch(stream->direction) {
		case BKR_READING:
		_ring_fill(ring, stream->capacity);
		set_dma_addr(port->dma, private->dma_addr + ring->head);
		break;

		case BKR_WRITING:
		_ring_drain(ring, stream->capacity);
		set_dma_addr(port->dma, private->dma_addr + ring->tail);
		break;

		default:
		ring_unlock(ring);	/* FIXME:  ok in an interrupt? */
		release_dma_lock(flags);
		return;
	}
	write_ecr(port, ECR_MODE);
	ring_unlock(ring);	/* FIXME:  ok in an interrupt? */
	release_dma_lock(flags);
	private->last_io = jiffies;

	/* FIXME: don't do this here --- schedule it on a queue */
	bkr_stream_do_callback(stream);
}


static void bkr_parport_first_irq(void *handle)
{
	bkr_parport_private_t  *private = (bkr_parport_private_t *) ((struct bkr_stream_t *) handle)->private;
	struct parport  *port = private->dev->port;

	private->dev->irq_func = bkr_parport_irq;
	port->ops->write_control(port, 0);
	write_ecr(port, ECR_MODE);
	private->last_io = jiffies;
}


static void transmit_control_byte(struct parport *port, unsigned char byte)
{
	void (*write_control)(struct parport *, unsigned char) = port->ops->write_control;
	void (*write_data)(struct parport *, unsigned char) = port->ops->write_data;

	write_control(port, 0);
	write_control(port, 1);
	write_data(port, 0xd4);
	write_data(port, 0xd5);
	write_data(port, byte);
	write_control(port, 0);
}


/*
 * Ensure the DMA buffer is filled to a frame boundary, and check if it's
 * empty.
 */

static int flush(struct bkr_stream_t *stream)
{
	return(bkr_stream_fill_to(stream, stream->capacity, BKR_FILLER) < 0 ? -EAGAIN : bytes_in_ring(stream->ring) ? -EAGAIN : 0);
}


/* FIXME: should we bother worrying about this? */
static int check_for_timeout(bkr_parport_private_t *private)
{
	return(time_after(jiffies, private->last_io + BKR_PARPORT_TIMEOUT));
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
	bkr_parport_private_t  *private = (bkr_parport_private_t *) stream->private;
	struct parport  *port = private->dev->port;
	unsigned long  flags;
	int  pause;

	/*
	 * Grab the parallel port and ensure the device has been reset.
	 */

	if(parport_claim(private->dev))
		return(-EBUSY);
	port->ops->init_state(private->dev, &private->state);
	port->ops->save_state(port, &private->state);
	transmit_control_byte(port, 0);

	/*
	 * Initialize the DMA channel and buffer, then start the transfer.
	 */

	stream->direction = direction;

	if(BKR_VIDEOMODE(stream->mode) == BKR_NTSC)
		pause = BKR_LINE_PERIOD * (BKR_FIRST_LINE_NTSC - 1);
	else
		pause = BKR_LINE_PERIOD * (BKR_FIRST_LINE_PAL - 1);
	if(BKR_DENSITY(stream->mode) == BKR_HIGH)
		/* pause must be adjusted by +[23,27] microseconds for NTSC
		 * high */
		pause += 25;	/* == 4.7 bytes */
	else
		/* pause must be adjusted by +[41,78] microseconds for NTSC
		 * low */
		pause += 60;	/* == 5.6 bytes */
	if((pause += private->adjust) < 0)
		pause = 0;

	flags = claim_dma_lock();
	disable_dma(port->dma);
	clear_dma_ff(port->dma);
	set_dma_addr(port->dma, private->dma_addr);
	set_dma_count(port->dma, stream->capacity);
	if(direction == BKR_WRITING) {
		set_dma_mode(port->dma, DMA_MEM_TO_IO);
		port->ops->data_forward(port);
	} else {
		set_dma_mode(port->dma, DMA_IO_TO_MEM);
		port->ops->data_reverse(port);
	}
	enable_dma(port->dma);
	transmit_control_byte(port, bkr_control(stream->mode, stream->direction));
	udelay(pause);
	write_ecr(port, ECR_MODE);
	release_dma_lock(flags);

	/*
	 * All done.
	 */

	private->last_io = jiffies;
	return(0);
}


static int release(struct bkr_stream_t *stream)
{
	bkr_parport_private_t  *private = (bkr_parport_private_t *) stream->private;
	struct parport  *port = private->dev->port;
	int  result;

	if(stream->direction != BKR_STOPPED) {
		if(stream->direction == BKR_WRITING) {
			result = flush(stream);
			if(result < 0)
				return(result);
		}

		/* to stop irq handler from doing things */
		stream->direction = BKR_STOPPED;
		/* note: we get woken up before the IRQ handler is called */
		parport_wait_event(port, HZ/BKR_MIN_FRAME_FREQ);
		/* FIXME: should restore be done after transmit and negotiate? */
		port->ops->restore_state(port, &private->state);
		transmit_control_byte(port, 0);
		parport_negotiate(port, IEEE1284_MODE_COMPAT);
		parport_release(private->dev);
		private->dev->irq_func = bkr_parport_first_irq;
	}
	return(0);
}


static int read(struct bkr_stream_t *stream)
{
	return(check_for_timeout(stream->private) ? -ETIMEDOUT : bkr_simple_stream_read(stream));
}


static int write(struct bkr_stream_t *stream)
{
	return(check_for_timeout(stream->private) ? -ETIMEDOUT : bkr_simple_stream_write(stream));
}


static struct bkr_stream_t *new(struct bkr_stream_t *stream, int mode, const bkr_format_info_t *fmt)
{
	stream->mode = mode;
	stream->direction = BKR_STOPPED;
	stream->fmt = *fmt;
	stream->capacity = fmt->frame_size;
	stream->ring->size = DMA_BUFFER_SIZE - DMA_BUFFER_SIZE % stream->capacity;
	ring_reset(stream->ring);
	memset_ring(stream->ring, 0, stream->ring->size);

	return(stream);
}


/*
 * ========================================================================
 *
 *                          Parport Driver API
 *
 * ========================================================================
 */

static void do_detach(struct work_struct *work)
{
	bkr_parport_private_t  *private = container_of(work, bkr_parport_private_t, detach);
	struct bkr_unit_t  *unit = private->unit;
	struct bkr_stream_t  *stream = unit->devstream;

	down(&bkr_unit_list_lock);
	bkr_unit_unregister(unit);
	parport_unregister_device(private->dev);
	dma_free_coherent(NULL, DMA_BUFFER_SIZE, stream->ring->buffer, private->dma_addr);
	kfree(stream->ring);
	kfree(private);
	kfree(stream);
	up(&bkr_unit_list_lock);
}


static void detach(struct parport *port)
{
	struct bkr_unit_t  *unit;
	bkr_parport_private_t  *private;
	struct list_head  *curr;

	down(&bkr_unit_list_lock);
	list_for_each(curr, &bkr_unit_list) {
		unit = list_entry(curr, struct bkr_unit_t, list);
		if(unit->owner != THIS_MODULE)
			continue;
		private = (bkr_parport_private_t *) unit->devstream->private;
		if(private->dev->port == port)
			queue_work(detach_queue, &private->detach);
	}
	up(&bkr_unit_list_lock);
	if(!module_refcount(THIS_MODULE))
		flush_workqueue(detach_queue);
	return;
}


static void attach(struct parport *port)
{
	struct bkr_unit_t  *unit;
	struct bkr_stream_t  *stream;
	bkr_parport_private_t  *private;
	char  *option = units;
	int  length;
	char  msg[100];

	while(1) {
		length = min(strlen(port->name), strlen(option));
		if(length && !strncmp(option, port->name, length))
			break;
		option += length;
		if(*option == '\0')
			goto wrong_port;
		option++;
	}

	sprintf(msg, "%s does not support the required features", port->name);
	if((port->modes & REQUIRED_MODES) != REQUIRED_MODES)
		goto port_not_suitable;

	sprintf(msg, "%s: out of memory", port->name);
	private = kmalloc(sizeof(*private), GFP_KERNEL);
	stream = kmalloc(sizeof(*stream), GFP_KERNEL);
	if(!private || !stream)
		goto no_stream;
	stream->ring = kmalloc(sizeof(*stream->ring), GFP_KERNEL);
	if(!stream->ring)
		goto no_stream;

	sprintf(msg, "%s: can't allocate DMA buffer", port->name);
	stream->ring->buffer = dma_alloc_coherent(NULL, DMA_BUFFER_SIZE, &private->dma_addr, GFP_ATOMIC);
	if(!stream->ring->buffer)
		goto no_dma_buffer;

	sprintf(msg, "can't register a device on %s", port->name);
	private->dev = parport_register_device(port, MODULE_NAME, NULL, NULL, bkr_parport_first_irq, 0, NULL);
	if(!private->dev)
		goto no_parport;

	private->adjust = adjust;	/* FIXME: allow per-device adjustment */

	stream->ops = (struct bkr_stream_ops_t) {
		.new = new,
		.start = start,
		.release = release,
		.read = read,
		.write = write,
	};
	stream->private = private;
	stream->source = NULL;
	bkr_stream_set_callback(stream, NULL, NULL);
	stream->timeout = BKR_PARPORT_TIMEOUT;

	sprintf(msg, "device creation failed");
	down(&bkr_unit_list_lock);
	unit = bkr_unit_register(stream);
	if(!unit)
		goto no_unit;
	private->unit = unit;	/* for do_detach() */
	INIT_WORK(&private->detach, do_detach);
	unit->owner = THIS_MODULE,
	up(&bkr_unit_list_lock);

	printk(KERN_INFO MODULE_NAME ": unit %s: attached to %s", unit->name, port->name);
	if(private->adjust)
		printk(", adjusted %+d µs", private->adjust);
	printk("\n");

	return;

	no_unit:
		up(&bkr_unit_list_lock);
		parport_unregister_device(private->dev);
	no_parport:
		dma_free_coherent(NULL, DMA_BUFFER_SIZE, stream->ring->buffer, private->dma_addr);
	no_dma_buffer:
		kfree(stream->ring);
	no_stream:
		kfree(private);
		kfree(stream);
	port_not_suitable:
		printk(KERN_INFO MODULE_NAME ": %s\n", msg);
	wrong_port:
		return;
}


/*
 * ========================================================================
 *
 *                           Backer Device API
 *
 * ========================================================================
 */

static struct parport_driver bkr_parport_driver = {
	MODULE_NAME,
	attach,
	detach
};

/*
 * Simply add and remove ourselves to/from parport's driver list.  Actual
 * device probing/adding/removal/etc. takes place in the attach() and
 * detach() functions that are call-backs from the parport driver.
 */

static int __init bkr_parport_init(void)
{
	if(*units == '\0') {
		printk(KERN_INFO MODULE_NAME ": no parallel ports specified!\n");
		return(-ENODEV);
	}

	detach_queue = create_singlethread_workqueue("bkrdetach");
	if(!detach_queue)
		return(-ENODEV);

	if(parport_register_driver(&bkr_parport_driver) < 0) {
		destroy_workqueue(detach_queue);
		return(-ENODEV);
	}

	return(0);
}

static void __exit bkr_parport_exit(void)
{
	parport_unregister_driver(&bkr_parport_driver);
	flush_workqueue(detach_queue);
	destroy_workqueue(detach_queue);
}

module_init(bkr_parport_init);
module_exit(bkr_parport_exit);
