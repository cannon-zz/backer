/*
 * backer_fmt
 *
 * Driver for Danmere's Backer 16/32 video tape backup cards.
 *
 *                             Formating Layer
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


#ifdef __KERNEL__

#include <linux/errno.h>
#include <linux/malloc.h>
#include <linux/string.h>
#define  malloc(x)  kmalloc((x), GFP_KERNEL)
#define  free(x)    kfree(x)

#else

#include <errno.h>
#include <malloc.h>
#include <string.h>

#endif /* __KERNEL__ */

#include "rs.h"
#include "backer.h"
#include "backer_device.h"
#include "backer_fmt.h"

#undef GCR_STUFF


/*
 * Parameters
 */

#define  CORRELATION_THRESHOLD  204             /* 255 == 100% */


/*
 * Global Data
 */

/* Read the docs for important info before adjusting these! */
/* FIXME:  make sure EP modes have active_size == multiple of 9 */
static struct
        {
        unsigned int  leader;
        unsigned int  trailer;
	unsigned int  interleave;
        unsigned int  parity;
        } format[] =                            /* format information */
	{ {  32,  28,  8,  8 },                 /* LOW  NTSC SP  */
	  {  40,  32, 20,  8 },                 /* LOW  NTSC EP  */
	  {  40,  36,  8,  8 },                 /* LOW  PAL  SP  */
	  {  48,  48,  8,  8 },                 /* LOW  PAL  EP  */
	  {  80,  70, 20,  8 },                 /* HIGH NTSC SP  */
	  { 100,  70, 59,  6 },                 /* HIGH NTSC EP  */
	  { 100,  90, 20,  8 },                 /* HIGH PAL  SP  */
	  { 120, 120, 20,  8 },                 /* HIGH PAL  EP  */
	  {   0,   0,  1,  0 } };               /* RAW           */

unsigned char  weight[] =                       /* correlation weights */
	{ 0xff, 0xf7, 0xf7, 0xdb, 0xf7, 0xdb, 0xdb, 0xa3,
	  0xf7, 0xdb, 0xdb, 0xa3, 0xdb, 0xa3, 0xa3, 0x5d,
	  0xf7, 0xdb, 0xdb, 0xa3, 0xdb, 0xa3, 0xa3, 0x5d,
	  0xdb, 0xa3, 0xa3, 0x5d, 0xa3, 0x5d, 0x5d, 0x25,
	  0xf7, 0xdb, 0xdb, 0xa3, 0xdb, 0xa3, 0xa3, 0x5d,
	  0xdb, 0xa3, 0xa3, 0x5d, 0xa3, 0x5d, 0x5d, 0x25,
	  0xdb, 0xa3, 0xa3, 0x5d, 0xa3, 0x5d, 0x5d, 0x25,
	  0xa3, 0x5d, 0x5d, 0x25, 0x5d, 0x25, 0x25, 0x09,
	  0xf7, 0xdb, 0xdb, 0xa3, 0xdb, 0xa3, 0xa3, 0x5d,
	  0xdb, 0xa3, 0xa3, 0x5d, 0xa3, 0x5d, 0x5d, 0x25,
	  0xdb, 0xa3, 0xa3, 0x5d, 0xa3, 0x5d, 0x5d, 0x25,
	  0xa3, 0x5d, 0x5d, 0x25, 0x5d, 0x25, 0x25, 0x09,
	  0xdb, 0xa3, 0xa3, 0x5d, 0xa3, 0x5d, 0x5d, 0x25,
	  0xa3, 0x5d, 0x5d, 0x25, 0x5d, 0x25, 0x25, 0x09,
	  0xa3, 0x5d, 0x5d, 0x25, 0x5d, 0x25, 0x25, 0x09,
	  0x5d, 0x25, 0x25, 0x09, 0x25, 0x09, 0x09, 0x01,
	  0xf7, 0xdb, 0xdb, 0xa3, 0xdb, 0xa3, 0xa3, 0x5d,
	  0xdb, 0xa3, 0xa3, 0x5d, 0xa3, 0x5d, 0x5d, 0x25,
	  0xdb, 0xa3, 0xa3, 0x5d, 0xa3, 0x5d, 0x5d, 0x25,
	  0xa3, 0x5d, 0x5d, 0x25, 0x5d, 0x25, 0x25, 0x09,
	  0xdb, 0xa3, 0xa3, 0x5d, 0xa3, 0x5d, 0x5d, 0x25,
	  0xa3, 0x5d, 0x5d, 0x25, 0x5d, 0x25, 0x25, 0x09,
	  0xa3, 0x5d, 0x5d, 0x25, 0x5d, 0x25, 0x25, 0x09,
	  0x5d, 0x25, 0x25, 0x09, 0x25, 0x09, 0x09, 0x01,
	  0xdb, 0xa3, 0xa3, 0x5d, 0xa3, 0x5d, 0x5d, 0x25,
	  0xa3, 0x5d, 0x5d, 0x25, 0x5d, 0x25, 0x25, 0x09,
	  0xa3, 0x5d, 0x5d, 0x25, 0x5d, 0x25, 0x25, 0x09,
	  0x5d, 0x25, 0x25, 0x09, 0x25, 0x09, 0x09, 0x01,
	  0xa3, 0x5d, 0x5d, 0x25, 0x5d, 0x25, 0x25, 0x09,
	  0x5d, 0x25, 0x25, 0x09, 0x25, 0x09, 0x09, 0x01,
	  0x5d, 0x25, 0x25, 0x09, 0x25, 0x09, 0x09, 0x01,
	  0x25, 0x09, 0x09, 0x01, 0x09, 0x01, 0x01, 0x00 };

#ifdef GCR_STUFF
static unsigned short gcr_encode[] =            /* 8/9 (0,4/4) GCR modulation */
	{ 124, 125, 120, 121, 123, 113, 115, 114,
	  119, 118, 116,  97,  99,  98, 103, 102,
	  100, 110, 108, 109, 104, 105, 107,  65,
	   67,  66,  71,  70,  68,  78,  76,  77,
	   72,  73,  75,  94,  92,  93,  88,  89,
	   91,  81,  83,  82, 248, 249, 251, 241,
	  243, 242, 247, 246, 244, 225, 227, 226,
	  231, 230, 228, 238, 236, 237, 232, 233,
	  235, 193, 195, 194, 199, 198, 196, 206,
	  204, 205, 200, 201, 203, 222, 220, 221,
	  216, 217, 219, 209, 211, 210, 215, 214,
	  131, 130, 135, 134, 132, 142, 140, 141,
	  136, 137, 139, 158, 156, 157, 152, 153,
	  155, 145, 147, 146, 151, 150, 148, 190,
	  188, 189, 184, 185, 187, 177, 179, 178,
	  183, 182, 180, 161, 163, 162, 167, 166,
	  164, 497, 499, 498, 503, 502, 500, 481,
	  483, 482, 487, 486, 484, 494, 492, 493,
	  488, 489, 491, 449, 451, 450, 455, 454,
	  452, 462, 460, 461, 456, 457, 459, 478,
	  476, 477, 472, 473, 475, 465, 467, 466,
	  471, 470, 387, 386, 391, 390, 388, 398,
	  396, 397, 392, 393, 395, 414, 412, 413,
	  408, 409, 411, 401, 403, 402, 407, 406,
	  404, 446, 444, 445, 440, 441, 443, 433,
	  435, 434, 439, 438, 436, 417, 419, 418,
	  423, 422, 420, 430, 428, 429, 263, 262,
	  260, 270, 268, 269, 264, 265, 267, 286,
	  284, 285, 280, 281, 283, 273, 275, 274,
	  279, 278, 276, 318, 316, 317, 312, 313,
	  315, 305, 307, 306, 311, 310, 308, 289,
	  291, 290, 295, 294, 292, 302, 300, 301 };
#endif /* GCR_STUFF */

static struct
	{
	unsigned int  size;
	unsigned int  data;
	unsigned int  parity;
	} block;                                /* R-S block info */


/*
 * Macros
 */

static inline int mode_to_format(int mode)
{
	if(BKR_FORMAT(mode) == BKR_RAW)
		return(8);

	return(((BKR_DENSITY(mode) == BKR_HIGH)  << 2) |
	       ((BKR_VIDEOMODE(mode) == BKR_PAL) << 1) |
	       (BKR_FORMAT(mode) == BKR_EP));
}

static inline int sectors_per_second(void)
{
	return(BKR_VIDEOMODE(sector.mode) == BKR_PAL ? 50 : 60);
}

#define  HEADER_PTR  ((sector_header_t *) sector.buffer)
#define  GCR_MASK    ((unsigned short) 0x01ff)
#define  ROL(x, n)   asm("rolw %2, %0" : "=r" (x) : "0" (x), "c" (n))


/*
 * Function prototypes.
 */

static void  bkr_sector_randomize(__u32 *, int, __u32);
static int   bkr_sector_write_bor(void);
static int   bkr_sector_read_data(void);
static int   bkr_sector_write_data(void);
static int   bkr_sector_read_raw(void);
static int   bkr_sector_write_raw(void);
static int   bkr_get_sector(void);
static int   bkr_put_sector(void);


/*
 * ================================================================================================
 *
 *                                      MISCELLANEOUS
 *
 * ================================================================================================
 */

/*
 * bkr_format_reset()
 *
 * Reset the formating layer (assumes the mode is valid!).  The return code
 * indicates success or failure.  On failure, sector.mode is left = 0 which
 * can be used to check for failures after the fact.
 */

int bkr_format_reset(int mode, direction_t direction)
{
	int  fmt;

	sector.mode = 0;

	if(BKR_FORMAT(mode) == BKR_RAW)
		sector.video_size = device.frame_size;
	else if(BKR_VIDEOMODE(mode) == BKR_NTSC)
		sector.video_size = device.bytes_per_line * LINES_PER_FIELD_NTSC;
	else
		sector.video_size = device.bytes_per_line * LINES_PER_FIELD_PAL;

	fmt = mode_to_format(mode);
	sector.leader     = format[fmt].leader;
	sector.trailer    = format[fmt].trailer;
	sector.interleave = format[fmt].interleave;
	block.parity      = format[fmt].parity;

	sector.active_size = sector.video_size - sector.leader - sector.trailer;
#ifdef GCR_STUFF
	sector.modulation_padding = FIXME;
	block.size = (sector.active_size - sector.modulation_padding) / sector.interleave;
#else
	block.size = sector.active_size / sector.interleave;
#endif
	block.data = block.size - block.parity;
	sector.data_size = block.data * sector.interleave;
	sector.parity_size = block.parity * sector.interleave;

	sector.header = SECTOR_HEADER_INITIALIZER;
	sector.oddfield = 1;
	sector.need_sequence_reset = 1;
	sector.found_data = 0;
	sector.op_count = 0;

	free(sector.buffer);
	sector.buffer = (unsigned char *) malloc(sector.active_size);
	if(sector.buffer == NULL)
		return(-ENOMEM);
	memset(sector.buffer, BKR_FILLER, sector.active_size);

	if(BKR_FORMAT(mode) == BKR_RAW)
		{
		sector.read = bkr_sector_read_raw;
		sector.write = bkr_sector_write_raw;
		sector.start = sector.buffer;
		}
	else
		{
		sector.read = bkr_sector_read_data;
		sector.write = bkr_sector_write_bor;
		sector.start = sector.buffer + sizeof(sector_header_t);
		if(direction == WRITING)
			sector.op_count = sectors_per_second() * BOR_LENGTH;
		}

	sector.end = sector.buffer + sector.data_size;

	if((BKR_FORMAT(mode) == BKR_RAW) && (direction == WRITING))
		sector.offset = sector.start;
	else
		sector.offset = sector.end;

	reed_solomon_init(block.size, block.data, &sector.rs_format);

	errors = ERRORS_INITIALIZER;
	health = HEALTH_INITIALIZER;

	sector.mode = mode;

	return(0);
}


/*
 * space_in_buffer(), bytes_in_buffer()
 *
 * Return the space and bytes available in the device I/O buffer.  Note
 * that space_in_buffer() + bytes_in_buffer() == device.size - 1.
 */

unsigned int space_in_buffer(void)
{
	if(device.tail > device.head)
		return(device.tail - device.head - 1);
	return(device.tail+device.size - device.head - 1);
}

unsigned int bytes_in_buffer(void)
{
	if(device.tail > device.head)
		return(device.head+device.size - device.tail);
	return(device.head - device.tail);
}


/*
 * ================================================================================================
 *
 *                                         FORMATED I/O
 *
 * ================================================================================================
 */

/*
 * bkr_sector_write_bor()
 *
 * Generate Begining-Of-Record mark.  Return values are the same as for
 * bkr_sector_write_data().
 */

static int bkr_sector_write_bor(void)
{
	int  result = 0;

	memset(sector.start, BKR_FILLER, sector.end - sector.start);
	for(; sector.op_count; sector.op_count--)
		{
		result = bkr_sector_write_data();
		sector.offset = sector.end;
		if(result < 0)
			return(result);
		}

	sector.offset = sector.start;
	sector.header.parts.type = DATA_SECTOR;
	sector.write = bkr_sector_write_data;

	return(result);
}


/*
 * bkr_sector_write_eor()
 *
 * Generate End-Of-Record mark.  Return values are the same as for
 * bkr_sector_write_data().
 */

int bkr_sector_write_eor(void)
{
	int  result = 0;

	if(sector.op_count == 0)
		{
		if(sector.offset != sector.start)
			{
			result = bkr_sector_write_data();
			if(result < 0)
				return(result);
			}

		sector.header.parts.type = EOR_SECTOR;
		sector.op_count = sectors_per_second() * EOR_LENGTH;
		}

	memset(sector.start, BKR_FILLER, sector.end - sector.start);
	for(; sector.op_count; sector.op_count--)
		{
		sector.offset = sector.end;
		result = bkr_sector_write_data();
		if(result < 0)
			break;
		}

	return(result);
}


/*
 * bkr_sector_randomize()
 *
 * (De-)Randomize a buffer using the supplied seed.  The count is rounded
 * up to the nearest multiple of 4 so make sure the buffer is sized
 * accordingly!
 *
 * For the random number generator used see:  Knuth, D. E. 1981,
 * Semi-Numerical Algorithms, 2nd ed., vol 2 of The Art of Computer
 * Programing.
 */

static void bkr_sector_randomize(__u32 *location, int count, __u32 seed)
{
	unsigned int  index;
	__u32  history[4];

	for(index = 0; index < 4; index++)
		{
		seed = 1664525 * seed + 1013904223;
		history[index] = seed;
		}

	for(count = (count+3) >> 2; count; count--)
		{
		seed = 1664525 * seed + 1013904223;
		index = seed >> 30;
		location[count] ^= history[index];
		history[index] = seed;
		}
}


/*
 * bkr_sector_read_data()
 *
 * Retrieves the next sector from the DMA buffer.  The algorithm is to loop
 * until we either find an acceptable sector (it's a type we understand and
 * is in the correct order) or we encounter an error or EOR.  If we find a
 * sector we like, we set it up for a read out and return 1.  Otherwise we
 * return < 0 on error or 0 on EOR.  On error, a retry can be attempted by
 * simply re-calling this function.
 *
 * We tolerate junk sectors for the first little while in order to skip
 * over the noise found at the beginning of recordings.  This is done by
 * silently skipping over all errors (except -EWOULDBLOCKS) until we find
 * the first DATA sector or until we time out.
 */

static int bkr_sector_read_data(void)
{
	int  result;
	unsigned int  i;
	char  underflow_detect = 1;
	unsigned char  *data, *parity;

	sector.offset = sector.end = sector.start;

	while(1)
		{
		/*
		 * Get the next sector.
		 */

		result = bkr_get_sector();
		if(result < 0)
			return(result);

		/*
		 * Do error correction.
		 */

		data = sector.buffer;
		parity = sector.buffer + sector.data_size;
		for(i = 0; i < sector.interleave; data += block.data, parity += block.parity, i++)
			{
			result = reed_solomon_decode(parity, data, NULL, 0, &sector.rs_format);
			if(result < 0)
				{
				sector.need_sequence_reset = 1;
				if(sector.found_data)
					{
					errors.block++;
					return(-ENODATA);
					}
				HEADER_PTR->parts.type = BOR_SECTOR;
				break;
				}
			health.total_errors += result;
			if((unsigned int) result > errors.symbol)
				errors.symbol = result;
			if((unsigned int) result > errors.recent_symbol)
				errors.recent_symbol = result;
			}

		/*
		 * Check sector order.
		 */

		result = HEADER_PTR->parts.number - sector.header.parts.number;
		if((result == 1) || sector.need_sequence_reset)
			sector.need_sequence_reset = 0;
		else if(result > 1)
			errors.overrun++;
		else
			{
			errors.underflow += underflow_detect;
			underflow_detect = 0;
			continue;
			}
		sector.header.parts.number = HEADER_PTR->parts.number;

		/*
		 * Process the sector type.
		 */

		sector.header.all.state = HEADER_PTR->all.state;

		if(sector.header.parts.type == DATA_SECTOR)
			{
			sector.found_data = 1;
			break;
			}

		if(sector.header.parts.type == EOR_SECTOR)
			return(0);

		if(sector.header.parts.type == BOR_SECTOR)
			{
			errors = ERRORS_INITIALIZER;
			health = HEALTH_INITIALIZER;
			}
		}

	/*
	 * De-randomize the data and set the start and end pointers.
	 */

	bkr_sector_randomize((unsigned int *) sector.buffer, sector.data_size, sector.header.parts.number);

	if(sector.header.parts.truncate)
		sector.end = sector.start + sector.buffer[sector.data_size-1] + (sector.header.parts.hi_used << 8);
	else
		sector.end = sector.buffer + sector.data_size;
	sector.offset = sector.start;

	return(1);
}


/*
 * bkr_sector_write_data()
 *
 * Writes the current sector to the DMA buffer and resets for the next one.
 * Returns 0 on success, < 0 on error (on error, a retry can be attempted
 * by simply re-calling this function).
 */

static int bkr_sector_write_data(void)
{
	int  result;
	unsigned int  i;
	unsigned char  *data, *parity;

	if(sector.offset < sector.end)
		{
		sector.header.parts.truncate = 1;
		sector.buffer[sector.data_size-1] = (sector.offset - sector.start) & 0xff;
		sector.header.parts.hi_used = (sector.offset - sector.start) >> 8;
		sector.offset = sector.end;
		}

	/*
	 * Make room in the I/O buffer.
	 */

	result = bkr_device_write(sector.video_size+device.bytes_per_line);
	if(result < 0)
		return(result);

	/*
	 * Put the finishing touches on the sector data and write to tape.
	 */

	if(sector.header.parts.type == DATA_SECTOR)
		bkr_sector_randomize((unsigned int *) sector.buffer, sector.data_size, sector.header.parts.number);

	*HEADER_PTR = sector.header;

	data = sector.buffer;
	parity = sector.buffer + sector.data_size;
	for(i = 0; i < sector.interleave; data += block.data, parity += block.parity, i++)
		reed_solomon_encode(parity, data, &sector.rs_format);

	bkr_put_sector();

	/*
	 * Reset for the next sector
	 */

	sector.header.parts.number++;
	sector.header.parts.hi_used = 0;
	sector.header.parts.truncate = 0;
	sector.offset = sector.start;

	return(0);
}


/*
 * bkr_get_sector()
 *
 * Demodulates the next sector from the data stream.
 *
 * The search is done by computing a correlation coefficient for the data
 * stream against a copy of the key.  If this number goes above a threshold
 * then we've found the key and from its location the start of the sector can
 * be identified.  The value returned is the number of bytes in the data stream
 * that were skipped before the sector start was found.  On success, the DMA
 * buffer tail is left pointing to the first byte of sector data with enough
 * bytes in the buffer for the sector to be retrieved.  On failure (eg. on
 * -EWOULDBLOCK), the buffer tail is left pointing to the location it had
 * advanced to when the failure occured (is suitable for a retry).
 */

static int bkr_get_sector(void)
{
	int  result, skipped = -1;
	bkr_offset_t  i, j;
	unsigned int  correlation = 0;
#ifdef GCR_STUFF
	unsigned short  state, tmp;
	unsigned char  *in_pos, *out_pos;
	char  shift;
#endif

	/*
	 * Scan for the sector key in the byte stream
	 */

	device.tail--;
	do
		{
		device.tail++;
		skipped++;

		if(skipped > 0)
			if(correlation > health.best_nonkey)
				health.best_nonkey = correlation;

		result = bkr_device_read(sector.video_size);
		if(result < 0)
			return(result);

		correlation = 0;
		j = KEY_LENGTH;
		i = device.tail + sector.leader + KEY_LENGTH*sector.interleave;
		while(j)
			correlation += weight[device.buffer[i -= sector.interleave] ^ sector.header.parts.key[--j]];
		correlation /= KEY_LENGTH;
		}
	while(correlation < CORRELATION_THRESHOLD);

	device.tail += sector.leader;

	if(correlation < health.worst_key)
		health.worst_key = correlation;
	if(skipped < health.least_skipped)
		health.least_skipped = skipped;
	if(skipped > health.most_skipped)
		health.most_skipped = skipped;

	if(skipped > sector.trailer+device.bytes_per_line)
		errors.frame++;

	/*
	 * Apply the appropriate demodulation to the data in place.
	 */

#ifdef GCR_STUFF
	in_pos = out_pos = &device.buffer[device.tail];
	state = 0;
	shift = 1;
	while(1)
		{
		tmp = *(unsigned short *) (in_pos++);
#ifdef ROL
		ROL(tmp, shift);
#else
		tmp = (tmp >> 8) | (tmp << 8);
		tmp >>= 8 - shift;
#endif
		if(state & 1)
			tmp = ~tmp;
		state ^= tmp;
		*(out_pos++) = gcr_decode[tmp & GCR_MASK];
		if(++shift > 8)
			{
			if(++in_pos - &device.buffer[device.tail] >= sector.active_size)
				break;
			shift = 1;
			}
		}
#endif

	/*
	 * Deinterleave the sector data from the DMA buffer.
	 */

	for(i = 0; i < block.data; i += 1 - sector.data_size)
		for(; i < sector.data_size; i += block.data)
			sector.buffer[i] = device.buffer[device.tail++];
	for(i = sector.data_size; i < sector.data_size + block.parity; i += 1 - sector.parity_size)
		for(; i < sector.active_size; i += block.parity)
			sector.buffer[i] = device.buffer[device.tail++];

	return(0);
}


/*
 * bkr_put_sector()
 *
 * Write sector data to tape.  This step involves adding the header, the
 * footer, applying the appropriate bit modulation, and mixing with the key
 * sequence.
 */

static int bkr_put_sector(void)
{
	int  i;
#ifdef GCR_STUFF
	unsigned short  state, tmp;
	unsigned char  *in_pos, *out_pos;
	char  shift;
#else
	unsigned char  *out_pos;
#endif

	/*
	 * Write the leader to the DMA buffer
	 */

	memset(device.buffer + device.head, BKR_LEADER, sector.leader);
	device.head += sector.leader;

	/*
	 * Interleave the data into the DMA buffer
	 */

#ifdef GCR_STUFF
	out_pos = &device.buffer[device.head + sector.modulation_padding];
#else
	out_pos = &device.buffer[device.head];
#endif
	for(i = 0; i < block.data; i += 1 - sector.data_size)
		for(; i < sector.data_size; i += block.data)
			*(out_pos++) = sector.buffer[i];
	for(i = sector.data_size; i < sector.data_size + block.parity; i += 1 - sector.parity_size)
		for(; i < sector.active_size; i += block.parity)
			*(out_pos++) = sector.buffer[i];

	/*
	 * Apply the appropriate modulation to the data
	 */

#ifdef GCR_STUFF
	in_pos = &device.buffer[device.head + sector.modulation_padding];
	out_pos = &device.buffer[device.head];
	state = 0;
	tmp = 0;
	shift = 7;
	while(1)
		{
		if(state &= 1)
			state = GCR_MASK;
		state ^= gcr_encode[*(in_pos++)];
		tmp |= state << shift;
		tmp = (tmp >> 8) | (tmp << 8);
		if(--shift >= 0)
			{
			*(out_pos++) = tmp;  /* write low byte */
			tmp &= (unsigned short) 0xff00;
			}
		else
			{
			*( ((unsigned short *) out_pos)++ ) = tmp;
			if(out_pos >= in_pos)
				break;
			tmp = 0;
			shift = 7;
			}
		}
#endif
	device.head += sector.active_size;

	/*
	 * Write the trailer to the DMA buffer
	 */

	memset(device.buffer + device.head, BKR_TRAILER, sector.trailer);
	device.head += sector.trailer;

	if(sector.oddfield && (BKR_VIDEOMODE(sector.mode) == BKR_NTSC))
		{
		memset(device.buffer + device.head, BKR_TRAILER, device.bytes_per_line);
		device.head += device.bytes_per_line;
		}
	sector.oddfield ^= 1;

	if(device.head == device.size)
		device.head = 0;

	return(0);
}


/*
 * ================================================================================================
 *
 *                                         BY-PASS I/O
 *
 * ================================================================================================
 */

/*
 * bkr_sector_read_raw(), bkr_sector_write_raw()
 *
 * Read and write raw data.  Return codes are the same as for formated
 * reading/writing.
 */

static int bkr_sector_read_raw(void)
{
	int  result;
	unsigned int  count;

	result = bkr_device_read(sector.active_size);
	if(result < 0)
		return(result);

	if(device.tail + sector.active_size > device.size)
		{
		count = device.size - device.tail;
		memcpy(sector.start, device.buffer + device.tail, count);
		memcpy(sector.start + count, device.buffer, sector.active_size - count);
		}
	else
		memcpy(sector.start, device.buffer + device.tail, sector.active_size);
	device.tail += sector.active_size;

	sector.offset = sector.start;

	return(1);
}

static int bkr_sector_write_raw(void)
{
	int  result;
	unsigned int  count;

	result = bkr_device_write(sector.active_size);
	if(result < 0)
		return(result);

	if(device.head + sector.active_size >= device.size)
		{
		count = device.size - device.head;
		memcpy(device.buffer + device.head, sector.start, count);
		memcpy(device.buffer, sector.start + count, sector.active_size - count);
		device.head -= device.size;
		}
	else
		memcpy(device.buffer + device.head, sector.start, sector.active_size);
	device.head += sector.active_size;

	sector.offset = sector.start;

	return(0);
}
