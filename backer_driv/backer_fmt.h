/*
 * backer_fmt
 *
 * Linux 2.0.xx driver for Danmere's Backer 16/32 video tape backup cards.
 *
 * To access the formating layer, the symbols defined here are required.  The
 * access sequence should be as follows:
 *
 *      2.  bkr_format_reset()
 *      3.  bkr_write_bor()                 (only when writing)
 *      4.  block.read() or block.write()   (repeat as needed)
 *      5.  bkr_write_eor()                 (only when writing)
 *
 * Note:  In order to support multiple formats, reading and writing is done
 * via function pointers in the block info structure.  The pointers are set
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
 * sectors) of data starting on an odd field of video.  offset points to
 * the first byte of the block currently being read/written.  interleave
 * specifies the block interleave ratio (consecutive bytes from a single
 * block are spaced this far apart when written to tape).  header_loc
 * points to the location of the sector header within the data buffer.
 *
 *    --       --  +-------------+
 *    ^   leader   |   leader    |
 *    |        --  +-------------+  --
 *    |            |             |   ^
 *    |            |      D      |   |
 *    |            |             |   |
 *    |            |      A      |   |
 *    size         |             |   data_size
 *    |            |      T      |   |
 *    |            |             |   |
 *    |            |      A      |   |
 *    |            |             |   v
 *    |        --  +-------------+  --
 *    v  trailer   |   trailer   |
 *    --       --  +-------------+  <-- one extra line in NTSC & PAL modes
 *                 |   leader    |
 *                 +-------------+
 *                 |             |
 *                 |      D      |
 *                 |             |
 *                 |      A      |
 *                 |             |
 *                 |      T      |
 *                 |             |
 *                 |      A      |
 *                 |             |
 *                 +-------------+
 *                 |   trailer   |
 *                 +-------------+  <-- one extra line in PAL mode
 */


/*
 * The meaning of the variables involved in block formating is as
 * illustrated in the following diagram.  The optional capacity field
 * occupies the single byte at the end of a block whose data is truncated
 * to less than full capacity and specifies, for that block, the
 * zero-origin offset of the end pointer from the start pointer.  In blocks
 * containing the sector header, the header occupies the first
 * sizeof(sector_header_t) bytes of the data area.  In all cases, the
 * formating code will ensure that the start pointer points to the first
 * byte of file data in the block while the end pointer points to the byte
 * immediately after the last byte of file data in the block.  In
 * pass-through mode, the entire sector data buffer is used as a single
 * block with the start and end pointers pointing to its start and end and
 * no processing is applied to the data what so ever.
 *
 *             --  +-------------+  --
 *             ^   |             |   ^
 *             |   |    Parity   |   parity
 *             |   |             |   v
 *             |   +-------------+  --
 *             |   |    Header   |
 *             |   +-------------+  <--- start (pointer)
 *             |   |             |
 *             |   |             |
 *             |   |             |
 *          size   |      D      |
 *             |   |      A      |
 *             |   |      T      |
 *             |   |      A      |
 *             |   |             |
 *             |   |             |
 *             |   |             |
 *             |   +- - - - - - -+
 *             v   |   capacity  |
 *             --  +-------------+  <--- end (pointer)
 */


#ifndef BACKER_FMT_H
#define BACKER_FMT_H

#include "backer_device.h"
#include "rs.h"

/*
 * Paramters
 */

#define  BKR_LEADER            0x33     /* leader is filled with this */
#define  BKR_TRAILER           0x33     /* trailer is filled with this */
#define  BKR_FILLER            0x33     /* unused space is filled with this */
#define  KEY_LENGTH            28       /* bytes */
#define  BOR_LENGTH            4        /* seconds */
#define  EOR_LENGTH            1        /* seconds */

#define KEY_INITIALIZER                                      \
	{ 0xb9, 0x57, 0xd1, 0x0b, 0xb5, 0xd3, 0x66, 0x07,    \
	  0x5e, 0x76, 0x99, 0x7d, 0x73, 0x6a, 0x09, 0x1e,    \
	  0x89, 0x55, 0x3f, 0x21, 0xca, 0xa6, 0x36, 0xb7,    \
	  0xdf, 0xf9, 0xaa, 0x17 }

/*
 * Header structures
 */

typedef struct
	{
	unsigned char type : 3;         /* type */
	unsigned char pad : 3;
	unsigned char header : 1;       /* block contains sector header */
	unsigned char truncate : 1;     /* block is truncated */
	} block_header_t;

#define  BOR_BLOCK   0                  /* block is a BOR marker */
#define  EOR_BLOCK   1                  /* blcok is an EOR marker */
#define  DATA_BLOCK  2                  /* block contains data */

#define  BLOCK_HEADER_INITIALIZER  ((block_header_t) { DATA_BLOCK, 0, 0, 0 })

typedef struct
	{
	unsigned char key[KEY_LENGTH];                  /* sector key */
	unsigned int  number __attribute__ ((packed));  /* sector number */
	} sector_header_t;

#define  SECTOR_HEADER_INITIALIZER  ((sector_header_t) { KEY_INITIALIZER, 0 });


/*
 * Data exported by formating layer
 */

struct bkrerrors errors;                /* error counts */
struct bkrhealth health;                /* health indicators */

struct
	{
	unsigned char  *offset;         /* next byte to be read/written */
	unsigned char  *start;          /* see diagram above */
	unsigned char  *end;            /* see diagram above */
	block_header_t  header;         /* block header copy */
	unsigned int  size;             /* see diagram above */
	unsigned int  parity;           /* see diagram above */
	int  (*read)(f_flags_t, jiffies_t);        /* pointer to appropriate read function */
	int  (*write)(f_flags_t, jiffies_t);       /* pointer to appropriate write function */
	struct rs_format_t  rs_format;  /* Reed-Solomon format parameters */
	} block;

struct
	{
	unsigned char  *data;           /* uninterleaved data for current sector */
	unsigned char  *offset;         /* location of current block */
	unsigned int  interleave;       /* block interleave */
	unsigned int  size;             /* see diagram above */
	unsigned int  data_size;        /* see diagram above */
	unsigned int  leader;           /* see diagram above */
	unsigned int  trailer;          /* see diagram above */
	int  oddfield;                  /* current video field is odd */
	int  need_sequence_reset;       /* sector number needs to be reset */
	int  mode;                      /* as in bkrconfig.mode */
	sector_header_t  *header_loc;   /* sector header location */
	sector_header_t  header;        /* sector header copy */
	} sector;


/*
 * Functions exported by formating layer.  The block read and write
 * functions are accessed through the pointers in the block data structure.
 */

int           bkr_format_reset(direction_t, int);
int           bkr_write_bor(jiffies_t);
int           bkr_write_eor(jiffies_t);
unsigned int  space_in_buffer(void);
unsigned int  bytes_in_buffer(void);


#endif /* BACKER_FMT_H */
