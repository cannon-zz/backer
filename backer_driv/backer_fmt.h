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
 *	device.state = STOPPED;
 *	device.buffer = (allocate a buffer here)
 *	sector.buffer = NULL;
 *
 * Reset the two layers in order.
 *
 *	bkr_device_reset();
 *	bkr_format_reset();
 *
 * Start the transfer.
 *
 *      bkr_device_start_transfer();
 *
 * Read/write data.
 *
 *	sector.read();                  (repeat as needed if reading)
 *      sector.write();                 (repeat as needed if writing)
 *
 * End the recording (only if writing)
 *
 *	bkr_write_eor();
 *	bkr_device_flush();
 *
 * Stop the transfer.
 *
 *	bkr_device_stop_transfer();
 *
 * Final clean up.
 *
 *	free(sector.buffer);
 *	free(device.buffer);
 *
 * Note:  In order to support multiple formats, reading and writing is done
 * via function pointers in the sector structure.  The pointers are set
 * according to the mode passed to bkr_format_reset().  During formated
 * writes, the first calls to write are directed to the BOR mark generator
 * until after the BOR has been fully written after which calls are
 * directed to the normal sector writer.
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


/*
 * The meaning of some of the variables used in sector formating is as
 * follows.  The diagram shows the layout for a complete frame (two
 * sectors) starting on an odd field of video.
 *
 *    --       --  +-------------+
 *    ^   leader   |   leader    |
 *    |        --  +-------------+  --
 *    |            |     A       |   ^
 *    |            |     C       |   |
 *    |            |     T R     |   |
 *    |            |     I E     |   |
 *  video_size     |     V G     |   active_size
 *    |            |     E I     |   |
 *    |            |       O     |   |
 *    |            |       N     |   |
 *    |            |             |   v
 *    |        --  +-------------+  --
 *    v  trailer   |   trailer   |
 *    --       --  +-------------+  <-- one extra line in NTSC
 *                 |   leader    |
 *                 +-------------+
 *                 |     A       |
 *                 |     C       |
 *                 |     T R     |
 *                 |     I E     |
 *                 |     V G     |
 *                 |     E I     |
 *                 |       O     |
 *                 |       N     |
 *                 |             |
 *                 +-------------+
 *                 |   trailer   |
 *                 +-------------+
 *
 * The "extra line" is a consequence of the interleaving of the image and
 * is a 1/2 width line occuring in the odd video field of the card's NTSC
 * signal.  The formating code handles this by inserting a dummy line.  PAL
 * also has a 1/2 width line but in this case its presence makes both video
 * fields have the same number of lines so it's just absorbed into the
 * trailer.  (Note:  although they are 1/2 width lines, a full line of data
 * is output by the Backer hardware).
 *
 * Zoomed in on the active area of a single sector, the meaning of a few
 * more variables is shown below.  offset always points to the next byte to
 * be read/written and if equal to end then the sector is empty/full
 * respectively.
 *
 * If the sector contains less than its maximum capacity of data, then the
 * actual number of bytes in the sector is represented as a 12 bit number
 * whose high 8 bits are placed in the last available byte of the data area
 * and low 4 bits are stored in the header.  The number thusly represented
 * is not simply the count of bytes in the sector but the count of bytes
 * encoded in such a way that the low 4 bits are never all 0 which allows
 * the 0 value to be used to indicate that the sector is full.
 *
 * Logically, the data portion of the sector (including the header) is
 * divided into blocks.  Associated with each block is a group of parity
 * symbols used for error control.  The parity symbols for all the blocks
 * are grouped together at the top end of the sector's active area.
 *
 * The active area of the sector is not written linearly to tape but is
 * instead interleaved in order to distribute burst errors among separate
 * blocks.  The final distance, in bytes, between adjacent bytes of a given
 * block when written to tape is given by the interleave parameter.
 *
 *             --  +-------------+ -- <-- buffer (pointer)
 *             ^   |             |  ^
 *             |   |             |  |
 *             |   |             |  |
 *             |   |             |  |
 *             |   |      D      |  |
 *     data_size   |      A      |  |
 *             |   |      T      |  buffer_size
 *             |   |      A      |  |
 *             |   |             |  |
 *             |   |             |  | <--- end (pointer as appropriate)
 *             |   |             |  |
 *             |   +- hi 8 bits -+  |
 *             |   +-------------+  |
 *             v   |    Header   |  |
 *             --  +-------------+  |
 *             ^   |             |  |
 *   parity_size   |    Parity   |  |
 *             v   |             |  v
 *             --  +-------------+ --
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
#error Oh oh:  byte order problems!  figure it out yourself.
#endif
#endif

/*
 * Paramters
 */

#define  BKR_LEADER            0xe2     /* leader is filled with this */
#define  BKR_TRAILER           0x33     /* trailer is filled with this */
#define  BKR_FILLER            0x33     /* unused space is filled with this */
#define  BOR_LENGTH            4        /* second(s) */
#define  EOR_LENGTH            1        /* second(s) */


/*
 * Header structure
 *
 * T-200 at EP = 600 minutes = 2160000 NTSC sectors = 22 bit sector number
 */

typedef enum
	{
	BOR_SECTOR = 0,                 /* sector is a BOR marker */
	EOR_SECTOR,                     /* sector is an EOR marker */
	DATA_SECTOR                     /* sector contains data */
	} bkr_sector_type_t;

#if __BYTE_ORDER == __LITTLE_ENDIAN

typedef struct
	{
	u_int32_t  number : 22;         /* sector number */
	u_int32_t  low_used : 4;        /* low 4 bits of usage */
	u_int32_t  type : 6;            /* sector type */
	} bkr_sector_header_t;

#define  SECTOR_HEADER_INITIALIZER  ((bkr_sector_header_t) { 0, 0, BOR_SECTOR })

#else /* __BYTE_ORDER */

typedef struct
	{
	u_int32_t  type : 6;           /* sector type */
	u_int32_t  low_used : 4;       /* low 4 bits of usage */
	u_int32_t  number : 22;        /* sector number */
	} bkr_sector_header_t;

#define  SECTOR_HEADER_INITIALIZER  ((bkr_sector_header_t) { BOR_SECTOR, 0, 0 })

#endif /* __BYTE_ORDER */

typedef enum
	{
	NRZ = 0,                        /* Non-return to zero */
	GCR                             /* Group code record */
	} modulation_t;


/*
 * Data exported by formating layer
 */

struct bkrerrors errors;                /* error counts */
struct bkrhealth health;                /* health indicators */

struct
	{
	unsigned char  *buffer;         /* uninterleaved data buffer */
	unsigned char  *offset;         /* location of next byte to be read/written */
	unsigned char  *end;            /* see diagram above */
	unsigned int  interleave;       /* block interleave */
	unsigned int  key_interval;     /* key byte spacing */
	unsigned int  key_length;       /* number of key bytes */
	modulation_t  modulation;       /* modulation type */
	unsigned int  video_size;       /* see diagram above */
	unsigned int  active_size;      /* see diagram above */
	unsigned int  buffer_size;      /* see diagram above */
	unsigned int  modulation_pad;   /* extra space for modulation overhead */
	unsigned int  data_size;        /* see diagram above */
	unsigned int  parity_size;      /* see diagram above */
	unsigned int  leader;           /* see diagram above */
	unsigned int  trailer;          /* see diagram above */
	int  oddfield;                  /* current video field is odd */
	int  need_sequence_reset;       /* sector number needs to be reset */
	int  found_data;                /* have found first valid data sector */
	int  op_count;                  /* counter for misc operations */
	int  mode;                      /* current mode (see backer.h) */
	bkr_sector_header_t  header;    /* sector header copy */
	struct rs_format_t  rs_format;  /* Reed-Solomon format parameters */
	int  (*read)(void);             /* sector read function */
	int  (*write)(void);            /* sector write function */
	} sector;


/*
 * Functions exported by formating layer.  The read and write functions are
 * accessed through the pointers in the sector data structure.
 */

int           bkr_format_reset(int, bkr_state_t);
int           bkr_sector_write_eor(void);
unsigned int  space_in_buffer(void);
unsigned int  bytes_in_buffer(void);


#endif /* BACKER_FMT_H */
