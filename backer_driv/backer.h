/*
 * backer.h
 * 
 * Header file for Backer 16/32 device driver.
 *
 * Copyright (C) 2000  Kipp C. Cannon
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

#ifndef __KERNEL__
#define  HZ  100
#endif

typedef  unsigned long  jiffies_t;          /* type for jiffies */
typedef  unsigned short  f_flags_t;         /* type for f_flags in struct file */

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
 * Device driver stuff
 */

#define  BKR_NAME              "backer"
#define  BKR_VERSION           "0.10(beta)"
#define  BKR_MAJOR             60           /* adjust this for your system */

#define  DEFAULT_IOPORT        0x300        /* adjust this for your system */
#define  DEFAULT_DMA_CHANNEL   3            /* adjust this for your system */
#define  DEFAULT_BUFFER_SIZE   65500        /* bytes */
#define  DEFAULT_TIMEOUT       10           /* seconds */
#define  DEFAULT_MODE          (BKR_NTSC | BKR_LOW | BKR_FMT | BKR_SP)
#define  BKR_MAX_TIMEOUT       120          /* seconds */

#define  DMA_IO_TO_MEM         0x14         /* demand transf, inc addr, auto-init */
#define  DMA_MEM_TO_IO         0x18         /* demand transf, inc addr, auto-init */
#define  DMA_HOLD_OFF          512          /* stay this far back from transfer point */

#define  MIN_UPDATE_FREQ       3            /* minimum rate for DMA status updates in Hz */
#define  MAX_UPDATE_FREQ       50           /* maximum rate for DMA status updates in Hz */
#define  MIN_SYNC_FREQ         50           /* minimum sync frequency in Hz if video OK */

struct bkrerrors                            /* Error counts */
	{
	unsigned int  symbol;               /* most symbol errors in any one block */
	unsigned int  block;                /* uncorrectable blocks since BOR */
	unsigned int  sector;               /* framing errors since BOR */
	unsigned int  overrun;              /* buffer overruns since BOR */
	unsigned int  underflow;            /* underflow warnings since BOR */
	};

#define  ERRORS_INITIALIZER  ((struct bkrerrors) {0, 0, 0, 0, 0})

/*
 * IOCTL stuff
 */

struct bkrstatus                            /* Status structure (read only) */
	{
	unsigned int  bytes;                /* in DMA buffer */
	struct bkrerrors errors;
	unsigned int  worst_match;
	unsigned int  best_nonmatch;
	unsigned int  least_skipped;
	unsigned int  most_skipped;
	};

struct bkrformat                            /* Format structure (read only) */
	{
	unsigned int  bytes_per_line;
	unsigned int  buffer_size;          /* == bytes per frame * an integer */
	unsigned int  sector_size;          /* bytes */
	unsigned int  header_length;        /* bytes */
	unsigned int  footer_length;        /* bytes */
	unsigned int  aux_offset;           /* bytes from start of sector */
	unsigned int  aux_length;           /* bytes */
	unsigned int  block_size;           /* bytes */
	unsigned int  block_capacity;       /* bytes (meaningless in pass-through mode) */
	unsigned int  block_parity;         /* bytes */
	};

struct bkrconfig                            /* Config structure (read/write) */
	{
	unsigned int  mode;                 /* see below */
	unsigned int  timeout;              /* in seconds */
	};

#define  BKRIOCGETSTATUS       _IOR('m', 10, struct bkrstatus)    /* get status */
#define  BKRIOCGETMODE         _IOR('m', 11, struct bkrconfig)    /* get configuration */
#define  BKRIOCSETMODE         _IOW('m', 11, struct bkrconfig)    /* set configuration */
#define  BKRIOCGETFORMAT       _IOR('m', 12, struct bkrformat)    /* get format */
#define  BKRIOCGETAUX          _IOR('m', 13, 0)                   /* read aux buffer */
#define  BKRIOCSETAUX          _IOW('m', 13, 0)                   /* write aux buffer */

/*
 * For bkrconfig.mode
 */

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

/* convert mode to an index for the tape format array (see BKR_FORMATS) */
#define  BKR_MODE_TO_FORMAT(x) ((BKR_DENSITY(x)==BKR_HIGH)<<2  | \
                                (BKR_VIDEOMODE(x)==BKR_PAL)<<1 | \
                                (BKR_SPEED(x)==BKR_EP))

/*
 * General Formating
 *
 * Note KEY_LENGTH and the parity lengths must be multiples of 2.
 *
 * UPCOMING TAPE FORMAT CHANGES
 * -reduce the number of parity symbols (12 & 16 rather than 16 & 20)
 * -new data randomizer
 * -make sector layer responsible or the sector key
 * -possible ellimination of auxiliary region
 * -if aux region is elliminated then block interleave will change
 */

#define  TAPE_FORMAT           0            /* tape format version (not yet used!) */
#define  CORR_THRESHOLD(x)     ((x)*8/5)    /* maximum number of bits allowed to be bad (20%) */
#define  BKR_FILLER            0x33         /* filler for unused space */
#define  KEY_LENGTH            28           /* bytes (must be a multiple of 2) */
#define  BOR_LENGTH            4            /* seconds */
#define  EOR_LENGTH            1            /* seconds */

struct bkr_format
	{
	unsigned int  header_length;
	unsigned int  aux_length;
	unsigned int  footer_length;
	unsigned int  parity;
	};
                     /* head aux foot prty */
#define  BKR_FORMATS  {{ 32,  56, 28, 16 },       /* LOW  NTSC SP */   \
                       { 32,  56, 28, 20 },       /* LOW  NTSC EP */   \
                       { 32,  56, 28, 16 },       /* LOW  PAL  SP */   \
                       { 32,  56, 28, 20 },       /* LOW  PAL  EP */   \
                       { 80, 140, 70, 16 },       /* HIGH NTSC SP */   \
                       { 80, 140, 70, 20 },       /* HIGH NTSC EP */   \
                       { 80, 140, 70, 16 },       /* HIGH PAL  SP */   \
                       { 80, 140, 70, 20 }}       /* HIGH PAL  EP */

/*
 * Block Layer
 */

#define  BLOCK_TYPE(x)         ((x) & 0xc000)     /* type */
#define  KEY_BLOCK(x)          ((x) & 0x2000)     /* block contains sector key */
#define  TRUNCATE_BLOCK(x)     ((x) & 0x1000)     /* block is truncated */
#define  BLOCK_SEQ(x)          ((x) & 0x0fff)     /* sequence number */
#define  BOR_BLOCK             0x0000             /* is a BOR block */
#define  EOR_BLOCK             0x4000             /* is an EOR block */
#define  DATA_BLOCK            0x8000             /* is a data block */


/*
 * Parameter checks.
 */

#if DEFAULT_BUFFER_SIZE >= 65536
#error "DEFAULT_BUFFER_SIZE too high"
#endif

#if (BKR_VIDEOMODE(DEFAULT_MODE) != BKR_PAL) && (BKR_VIDEOMODE(DEFAULT_MODE) != BKR_NTSC)
#error "Bad video mode specifier in DEFAULT_MODE"
#endif
#if (BKR_DENSITY(DEFAULT_MODE) != BKR_HIGH) && (BKR_DENSITY(DEFAULT_MODE) != BKR_LOW)
#error "Bad density specifier in DEFAULT_MODE"
#endif
#if (BKR_FORMAT(DEFAULT_MODE) != BKR_FMT) && (BKR_FORMAT(DEFAULT_MODE) != BKR_RAW)
#error "Bad format specifier in DEFAULT_MODE"
#endif
#if (BKR_SPEED(DEFAULT_MODE) != BKR_SP) && (BKR_SPEED(DEFAULT_MODE) != BKR_EP)
#error "Bad tape speed specifier in DEFAULT_MODE"
#endif

#if (DEFAULT_TIMEOUT < BOR_LENGTH) || (DEFAULT_TIMEOUT > BKR_MAX_TIMEOUT)
#error "DEFAULT_TIMEOUT too long or too short"
#endif

#if (KEY_LENGTH & 1) != 0
#error "KEY_LENGTH must be a multiple of 2"
#endif

#if EOR_BLOCK == 0
#error "EOR_BLOCK must be non-zero"
#endif

#if (MIN_UPDATE_FREQ > HZ) || (MAX_UPDATE_FREQ > HZ) || (MIN_SYNC_FREQ > HZ)
#error "One of the *_FREQ parameters is too high"
#endif

#endif /* _BACKER_H */
