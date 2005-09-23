/*
 * backer.h
 * 
 * Header file for Backer 16/32 device driver.
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
 */

#ifndef  _BACKER_H
#define  _BACKER_H

#include <linux/types.h>

/*
 * Data Types
 */

typedef  unsigned long  jiffies_t;              /* type for jiffies */
typedef  unsigned short  f_flags_t;             /* type for f_flags in struct file */


/*
 * Parameters
 *
 * BKR_DEF_MODE must be a real mode or the driver will enter an infinite
 * recursion when loaded.  You've been warned.  (it's a real mode if it has
 * exactly one constant from each group ORed together).
 */

#define  BKR_MAX_TIMEOUT       120              /* seconds */

#ifndef __KERNEL__
#define  HZ  100
#endif


/*
 * Hardware stuff
 */

#define  BYTES_PER_LINE_LOW    4
#define  BYTES_PER_LINE_HIGH   10
#define  LINES_PER_FIELD_NTSC  253
#define  LINES_PER_FIELD_PAL   305
#define  INTERNAL_BUFFER       4

#define  BIT_HIGH_DENSITY      0x01
#define  BIT_DMA_REQUEST       0x02
#define  BIT_DATA              0x04
#define  BIT_SYNC              0x08
#define  BIT_FRAME_BUSY        0x10
#define  BIT_TRANSMIT          0x20
#define  BIT_RECEIVE           0x40
#define  BIT_NTSC_VIDEO        0x80


/*
 * IOCTL stuff
 */

struct bkrerrors                                /* Error counts */
	{
	unsigned int  symbol;                   /* most symbol errors in any one block */
	unsigned int  recent_symbol;            /* most symbol errors since last update */
	unsigned int  block;                    /* uncorrectable blocks since BOR */
	unsigned int  frame;                    /* framing errors since BOR */
	unsigned int  overrun;                  /* buffer overruns since BOR */
	unsigned int  underflow;                /* underflows since BOR */
	};

#define  ERRORS_INITIALIZER  ((struct bkrerrors) {0, 0, 0, 0, 0, 0})

struct bkrhealth
	{
	unsigned int  worst_key;
	unsigned int  best_nonkey;
	unsigned int  least_skipped;
	unsigned int  most_skipped;
	};

#define  HEALTH_INITIALIZER  ((struct bkrhealth) {~0, 0, ~0, 0})

struct bkrstatus                                /* Status structure (read only) */
	{
	unsigned int  bytes;                    /* in DMA buffer */
	struct bkrerrors errors;
	struct bkrhealth health;
	};

struct bkrformat                                /* Format structure (read only) */
	{
	unsigned int  buffer_size;              /* == bytes per frame * an integer */
	unsigned int  sector_size;              /* bytes */
	unsigned int  leader;                   /* bytes */
	unsigned int  trailer;                  /* bytes */
	unsigned int  interleave;               /* interleave ratio */
	unsigned int  block_size;               /* bytes */
	unsigned int  block_parity;             /* bytes */
	unsigned int  block_capacity;           /* bytes */
	unsigned int  sector_capacity;          /* bytes */
	};

struct bkrconfig                                /* Config structure (read/write) */
	{
	unsigned int  mode;                     /* see below */
	unsigned int  timeout;                  /* in seconds */
	};

#define  BKRIOCGETSTATUS       _IOR('m', 10, struct bkrstatus)    /* get status */
#define  BKRIOCGETMODE         _IOR('m', 11, struct bkrconfig)    /* get configuration */
#define  BKRIOCSETMODE         _IOW('m', 11, struct bkrconfig)    /* set configuration */
#define  BKRIOCGETFORMAT       _IOR('m', 12, struct bkrformat)    /* get format */

/* For bkrconfig.mode */

#define  BKR_VIDEOMODE(x)      ((x) & 0x0003)
#define  BKR_DENSITY(x)        ((x) & 0x000c)
#define  BKR_FORMAT(x)         ((x) & 0x0030)
#define  BKR_SPEED(x)          ((x) & 0x00c0)
#define  BKR_NTSC              0x0001           /* NTSC video mode */
#define  BKR_PAL               0x0002           /* PAL video mode */
#define  BKR_LOW               0x0004           /* low density */
#define  BKR_HIGH              0x0008           /* high density */
#define  BKR_FMT               0x0010           /* read/write formated data */
#define  BKR_RAW               0x0020           /* read/write raw data */
#define  BKR_SP                0x0040           /* VCR is in SP/LP mode */
#define  BKR_EP                0x0080           /* VCR is in EP mode */


#endif /* _BACKER_H */
