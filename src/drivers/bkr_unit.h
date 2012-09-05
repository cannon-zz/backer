/*
 * Unit description for Backer device driver.
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
 */


#ifndef  _BACKER_UNIT_H
#define  _BACKER_UNIT_H

#include <linux/semaphore.h>

#include <linux/list.h>
#include <linux/sysctl.h>

#include <backer.h>
#include <bkr_ring_buffer.h>


/*
 * ========================================================================
 *
 *                           UNIT DESCRIPTIONS
 *
 * ========================================================================
 */


#define BKR_FILLER 0x33         /* Generic filler */
#define  BKR_NAME_LENGTH  (sizeof("99"))        /* limits driver to 100 units */


enum bkr_direction_t {
	BKR_STOPPED = 0,
	BKR_READING,
	BKR_WRITING
};


struct bkr_sysctl_table_t {
	struct ctl_table_header  *header;
	struct ctl_table  dev_dir[2];
	struct ctl_table  driver_dir[2];
	struct ctl_table  unit_dir[2];
	struct ctl_table  entries[3];
};


struct bkr_stream_private_t;


struct bkr_unit_t {
	struct list_head  list;         /* next/prev in list */
	char  name[BKR_NAME_LENGTH];    /* name */
	unsigned int  number;           /* unit number */
	struct semaphore  lock;         /* down == unit is claimed */
	struct module  *owner;          /* module owning this unit */
	wait_queue_head_t  queue;       /* I/O event queue */
	struct bkr_sysctl_table_t  sysctl;     /* sysctl interface table */
	int  last_error;                /* Pending error code if != 0 */
	struct bkr_stream_t {
		struct ring  *ring;             /* this stream's I/O ring */
		unsigned int frame_size;        /* two video fields */
		struct bkr_stream_ops_t {
			struct bkr_stream_t  *(*ready)(struct bkr_stream_t *, int, unsigned int);
			int  (*start)(struct bkr_stream_t *, enum bkr_direction_t);
			int  (*release)(struct bkr_stream_t *);
			int  (*read)(struct bkr_stream_t *);
			int  (*write)(struct bkr_stream_t *);
		} ops;                          /* stream control functions */
		int  mode;                      /* stream settings */
		volatile enum bkr_direction_t  direction;     /* stream state */
		void  (*callback)(void *);      /* I/O activity call-back */
		void  *callback_data;           /* call-back data */
		unsigned int  timeout;          /* I/O activity timeout */
		struct bkr_stream_private_t  *private; /* per-stream private data */
	} *stream;                      /* this unit's data stream */
};                                      /* unit information */


extern struct list_head  bkr_unit_list;         /* list of installed units */
extern struct semaphore  bkr_unit_list_lock;
extern struct bkr_unit_t *bkr_unit_register(struct bkr_stream_t *);
extern void bkr_unit_unregister(struct bkr_unit_t *);


/*
 * Generate the Backer device control byte from the given video mode,
 * density and transfer direction.
 */


static unsigned char bkr_control(int mode, enum bkr_direction_t direction)
{
	unsigned char  control;

	control = BKR_BIT_DMA_REQUEST;
	if(BKR_DENSITY(mode) == BKR_HIGH)
		control |= BKR_BIT_HIGH_DENSITY;
	if(BKR_VIDEOMODE(mode) == BKR_NTSC)
		control |= BKR_BIT_NTSC_VIDEO;
	control |= direction == BKR_WRITING ? BKR_BIT_TRANSMIT : BKR_BIT_RECEIVE;

	return control;
}


/*
 * Stream callback manipulation
 */


static void bkr_stream_set_callback(struct bkr_stream_t *stream, void (*callback)(void *), void *data)
{
	stream->callback_data = data;
	stream->callback = callback;
}


static void bkr_stream_do_callback(struct bkr_stream_t *stream)
{
	if(stream->callback)
		stream->callback(stream->callback_data);
}


#endif /* _BACKER_UNIT_H */
