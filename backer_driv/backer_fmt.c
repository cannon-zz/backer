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

#define  jiffies  0

#endif /* __KERNEL__ */

#include "rs.h"
#include "backer.h"
#include "backer_device.h"
#include "backer_fmt.h"


/*
 * Parameters
 */

#define  CORRELATION_THRESHOLD  204             /* 255 == 100% */


/*
 * Global Data
 *
 * 2280 = 2 * 2 * 2 * 3 * 5 * 19
 */

/* Read the docs for important info before adjusting these! */
static struct
        {
        unsigned int  leader;
        unsigned int  trailer;
	unsigned int  interleave;
        unsigned int  parity;
        } format[] =                            /* format information */
	{ {  32,  28,  8, 10 },                 /* FMT LOW  NTSC SP */
	  {  48,  44,  8, 10 },                 /* FMT LOW  NTSC EP */
	  {  40,  36,  8, 10 },                 /* FMT LOW  PAL  SP */
	  {  48,  48,  8, 10 },                 /* FMT LOW  PAL  EP */
	  {  80,  70, 20, 10 },                 /* FMT HIGH NTSC SP */
	  { 110, 140, 60, 10 },                 /* FMT HIGH NTSC EP */
	  { 100,  90, 20, 10 },                 /* FMT HIGH PAL  SP */
	  { 120, 120, 20, 10 },                 /* FMT HIGH PAL  EP */
	  {   0,   0,  1,  0 },                 /* RAW LOW  NTSC SP */
	  {   0,   0,  1,  0 },                 /* RAW LOW  NTSC EP */
	  {   0,   0,  1,  0 },                 /* RAW LOW  PAL  SP */
	  {   0,   0,  1,  0 },                 /* RAW LOW  PAL  EP */
	  {   0,   0,  1,  0 },                 /* RAW HIGH NTSC SP */
	  {   0,   0,  1,  0 },                 /* RAW HIGH NTSC EP */
	  {   0,   0,  1,  0 },                 /* RAW HIGH PAL  SP */
	  {   0,   0,  1,  0 } };               /* RAW HIGH PAL  EP */

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

static struct
	{
	unsigned int  size;
	unsigned int  data;
	unsigned int  parity;
	} block;                                /* R-S block info */


/*
 * Macros
 */

/* convert mode to an index for the format array */
static inline int mode_to_format(int mode)
{
	return(((BKR_FORMAT(mode) == BKR_RAW)    << 3) |
	       ((BKR_DENSITY(mode) == BKR_HIGH)  << 2) |
	       ((BKR_VIDEOMODE(mode) == BKR_PAL) << 1) |
	       ((BKR_SPEED(mode) == BKR_EP)      << 0));
}

static inline int sectors_per_second(void)
{
	return(BKR_VIDEOMODE(sector.mode) == BKR_PAL ? 50 : 60);
}

#define  HEADER_PTR  ((sector_header_t *) sector.buffer)


/*
 * Function prototypes.
 */

static void  bkr_sector_randomize(__u32 *, int, __u32);
static int   bkr_sector_read_fmt(f_flags_t, jiffies_t);
static int   bkr_sector_write_fmt(f_flags_t, jiffies_t);
static int   bkr_sector_read_raw(f_flags_t, jiffies_t);
static int   bkr_sector_write_raw(f_flags_t, jiffies_t);
int          bkr_find_sector(f_flags_t, jiffies_t);


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

	sector.buffer_size = sector.video_size - sector.leader - sector.trailer;
	block.size = sector.buffer_size / sector.interleave;
	block.data = block.size - block.parity;
	sector.data_size = block.data * sector.interleave;
	sector.parity_size = block.parity * sector.interleave;

	sector.header = SECTOR_HEADER_INITIALIZER;
	sector.oddfield = 1;
	sector.need_sequence_reset = 1;

	free(sector.buffer);
	sector.buffer = (unsigned char *) malloc(sector.buffer_size);
	if(sector.buffer == NULL)
		return(-ENOMEM);
	memset(sector.buffer, BKR_FILLER, sector.buffer_size);

	if(BKR_FORMAT(mode) == BKR_FMT)
		{
		sector.read = bkr_sector_read_fmt;
		sector.write = bkr_sector_write_fmt;
		sector.start = sector.buffer + sizeof(sector_header_t);
		}
	else
		{
		sector.read = bkr_sector_read_raw;
		sector.write = bkr_sector_write_raw;
		sector.start = sector.buffer;
		}

	sector.end = sector.buffer + sector.data_size;

	if(direction == WRITING)
		sector.offset = sector.start;
	else
		sector.offset = sector.end;

	reed_solomon_init(block.size, block.data, &sector.rs_format);

	errors = ERRORS_INITIALIZER;

	sector.mode = mode;

	return(0);
}


/*
 * space_in_buffer(), bytes_in_buffer()
 *
 * Return the space and bytes available in the DMA buffer.  Note that
 * space_in_buffer() + bytes_in_buffer() == device.size - 1.
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
 * bkr_write_bor() bkr_write_eor()
 *
 * Generate Begining-Of-Record and End-Of-Record marks.  Both return 0 on
 * success, < 0 on failure.
 */

int bkr_write_bor(jiffies_t bailout)
{
	int  result = 0;
	unsigned int  i;

	sector.header.parts.type = BOR_SECTOR;

	for(i = sectors_per_second() * BOR_LENGTH; i && !result; i--)
		{
		memset(sector.start, BKR_FILLER, sector.end - sector.start);
		sector.offset = sector.end;
		result = bkr_sector_write_fmt(0, bailout);
		}

	sector.header.parts.type = DATA_SECTOR;

	return(result);
}

int bkr_write_eor(jiffies_t bailout)
{
	int  result = 0;
	unsigned int  i;

	if(sector.offset != sector.start)
		result = bkr_sector_write_fmt(0, bailout);

	sector.header.parts.type = EOR_SECTOR;

	for(i = sectors_per_second() * EOR_LENGTH; i && !result; i--)
		{
		memset(sector.start, BKR_FILLER, sector.end - sector.start);
		sector.offset = sector.end;
		result = bkr_sector_write_fmt(0, bailout);
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
 * bkr_sector_read()
 *
 * Retrieves the next sector from the DMA buffer.  The algorithm is to loop
 * until we either find an acceptable sector (it's a type we understand and
 * is in the correct order) or we encounter an error or EOR.  If we find a
 * sector we like, we set it up for a read out and return 1.  Otherwise we
 * return < 0 on error or 0 on EOR.  On error, a retry can be attempted by
 * simply re-calling this function.
 */

static int bkr_sector_read_fmt(f_flags_t f_flags, jiffies_t bailout)
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

		result = bkr_find_sector(f_flags, bailout);
		if(result < 0)
			return(result);
		if((unsigned int) result > sector.trailer+device.bytes_per_line)
			errors.frame++;

		/*
		 * De-interleave the sector data from the DMA
		 * buffer.
		 */

		if(device.tail + sector.buffer_size >= device.size)
			{
			for(i = 0; i < block.data; i += 1 - sector.data_size)
				{
				if(device.tail + sector.interleave >= device.size)
					{
					for(; device.tail < device.size; i += block.data)
						sector.buffer[i] = device.buffer[device.tail++];
					device.tail = 0;
					}
				for(; i < sector.data_size; i += block.data)
					sector.buffer[i] = device.buffer[device.tail++];
				}
			for(i = sector.data_size; i < sector.data_size + block.parity; i += 1 - sector.parity_size)
				{
				if(device.tail + sector.interleave >= device.size)
					{
					for(; device.tail < device.size; i += block.parity)
						sector.buffer[i] = device.buffer[device.tail++];
					device.tail = 0;
					}
				for(; i < sector.buffer_size; i += block.parity)
					sector.buffer[i] = device.buffer[device.tail++];
				}
			}
		else
			{
			for(i = 0; i < block.data; i += 1 - sector.data_size)
				for(; i < sector.data_size; i += block.data)
					sector.buffer[i] = device.buffer[device.tail++];
			for(i = sector.data_size; i < sector.data_size + block.parity; i += 1 - sector.parity_size)
				for(; i < sector.buffer_size; i += block.parity)
					sector.buffer[i] = device.buffer[device.tail++];
			}

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
				errors.block++;
				sector.need_sequence_reset = 1;
				return(-ENODATA);
				}
			if((unsigned int) result > errors.symbol)
				errors.symbol = result;
			if((unsigned int) result > errors.recent_symbol)
				errors.recent_symbol = result;
			}

		/*
		 * Ensure correct sector order.
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
			break;

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
 * bkr_sector_write()
 *
 * Writes the current sector to the DMA buffer and resets for the next one.
 * Returns 0 on success, < 0 on error (on error, a retry can be attempted
 * by simply re-calling this function).
 */

static int bkr_sector_write_fmt(f_flags_t f_flags, jiffies_t bailout)
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

	result = bkr_device_write(sector.video_size+device.bytes_per_line, f_flags, bailout);
	if(result < 0)
		return(result);

	/*
	 * Put the finishing touches on the sector data
	 */

	if(sector.header.parts.type == DATA_SECTOR)
		bkr_sector_randomize((unsigned int *) sector.buffer, sector.data_size, sector.header.parts.number);

	*HEADER_PTR = sector.header;

	data = sector.buffer;
	parity = sector.buffer + sector.data_size;
	for(i = 0; i < sector.interleave; data += block.data, parity += block.parity, i++)
		reed_solomon_encode(parity, data, &sector.rs_format);

	/*
	 * Write the leader to the DMA buffer
	 */

	memset(device.buffer + device.head, BKR_LEADER, sector.leader);
	device.head += sector.leader;

	/*
	 * Interleave the data into the DMA buffer
	 */

	for(i = 0; i < block.data; i += 1 - sector.data_size)
		for(; i < sector.data_size; i += block.data)
			device.buffer[device.head++] = sector.buffer[i];
	for(i = sector.data_size; i < sector.data_size + block.parity; i += 1 - sector.parity_size)
		for(; i < sector.buffer_size; i += block.parity)
			device.buffer[device.head++] = sector.buffer[i];

#if 0   /* pad if using non-comensurate interleaving */
	i = sector.buffer_size % block.size;
	memset(device.buffer + device.head, BKR_FILLER, i);
	device.head += i;
#endif

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
 * bkr_find_sector()
 *
 * Finds the next sector in the data stream.  The search is done by
 * computing a correlation coefficient for the data stream against a copy
 * of the key.  If this number goes above a threshold then we've found the
 * key and from its location the start of the sector can be identified.
 * The value returned is the number of bytes in the data stream that were
 * skipped before the sector start was found.  On success, the DMA buffer
 * tail is left pointing to the first byte of sector data with enough bytes
 * in the buffer for the sector to be retrieved.  On failure (eg. on
 * -EWOULDBLOCK), the buffer tail is left pointing to the location it had
 * advanced to when the failure occured (is suitable for a retry).
 */

int bkr_find_sector(f_flags_t f_flags, jiffies_t bailout)
{
	int  result, skipped = -1;
	unsigned int  i, j;
	unsigned int  correlation = 0;

	device.tail--;
	do
		{
		if(++device.tail == device.size)
			device.tail = 0;
		skipped++;

		if(skipped > 0)
			if(correlation > health.best_nonkey)
				health.best_nonkey = correlation;

		result = bkr_device_read(sector.video_size, f_flags, bailout);
		if(result < 0)
			return(result);

		correlation = 0;
		j = KEY_LENGTH;
		i = device.tail + sector.leader + KEY_LENGTH*sector.interleave;
		if(i >= device.size)
			{
			i -= device.size;
			while((i >= sector.interleave) && j)
				correlation += weight[device.buffer[i -= sector.interleave] ^ sector.header.parts.key[--j]];
			i += device.size;
			}
		while(j)
			correlation += weight[device.buffer[i -= sector.interleave] ^ sector.header.parts.key[--j]];
		correlation /= KEY_LENGTH;
		}
	while(correlation < CORRELATION_THRESHOLD);

	if(correlation < health.worst_key)
		health.worst_key = correlation;
	if(skipped < health.least_skipped)
		health.least_skipped = skipped;
	if(skipped > health.most_skipped)
		health.most_skipped = skipped;

	device.tail += sector.leader;
	if(device.tail >= device.size)
		device.tail -= device.size;

	return(skipped);
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

static int bkr_sector_read_raw(f_flags_t f_flags, jiffies_t bailout)
{
	int  result;
	unsigned int  count;

	result = bkr_device_read(sector.buffer_size, f_flags, bailout);
	if(result < 0)
		return(result);

	if(device.tail + sector.buffer_size >= device.size)
		{
		count = device.size - device.tail;
		memcpy(sector.start, device.buffer + device.tail, count);
		memcpy(sector.start + count, device.buffer, sector.buffer_size - count);
		device.tail -= device.size;
		}
	else
		memcpy(sector.start, device.buffer + device.tail, sector.buffer_size);
	device.tail += sector.buffer_size;

	sector.offset = sector.start;

	return(1);
}

static int bkr_sector_write_raw(f_flags_t f_flags, jiffies_t bailout)
{
	int  result;
	unsigned int  count;

	result = bkr_device_write(sector.buffer_size, f_flags, bailout);
	if(result < 0)
		return(result);

	if(device.head + sector.buffer_size >= device.size)
		{
		count = device.size - device.head;
		memcpy(device.buffer + device.head, sector.start, count);
		memcpy(device.buffer, sector.start + count, sector.buffer_size - count);
		device.head -= device.size;
		}
	else
		memcpy(device.buffer + device.head, sector.start, sector.buffer_size);
	device.head += sector.buffer_size;

	sector.offset = sector.start;

	return(0);
}
