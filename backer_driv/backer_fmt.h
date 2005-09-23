/*
 * backer_fmt.h
 *
 * Driver for Backer 16/32 video tape backup devices.
 *
 * Header for data formatting layer.
 *
 * The device and formating layers, while separate, do expect each other to
 * perform certain tasks so their use is inter-twined.  Here's the order in
 * which things should be done.
 *
 * Do some pre-cleaning.
 *
 *	device.state = BKR_STOPPED;
 *	sector.buffer = NULL;
 *
 * Reset the two layers in order.
 *
 *	bkr_format_reset();
 *
 * Start the transfer.
 *
 *      device.ops->start();
 *
 * Read/write data.
 *
 *	sector.read();                  (repeat as needed if reading)
 *      sector.write();                 (repeat as needed if writing)
 *
 * End the recording (only if writing)
 *
 *	bkr_write_eor();
 *	device.ops->flush();
 *
 * Stop the transfer.
 *
 *	device.ops->stop();
 *
 * Final clean up.
 *
 *	free(sector.buffer);
 *
 * Note:  In order to support multiple formats, reading and writing is done
 * via function pointers in the sector structure.  The pointers are set
 * according to the mode passed to bkr_format_reset() in the device
 * structure.  During formated writes, calls to write are directed to the
 * BOR mark generator until the BOR has been fully written after which
 * calls are directed to the normal sector writer.
 *
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


#ifndef BACKER_FMT_H
#define BACKER_FMT_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <sys/types.h>
#endif

#include "backer.h"
#include "backer_device.h"
#include "rs.h"

#if !defined __BYTE_ORDER
#if defined __LITTLE_ENDIAN && !defined __BIG_ENDIAN
#define __BYTE_ORDER __LITTLE_ENDIAN
#elif !defined __LITTLE_ENDIAN && defined __BIG_ENDIAN
#define __BYTE_ORDER __BIG_ENDIAN
#else
#error Oh oh:  byte order problems!  Figure it out yourself.
#endif
#endif


/*
 * Paramters
 */

#define  BOR_LENGTH    5                /* second(s) */
#define  EOR_LENGTH    1                /* second(s) */
#define  BKR_MAX_KEY_WEIGHT  64


/*
 * Header structure
 *
 * 26 bit sector number = about 155 hours in NTSC, or 186 hours in PAL.
 */

#if __BYTE_ORDER == __LITTLE_ENDIAN

typedef struct
	{
	int32_t  number : 25;           /* sector number */
	u_int32_t  low_used : 4;        /* low 4 bits of encoded usage */
	u_int32_t  id : 3;              /* stream id */
	} bkr_sector_header_t;

#define  SECTOR_HEADER_INITIALIZER  ((bkr_sector_header_t) { -1, 0, 0 })

#else /* __BYTE_ORDER */

typedef struct
	{
	u_int32_t  id : 3;             /* stream id */
	u_int32_t  low_used : 4;       /* low 4 bits of encoded usage */
	int32_t  number : 25;          /* sector number */
	} bkr_sector_header_t;

#define  SECTOR_HEADER_INITIALIZER  ((bkr_sector_header_t) { 0, 0, -1 })

#endif /* __BYTE_ORDER */


/*
 * Types
 */

typedef struct
	{
	unsigned int  symbol;
	unsigned int  recent_symbol;
	unsigned int  block;
	unsigned int  frame;
	unsigned int  overrun;
	unsigned int  underrun;
	} bkr_errors_t;

#define  BKR_ERRORS_INITIALIZER  ((bkr_errors_t) {0, 0, 0, 0, 0, 0})

typedef struct
	{
	unsigned int  total_errors;     /* total bad bytes corrected */
	unsigned int  worst_key;
	unsigned int  best_nonkey;
	int  last_field_offset;         /* offset of last field in I/O buffer */
	unsigned int  smallest_field;   /* smallest video field so far */
	unsigned int  largest_field;    /* largest video field so far */
	} bkr_health_t;

#define  BKR_HEALTH_INITIALIZER  ((bkr_health_t) { 0, BKR_MAX_KEY_WEIGHT, 0, -1, ~0, 0})

struct bkr_stream_t
	{
	unsigned char  *buffer;         /* uninterleaved sector buffer */
	unsigned char  *pos;            /* location of next byte */
	unsigned char  *end;            /* see diagram above */
	bkr_format_info_t  fmt;         /* sector format information */
	rs_format_t  rs_format;         /* Reed-Solomon format information */
	int  need_sequence_reset;       /* sector sequence needs to be reset */
	int  not_underrunning;          /* 0 == sector is in an underrun */
	int  op_count;                  /* counter for misc operations */
	bkr_sector_header_t  header;    /* sector header copy */
	int  (*read)(bkr_device_t *, struct bkr_stream_t *);   /* read sector */
	int  (*write)(bkr_device_t *, struct bkr_stream_t *);  /* write sector */
	bkr_errors_t  errors;           /* error counts */
	bkr_health_t  health;           /* health indicators */
	};

typedef struct bkr_stream_t bkr_stream_t;


/*
 * Functions exported by formating layer.  The read and write functions are
 * accessed through pointers in the sector data structure.
 */

extern int bkr_format_reset(bkr_device_t *, bkr_stream_t *, int, bkr_state_t);
extern int bkr_sector_write_eor(bkr_device_t *, bkr_stream_t *);


/*
 * Macros
 */

static inline int bkr_sector_capacity(bkr_format_info_t *fmt)
{
	return(fmt->data_size - sizeof(bkr_sector_header_t));
}


#endif /* BACKER_FMT_H */
