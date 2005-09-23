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
 *	device.direction = STOPPED;
 *	device.buffer = (allocate a buffer here)
 *	device.alloc_size = (insert allocated buffer size here)
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
 *	bkr_write_bor();                (only if writing)
 *
 *	sector.read();                  (repeat as needed if reading)
 *      sector.write();                 (repeat as needed if writing)
 *
 *	bkr_write_eor();                (only if writing)
 *
 * Flush if needed and stop the transfer.
 *
 *	bkr_device_flush();             (only if writing)
 *	bkr_device_stop_transfer();
 *
 * Final clean up.
 *
 *	free(sector.buffer);
 *	free(device.buffer);
 *
 * Note:  In order to support multiple formats, reading and writing is done
 * via function pointers in the sector structure.  The pointers are set
 * according to the mode passed to bkr_format_reset().
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
 *    |        --  +-------------+  --  <-- buffer (pointer)
 *    |            |     A       |   ^
 *    |            |     C       |   |
 *    |            |     T R     |   |
 *    |            |     I E     |   |
 *  video_size     |     V G     |   buffer_size
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
 * Zoomed in on the active area of a single sector, the meaning of a few more
 * variables is shown below.  offset always points to the next byte to be
 * read/written and if equal to end then the sector is empty/full respectively.
 * If the sector contains less than its maximum capacity of data, then the
 * truncate bit is set in the header and the last byte of the data area is used
 * to hold the lowest 8 bits of the count of actual bytes (excluding the
 * header) in the sector while the upper 4 bits of the count are stored in the
 * header itself.  end always points to the end of the data portion of the
 * sector and is adjusted appropriately when a truncated sector is retrieved.
 *
 * Logically, the data portion of the sector (including the header) is
 * divided into blocks although this is hidden from the interface.
 * Associated with each block is a group of parity symbols used for error
 * control.  The parity symbols for all the blocks are grouped together at
 * the top end of the sector's active area.
 *
 * The active area of the sector is not written linearly to tape but is
 * instead interleaved in order to distribute burst errors among the
 * individual blocks.  The final distance, in bytes, between adjacent bytes
 * of a given block when written to tape is given by the interleave
 * paramter.
 *
 *             --  +-------------+
 *             ^   |    Header   |
 *             |   +-------------+  <--- start (pointer)
 *             |   |             |
 *             |   |             |
 *             |   |             |
 *     data_size   |      D      |
 *             |   |      A      |
 *             |   |      T      |
 *             |   |      A      |
 *             |   |             |
 *             |   |             |
 *             |   |             |
 *             |   +- - - - - - -+
 *             v   |   capacity  |
 *             --  +-------------+  <--- end (pointer)
 *             ^   |             |
 *   parity_size   |    Parity   |
 *             v   |             |
 *             --  +-------------+
 */


#ifndef BACKER_FMT_H
#define BACKER_FMT_H

#include "rs.h"
#include "backer_device.h"

/*
 * Paramters
 */

#define  BKR_LEADER            0xe2     /* leader is filled with this */
#define  BKR_TRAILER           0x33     /* trailer is filled with this */
#define  BKR_FILLER            0x33     /* unused space is filled with this */
#define  KEY_LENGTH            28       /* bytes */
#define  BOR_LENGTH            4        /* seconds */
#define  EOR_LENGTH            1        /* seconds */


/*
 * Header structure
 *
 * NOTE:  assumes unsigned int is 32 bits.
 */

typedef union
	{
	struct
		{
		unsigned char key[KEY_LENGTH];  /* key sequence */
		unsigned int  number : 22 __attribute__ ((packed));      /* sector number */
		unsigned int  hi_used : 4 __attribute__ ((packed));      /* high 4 bits of usage */
		unsigned int  type : 5 __attribute__ ((packed));         /* sector type */
		unsigned int  truncate : 1 __attribute__ ((packed));     /* sector is truncated */
		} parts;
	struct
		{
		unsigned char key[KEY_LENGTH];  /* key sequence */
		unsigned int  state __attribute__ ((packed));            /* merged flags */
		} all;
	} sector_header_t;

#define KEY_SEQUENCE                                         \
	{ 0xb9, 0x57, 0xd1, 0x0b, 0xb5, 0xd3, 0x66, 0x07,    \
	  0x5e, 0x76, 0x99, 0x7d, 0x73, 0x6a, 0x09, 0x1e,    \
	  0x89, 0x55, 0x3f, 0x21, 0xca, 0xa6, 0x36, 0xb7,    \
	  0xdf, 0xf9, 0xaa, 0x17 }

#define  BOR_SECTOR   0                 /* sector is a BOR marker */
#define  EOR_SECTOR   1                 /* sector is an EOR marker */
#define  DATA_SECTOR  2                 /* sector contains data */

#define  SECTOR_HEADER_INITIALIZER  ((sector_header_t) {{ KEY_SEQUENCE, 0, 0, DATA_SECTOR, 0 }})


/*
 * Data exported by formating layer
 */

struct bkrerrors errors;                /* error counts */
struct bkrhealth health;                /* health indicators */

struct
	{
	unsigned char  *buffer;         /* uninterleaved data buffer */
	unsigned char  *offset;         /* location of next byte to be read/written */
	unsigned char  *start;          /* see diagram above */
	unsigned char  *end;            /* see diagram above */
	unsigned int  interleave;       /* block interleave */
	unsigned int  video_size;       /* see diagram above */
	unsigned int  buffer_size;      /* see diagram above */
	unsigned int  data_size;        /* see diagram above */
	unsigned int  parity_size;      /* see diagram above */
	unsigned int  leader;           /* see diagram above */
	unsigned int  trailer;          /* see diagram above */
	int  oddfield;                  /* current video field is odd */
	int  need_sequence_reset;       /* sector number needs to be reset */
	int  mode;                      /* current mode (see backer.h) */
	sector_header_t  header;        /* sector header copy */
	struct rs_format_t  rs_format;  /* Reed-Solomon format parameters */
	int  (*read)(f_flags_t, jiffies_t);     /* sector read function */
	int  (*write)(f_flags_t, jiffies_t);    /* sector write function */
	} sector;


/*
 * Functions exported by formating layer.  The read and write functions are
 * accessed through the pointers in the sector data structure.
 */

int           bkr_format_reset(int, direction_t);
int           bkr_write_bor(jiffies_t);
int           bkr_write_eor(jiffies_t);
unsigned int  space_in_buffer(void);
unsigned int  bytes_in_buffer(void);


/*
 * Some private functions meant only for export to the error analysis
 * program.
 */

#ifdef BACKER_FMT_PRIVATE

extern unsigned char  weight[];
int   bkr_find_sector(f_flags_t, jiffies_t);

#endif /* BACKER_FMT_PRIVATE */


#endif /* BACKER_FMT_H */
