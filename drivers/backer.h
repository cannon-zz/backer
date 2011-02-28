/*
 * Header file for Backer 16/32 device driver.
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
 */


/*
 * The meaning of some of the variables used in sector formating is as
 * follows.  The diagram shows the layout for a complete frame (two
 * sectors) starting on an odd field of video.
 *
 *           --  +-------------+  --
 *           ^   |   leader    |   leader
 *           |   +-------------+  --
 *           |   |     A       |   ^
 *           |   |     C       |   |
 *           |   |     T R     |   |
 *           |   |     I E     |   |
 *  field_size   |     V G     |   active_size
 *           |   |     E I     |   |
 *           |   |       O     |   |
 *           |   |       N     |   |
 *           |   |             |   v
 *           |   +-------------+  --
 *           v   |   trailer   |   trailer
 *           --  +-------------+  --
 *               |  interlace  |   interlace
 *               +-------------+  --
 *               |   leader    |
 *               +-------------+
 *               |     A       |
 *               |     C       |
 *               |     T R     |
 *               |     I E     |
 *               |     V G     |
 *               |     E I     |
 *               |       O     |
 *               |       N     |
 *               |             |
 *               +-------------+
 *               |   trailer   |
 *               +-------------+
 *
 * Television images are interlaced --- the lines of every other video
 * field are drawn on the screen inbetween the lines of the previous video
 * field thus increasing the resolution of the image through time-domain
 * multiplexing.  The interlacing is accomplished by shifting the phase of
 * "even" video fields with respect to "odd" video fields by 1/2 a line
 * period.  In accomplishing this, the Backer hardware consumes one full
 * additional line of data during odd video fields when generating an NTSC
 * signal but no additional data for PAL signals.  The exact number of
 * additional bytes to be inserted at the end of each odd video field is
 * stored in the interlace parameter of the format.  In order to accomodate
 * any possible hardware behaviour, the interlace parameter can be
 * positive, 0 or even negative and need not be a multiple of the line
 * width.
 *
 * The helical scanning technique employed by VCR video heads introduces
 * noise at the top and bottom of each video field.  The role of the leader
 * and trailer is to pad out these unusable portions of the video fields.
 * The active region represents that portion of the field which can be used
 * to reliably store data.  Since Backers do not recover a fixed number of
 * bytes from each video field, it is necessary to provide a
 * synchronization mark within the byte stream that can be searched for on
 * playback to identify the locations of video fields.  In the case of this
 * software, this mark consists of several bytes distributed uniformly
 * throughout the active region.  The particular sequence of bytes used is
 * called the "key sequence" which consists of key_length bytes placed
 * every key_interval bytes within the active region.
 *
 * The remainder of the active region is available for the recording of
 * data and its contents are processed in the stream buffer.  The stream
 * buffer is also the staging area for data being copied to/from user
 * space.  For the processing of this buffer, the meaning of a few more
 * variables is shown below.  pos always points to the next byte to be
 * read/written and if equal to end then the sector is empty/full
 * respectively.
 *
 * The buffer contains three types of information:  a header containing
 * information about the sector (its sequence number, etc.), file data, and
 * finally parity bytes used for error correction.  The file data bytes and
 * header together form the "data" area with the header appearing at its
 * top end while the parity bytes form the "parity" area.
 *
 * Data is written sequentially into the data area upto the sector's
 * maximum capacity at which point the header is inserted and then the
 * parity bytes are computed.  The error control code employed is a
 * Reed-Solomon code whose symbols are interleaved so as to distribute
 * burst errors among separate R.S. blocks.  The number of R.S. blocks to
 * be interleaved together is set by the interleave parameter.
 *
 * The header normally occupies the top 4 bytes of the data area.  If the
 * sector contains less than its maximum capacity of data (eg. it is the
 * last sector of the recording and is not full), then the header is
 * expanded to occupy the top 5 bytes of the data area.  The additional
 * byte is used to store the high 8 bits of the actual number of file data
 * bytes in the sector with the low 4 bits being stored in a portion of the
 * normal header set aside for that purpose.  The count of bytes is mapped
 * into the resulting 12 bit number in such a way that the low four bits
 * are never all 0.  This allows the 0 value to be used to indicate a
 * normal, non-truncated, sector.
 *
 *             --  +-------------+
 *             ^   |             |
 *             |   |             |
 *             |   |             |
 *             |   |             |
 *             |   |      D      |
 *     data_size   |      A      |
 *             |   |      T      |
 *             |   |      A      |
 *             |   |             |
 *             |   |             |
 *             |   |             |
 *             |   +- hi 8 bits -+
 *             |   +-------------+
 *             v   |    Header   |
 *             --  +-------------+
 *             ^   |             |
 *   parity_size   |    Parity   |
 *             v   |             |
 *             --  +-------------+
 */


#ifndef  _BACKER_H
#define  _BACKER_H


/*
 * Hardware stuff
 */


#define  BKR_BIT_HIGH_DENSITY   0x01
#define  BKR_BIT_DMA_REQUEST    0x02
#define  BKR_BIT_DATA           0x04
#define  BKR_BIT_SYNC           0x08
#define  BKR_BIT_FRAME_BUSY     0x10
#define  BKR_BIT_TRANSMIT       0x20
#define  BKR_BIT_RECEIVE        0x40
#define  BKR_BIT_NTSC_VIDEO     0x80


#define  BKR_MIN_FRAME_FREQ     20      /* min frame rate in Hz */
#define  BKR_LINE_PERIOD        64      /* microseconds (NTSC and PAL) */
#define  BKR_FIRST_LINE_NTSC    10      /* first line of data in video field */
#define  BKR_FIRST_LINE_PAL     6       /* ditto */


/*
 * Mode masks and flags.
 *
 * Used widely throughout the code to describe the operating mode.
 * Returned by MTIOCGET in mtget.mt_dsreg.  Also appears in the status file
 * in the /proc file system.
 */


#define  BKR_VIDEOMODE_MASK    0x0003
#define  BKR_DENSITY_MASK      0x000c
#define  BKR_CODEC_MASK        0x0030
#define  BKR_MODE_MASK         (BKR_VIDEOMODE_MASK | BKR_DENSITY_MASK | BKR_CODEC_MASK)
#define  BKR_VIDEOMODE(x)      ((x) & BKR_VIDEOMODE_MASK)
#define  BKR_DENSITY(x)        ((x) & BKR_DENSITY_MASK)
#define  BKR_CODEC(x)          ((x) & BKR_CODEC_MASK)


enum bkr_videomode {
	BKR_NTSC = 0x0001,     /* NTSC video mode */
	BKR_PAL = 0x0002       /* PAL video mode */
};


enum bkr_bitdensity {
	BKR_LOW = 0x0004,      /* Low density */
	BKR_HIGH = 0x0008      /* High density */
};


enum bkr_sectorformat {
	BKR_SP = 0x0010,       /* read/write SP/LP format */
	BKR_EP = 0x0020        /* read/write EP format */
};


/*
 * _sysctl() interface
 */


enum {
	DEV_BACKER=31	/* FIXME:  get a number (from LANANA?) */
};


enum {
	BACKER_STATUS=1,
	BACKER_FORMAT=2
};


#endif /* _BACKER_H */
