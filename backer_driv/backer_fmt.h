/*
 * backer_fmt
 *
 * Linux 2.0.xx driver for Danmere's Backer 16/32 video tape backup cards.
 *
 * To access the formating layer, the symbols defined here are required.  The
 * access sequence should be as follows:
 *
 *      1.  bkr_set_parms()                 (only needed on format change)
 *      2.  bkr_format_reset()
 *      3.  bkr_write_bor()                 (only needed when writing)
 *      4.  block.read() or block.write()
 *      5.  bkr_write_eor()                 (only needed when writing)
 *
 * Note that reading and writing is done via the function pointers in the
 * block data structure which will point to the raw or formated I/O functions
 * as determined by the mode passed to bkr_set_parms().
 *
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
 *
 */


/*
 * The meaning of the variables used in sector formating is as follows.
 * The diagram shows the layout for a complete frame (two sectors) of data
 * starting on an odd field of video.  The parameter block_offset points
 * to the byte in the line of video immediately following the end of the
 * next block to be read/written.  The byte order on the diagram is
 * top-to-bottom.
 *
 *             --  +-------------+  --
 *             ^   |    header   |   ^
 *    aux_offset   +-------------+   |
 *             |   |             |   |
 *             |   |      D      |   |
 *             |   |             |   |
 *             v   |      A      |   |
 *             --  +-------------+   |
 *    aux_length   |     aux     |   |
 *             --  +-------------+   footer_offset
 *                 |      T      |   |
 *                 |             |   |
 *                 |      A      |   |
 *                 |             |   v
 *                 +-------------+  --
 *                 |    footer   |   footer_length (+1 line in NTSC mode)
 *                 +-------------+  --
 *                 |    header   |   header_length
 *                 +-------------+  --
 *                 |             |
 *                 |      D      |
 *                 |             |
 *                 |      A      |
 *                 +-------------+
 *                 |     aux     |
 *                 +-------------+
 *                 |      T      |
 *                 |             |
 *                 |      A      |
 *                 |             |
 *                 +-------------+  --
 *                 |    footer   |   footer_length
 *                 +-------------+  --
 */


/*
 * The meaning of the variables involved in block formating is as illustrated
 * in the following diagram.  The optional capacity field occupies the single
 * byte immediately preceding the header word in a block whose data is
 * truncated to less than full capacity.  In blocks used as key blocks, the
 * sector key occupies the first KEY_LENGTH symbols of the data area.  The
 * byte order for the diagram is top-to-bottom.
 *
 *             --  +-------------+  --
 *             ^   |             |   ^
 *             |   |    Parity   |   ecc_length
 *             |   |             |   v
 *             |   +-------------+  --  <--- header (pointer)
 *             |   |    Header   |
 *             |   +-------------+      <--- start (pointer)
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
 *             --  +-------------+      <--- end (pointer)
 */


#ifndef BACKER_FMT_H
#define BACKER_FMT_H

#include "rs.h"

#define KEY_INITIALIZER                                      \
	{ 0xb9, 0x57, 0xd1, 0x0b, 0xb5, 0xd3, 0x66, 0x07,    \
	  0x5e, 0x76, 0x99, 0x7d, 0x73, 0x6a, 0x09, 0x1e,    \
	  0x89, 0x55, 0x3f, 0x21, 0xca, 0xa6, 0x36, 0xb7,    \
	  0xdf, 0xf9, 0xaa, 0x17, 0x66, 0x46, 0x8a, 0x73,    \
	  0xae, 0x98, 0xe8, 0xa3, 0xd7, 0xb7, 0x49, 0x1a,    \
	  0xc0, 0x7c, 0x61, 0x06, 0xce, 0xa3, 0xa3, 0x33,    \
	  0x21, 0x86, 0x79, 0x3d, 0x22, 0x59, 0xc5, 0xc0,    \
	  0xa1, 0x7e, 0xbe, 0x71, 0x70, 0xce, 0x9e, 0x7d,    \
	  0xdf, 0xef, 0xd3, 0xde, 0xbc, 0xa8, 0x1e, 0x4c,    \
	  0xe7, 0xd2, 0xce, 0xce, 0x36, 0x3d, 0x87, 0xc7 };

#define WEIGHT_INITIALIZER                                   \
	{ 0x00, 0x01, 0x01, 0x02, 0x01, 0x02, 0x02, 0x03,    \
	  0x01, 0x02, 0x02, 0x03, 0x02, 0x03, 0x03, 0x08,    \
	  0x01, 0x02, 0x02, 0x03, 0x02, 0x03, 0x03, 0x08,    \
	  0x02, 0x03, 0x03, 0x08, 0x03, 0x08, 0x08, 0x08,    \
	  0x01, 0x02, 0x02, 0x03, 0x02, 0x03, 0x03, 0x08,    \
	  0x02, 0x03, 0x03, 0x08, 0x03, 0x08, 0x08, 0x08,    \
	  0x02, 0x03, 0x03, 0x08, 0x03, 0x08, 0x08, 0x08,    \
	  0x03, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,    \
	  0x01, 0x02, 0x02, 0x03, 0x02, 0x03, 0x03, 0x08,    \
	  0x02, 0x03, 0x03, 0x08, 0x03, 0x08, 0x08, 0x08,    \
	  0x02, 0x03, 0x03, 0x08, 0x03, 0x08, 0x08, 0x08,    \
	  0x03, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,    \
	  0x02, 0x03, 0x03, 0x08, 0x03, 0x08, 0x08, 0x08,    \
	  0x03, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,    \
	  0x03, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,    \
	  0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,    \
	  0x01, 0x02, 0x02, 0x03, 0x02, 0x03, 0x03, 0x08,    \
	  0x02, 0x03, 0x03, 0x08, 0x03, 0x08, 0x08, 0x08,    \
	  0x02, 0x03, 0x03, 0x08, 0x03, 0x08, 0x08, 0x08,    \
	  0x03, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,    \
	  0x02, 0x03, 0x03, 0x08, 0x03, 0x08, 0x08, 0x08,    \
	  0x03, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,    \
	  0x03, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,    \
	  0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,    \
	  0x02, 0x03, 0x03, 0x08, 0x03, 0x08, 0x08, 0x08,    \
	  0x03, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,    \
	  0x03, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,    \
	  0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,    \
	  0x03, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,    \
	  0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,    \
	  0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,    \
	  0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08 };

/*
 * Functions exported by formating layer.  The block read and write
 * functions are accessed through the pointers in the block data structure.
 */

int           bkr_set_parms(unsigned int, unsigned int);
void          bkr_format_reset(void);
void          bkr_write_bor(unsigned long);
void          bkr_write_eor(unsigned long);
unsigned int  space_in_buffer(void);
unsigned int  bytes_in_buffer(void);


/*
 * Data exported by formating layer
 */

typedef  __u16  header_t;

unsigned int  worst_match;                      /* these are for debugging only */
unsigned int  best_nonmatch;
unsigned int  least_skipped;
unsigned int  most_skipped;

struct
	{
	unsigned int  symbol;                   /* most symbols errors in any one block */
	unsigned int  block;                    /* uncorrectable blocks since start of i/o */
	unsigned int  sector;                   /* framing errors since start of i/o */
	unsigned int  overrun;                  /* buffer overruns since start of read */
	} errors;

struct
	{
	unsigned char  *buffer;                 /* holds the current block */
	header_t  *header;                      /* see diagram above */
	unsigned char  *offset;                 /* pointer to next byte to be read/written */
	unsigned char  *start;                  /* see diagram above */
	unsigned char  *end;                    /* see diagram above */
	unsigned int  size;                     /* block size in bytes */
	unsigned int  ecc_length;               /* see diagram above */
	unsigned int  sequence;                 /* sequence number */
	int   (*read)(unsigned long);           /* pointer to appropriate read function */
	void  (*write)(unsigned long);          /* pointer to appropriate write function */
	struct rs_format_t  rs_format;          /* Reed-Solomon format parameters */
	} block;

struct
	{
	unsigned int  size;                     /* sector size in bytes */
	unsigned int  header_length;            /* see diagram above */
	unsigned int  footer_offset;            /* see diagram above */
	unsigned int  footer_length;            /* see diagram above */
	unsigned int  aux_offset;               /* see diagram above */
	unsigned int  aux_length;               /* see diagram above */
	unsigned int  block_offset;             /* location of current block */
	unsigned char *aux;                     /* data for the auxiliary region */
	} sector;

#endif /* BACKER_FMT_H */
