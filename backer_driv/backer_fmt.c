/*
 * backer_fmt
 *
 * Linux 2.0.xx driver for Danmere's Backer 16/32 video tape backup cards.
 *
 *                             Formating Layer
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


#ifdef __KERNEL__

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/malloc.h>
#include <linux/string.h>

#define  malloc(x)  kmalloc((x), GFP_KERNEL)
#define  free(x)    kfree(x)

#else

#include <errno.h>
#include <fcntl.h>
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
 *
 * Note the parity lengths must be multiples of 2.
 */

/* { bytes of leader, bytes of trailer, bytes of parity } */
#define  FORMATS  {{  32, 28, 10 },     /* LOW  NTSC SP */   \
                   {  32, 28, 14 },     /* LOW  NTSC EP */   \
                   {  40, 32, 10 },     /* LOW  PAL  SP */   \
                   {  40, 32, 14 },     /* LOW  PAL  EP */   \
                   {  80, 70, 10 },     /* HIGH NTSC SP */   \
                   {  80, 70, 14 },     /* HIGH NTSC EP */   \
                   { 100, 80, 10 },     /* HIGH PAL  SP */   \
                   { 100, 80, 14 }}     /* HIGH PAL  EP */

/* convert mode to an index for the format array (see bkrconfig.mode) */
static inline unsigned int BKR_MODE_TO_FORMAT(unsigned int mode)
{
	return((BKR_DENSITY(mode)==BKR_HIGH)<<2  | \
	       (BKR_VIDEOMODE(mode)==BKR_PAL)<<1 | \
	       (BKR_SPEED(mode)==BKR_EP));
}

#define WEIGHT_INITIALIZER                                   \
	{ 0xff, 0xf7, 0xf7, 0xdb, 0xf7, 0xdb, 0xdb, 0xa3,    \
	  0xf7, 0xdb, 0xdb, 0xa3, 0xdb, 0xa3, 0xa3, 0x5d,    \
	  0xf7, 0xdb, 0xdb, 0xa3, 0xdb, 0xa3, 0xa3, 0x5d,    \
	  0xdb, 0xa3, 0xa3, 0x5d, 0xa3, 0x5d, 0x5d, 0x25,    \
	  0xf7, 0xdb, 0xdb, 0xa3, 0xdb, 0xa3, 0xa3, 0x5d,    \
	  0xdb, 0xa3, 0xa3, 0x5d, 0xa3, 0x5d, 0x5d, 0x25,    \
	  0xdb, 0xa3, 0xa3, 0x5d, 0xa3, 0x5d, 0x5d, 0x25,    \
	  0xa3, 0x5d, 0x5d, 0x25, 0x5d, 0x25, 0x25, 0x09,    \
	  0xf7, 0xdb, 0xdb, 0xa3, 0xdb, 0xa3, 0xa3, 0x5d,    \
	  0xdb, 0xa3, 0xa3, 0x5d, 0xa3, 0x5d, 0x5d, 0x25,    \
	  0xdb, 0xa3, 0xa3, 0x5d, 0xa3, 0x5d, 0x5d, 0x25,    \
	  0xa3, 0x5d, 0x5d, 0x25, 0x5d, 0x25, 0x25, 0x09,    \
	  0xdb, 0xa3, 0xa3, 0x5d, 0xa3, 0x5d, 0x5d, 0x25,    \
	  0xa3, 0x5d, 0x5d, 0x25, 0x5d, 0x25, 0x25, 0x09,    \
	  0xa3, 0x5d, 0x5d, 0x25, 0x5d, 0x25, 0x25, 0x09,    \
	  0x5d, 0x25, 0x25, 0x09, 0x25, 0x09, 0x09, 0x01,    \
	  0xf7, 0xdb, 0xdb, 0xa3, 0xdb, 0xa3, 0xa3, 0x5d,    \
	  0xdb, 0xa3, 0xa3, 0x5d, 0xa3, 0x5d, 0x5d, 0x25,    \
	  0xdb, 0xa3, 0xa3, 0x5d, 0xa3, 0x5d, 0x5d, 0x25,    \
	  0xa3, 0x5d, 0x5d, 0x25, 0x5d, 0x25, 0x25, 0x09,    \
	  0xdb, 0xa3, 0xa3, 0x5d, 0xa3, 0x5d, 0x5d, 0x25,    \
	  0xa3, 0x5d, 0x5d, 0x25, 0x5d, 0x25, 0x25, 0x09,    \
	  0xa3, 0x5d, 0x5d, 0x25, 0x5d, 0x25, 0x25, 0x09,    \
	  0x5d, 0x25, 0x25, 0x09, 0x25, 0x09, 0x09, 0x01,    \
	  0xdb, 0xa3, 0xa3, 0x5d, 0xa3, 0x5d, 0x5d, 0x25,    \
	  0xa3, 0x5d, 0x5d, 0x25, 0x5d, 0x25, 0x25, 0x09,    \
	  0xa3, 0x5d, 0x5d, 0x25, 0x5d, 0x25, 0x25, 0x09,    \
	  0x5d, 0x25, 0x25, 0x09, 0x25, 0x09, 0x09, 0x01,    \
	  0xa3, 0x5d, 0x5d, 0x25, 0x5d, 0x25, 0x25, 0x09,    \
	  0x5d, 0x25, 0x25, 0x09, 0x25, 0x09, 0x09, 0x01,    \
	  0x5d, 0x25, 0x25, 0x09, 0x25, 0x09, 0x09, 0x01,    \
	  0x25, 0x09, 0x09, 0x01, 0x09, 0x01, 0x01, 0x00 };

#define  CORRELATION_THRESHOLD  0xcc


/*
 * Function prototypes
 */

static int   bkr_block_read_fmt(f_flags_t, jiffies_t);
static int   bkr_block_write_fmt(f_flags_t, jiffies_t);
static int   bkr_block_read_raw(f_flags_t, jiffies_t);
static int   bkr_block_write_raw(f_flags_t, jiffies_t);
static int   bkr_sector_blocks_remaining(void);
static void  bkr_sector_randomize(void *, int, __u32);
static int   bkr_sector_read(f_flags_t, jiffies_t);
static int   bkr_sector_write(f_flags_t, jiffies_t);
static int   bkr_find_sector(f_flags_t, jiffies_t);


/*
 * Global Data
 */

static struct
        {
        unsigned int  leader;
        unsigned int  trailer;
        unsigned int  parity;
        } format[] = FORMATS;                           /* format information */
static unsigned char  weight[] = WEIGHT_INITIALIZER;    /* correlation weights */


/*
 * Macros
 */

static inline int BLOCKS_PER_SECOND(void)
{
	return(sector.interleave * (BKR_VIDEOMODE(device.mode) == BKR_PAL ? 50 : 60));
}

static inline int SECTOR_KEY_OFFSET(void)
{
	return(sector.leader + (block.parity + sizeof(block_header_t))*sector.interleave);
}


/*
 * ================================================================================================
 *
 *                                      MISCELLANEOUS
 *
 * ================================================================================================
 */

/*
 * bkr_set_parms()
 *
 * Sets all the format and device parameters based on the selected mode.
 * The mode parameter is as in bkrmode.  Returns -EINVAL if the supplied
 * mode is invalid, -ENOMEM if the various buffers couldn't be
 * allocated/resized, or 0 if everything is A-OK.
 *
 * An even number of bytes is always allocated for the block buffer so the
 * randomizer can work 16 bits at a time.
 */

int  bkr_set_parms(unsigned int mode, unsigned int max_buffer)
{
	device.control = 0;

	switch(BKR_DENSITY(mode))
		{
		case BKR_HIGH:
		device.control |= BIT_HIGH_DENSITY;
		device.bytes_per_line = BYTES_PER_LINE_HIGH;
		break;

		case BKR_LOW:
		device.bytes_per_line = BYTES_PER_LINE_LOW;
		break;

		default:
		goto bad_mode;
		}

	switch(BKR_VIDEOMODE(mode))
		{
		case BKR_NTSC:
		device.control |= BIT_NTSC_VIDEO;
		sector.size = device.bytes_per_line * LINES_PER_FIELD_NTSC;
		device.size = sector.size * 2 + device.bytes_per_line;
		break;

		case BKR_PAL:
		sector.size = device.bytes_per_line * (LINES_PER_FIELD_PAL - 1);
		device.size = (sector.size + device.bytes_per_line) * 2;
		break;

		default:
		goto bad_mode;
		}
	device.size = max_buffer - max_buffer % device.size;

	sector.interleave = device.bytes_per_line * 2;
	sector.leader  = format[BKR_MODE_TO_FORMAT(mode)].leader;
	sector.trailer = format[BKR_MODE_TO_FORMAT(mode)].trailer;
	sector.data_size = sector.size - sector.leader - sector.trailer;

	switch(BKR_FORMAT(mode))
		{
		case BKR_FMT:
		block.read = bkr_block_read_fmt;
		block.write = bkr_block_write_fmt;
		block.size = sector.data_size / sector.interleave;
		block.parity = format[BKR_MODE_TO_FORMAT(mode)].parity;
		break;

		case BKR_RAW:
		block.read = bkr_block_read_raw;
		block.write = bkr_block_write_raw;
		block.size = sector.data_size;
		block.parity = 0;
		break;

		default:
		goto bad_mode;
		}

	free(sector.data);
	sector.data = (unsigned char *) malloc(sector.data_size);
	if(sector.data == NULL)
		return(-ENOMEM);

	reed_solomon_init(block.size, block.size - block.parity, &block.rs_format);

	device.mode = mode;
	return(0);

	/*
	 * The specified mode does not exist --- use default mode.
	 */

	bad_mode:
	bkr_set_parms(BKR_DEF_MODE, max_buffer);
	return(-EINVAL);
}


/*
 * bkr_format_reset()
 *
 * Reset the block and sector formating layers.
 */

void bkr_format_reset(void)
{
	/*
	 * Sector layer
	 */

	sector.header_loc = (sector_header_t *) (sector.data + block.parity + sizeof(block_header_t));
	sector.header = (sector_header_t) { KEY_INITIALIZER, 0 };
	sector.oddfield = 1;
	sector.need_sequence_reset = 1;
	sector.offset = sector.data;
	if(device.direction == O_WRONLY)
		sector.offset += sector.data_size - block.size;

	/*
	 * Block layer
	 */

	block.header.type = DATA_BLOCK;
	block.header.header = 0;
	block.header.truncate = 0;
	switch(BKR_FORMAT(device.mode))
		{
		case BKR_FMT:
		block.start = sector.offset + block.parity + sizeof(block_header_t);
		break;

		case BKR_RAW:
		block.start = sector.data;
		break;
		}
	block.end = block.start + block.size;
	if(device.direction == O_WRONLY)
		block.offset = block.start;
	else
		block.offset = block.end;

	/*
	 * Stats
	 */

	errors = ERRORS_INITIALIZER;

	return;
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
 *                                      BLOCK-LEVEL FORMATING
 *
 * ================================================================================================
 *
 * Any errors occuring in the sector or device layers cancel the current
 * operation and the error code is passed along.
 */

/*
 * bkr_write_bor() bkr_write_eor()
 *
 * Generate Begining-Of-Record and End-Of-Record marks
 */

int bkr_write_bor(jiffies_t bailout)
{
	int  result = 0;
	unsigned int  i;

	block.header.type = BOR_BLOCK;

	for(i = BLOCKS_PER_SECOND() * BOR_LENGTH; i && !result; i--)
		{
		block.offset = block.end;
		result = bkr_block_write_fmt(0, bailout);
		}

	block.header.type = DATA_BLOCK;

	return(result);
}

int bkr_write_eor(jiffies_t bailout)
{
	int  result = 0;
	unsigned int  i;

	if(block.offset != block.start)
		{
		result = bkr_block_write_fmt(0, bailout);
		if(result < 0)
			return(result);
		}

	block.header.type = EOR_BLOCK;

	for(i = bkr_sector_blocks_remaining() + BLOCKS_PER_SECOND() * EOR_LENGTH; i && !result; i--)
		{
		block.offset = block.end;
		result = bkr_block_write_fmt(0, bailout);
		}

	return(result);
}

/*
 * bkr_block_read_fmt()
 *
 * Read and decode one block of formated data.  We loop until we find a
 * block type that we know how to process.  If a BOR block is found then we
 * reset all the appropriate counters but otherwise skip it too.  Returns <
 * 0 on error, 1 on success and 0 on EOR.
 */

static int bkr_block_read_fmt(f_flags_t f_flags, jiffies_t bailout)
{
	int  result;

	while(1)
		{
		result = bkr_sector_read(f_flags, bailout);

		block.start = sector.offset + block.parity + sizeof(block_header_t);
		block.end = sector.offset + block.size;

		if(result < 0)
			{
			block.offset = block.end;
			return(result);
			}

		block.header = *(block_header_t *) (sector.offset + block.parity);
		if(block.header.header)
			block.start += sizeof(sector_header_t);
		block.offset = block.start;

		if(block.header.type == EOR_BLOCK)
			{
			block.end = block.start;
			return(0);
			}

		if(block.header.truncate)
			block.end = block.start + *(block.end - 1);

		if(block.header.type == DATA_BLOCK)
			return(1);

		if(block.header.type == BOR_BLOCK)
			{
			errors = ERRORS_INITIALIZER;
			best_nonmatch = 0;
			worst_match = 1000000;
			least_skipped = 1000000;
			most_skipped = 0;
			}
		}
}

/*
 * bkr_block_write_fmt()
 *
 * Encode and write one block of formated data.  After writing, the next
 * block is prepared:  the header is set up appropriately and space is set
 * aside for the sector header if needed.
 */

static int bkr_block_write_fmt(f_flags_t f_flags, jiffies_t bailout)
{
	int  result;

	if(block.offset < block.end)
		{
		block.header.truncate = 1;
		*(block.end - 1) = block.offset - block.start;
		block.offset = block.end;
		}

	*(block_header_t *) (sector.offset + block.parity) = block.header;

	result = bkr_sector_write(f_flags, bailout);
	if(result < 0)
		return(result);

	block.start = sector.offset + block.parity + sizeof(block_header_t);
	block.end = sector.offset + block.size;
	block.header.header = 0;
	block.header.truncate = 0;
	if(result)
		{
		block.header.header = 1;
		block.start += sizeof(sector_header_t);
		}
	block.offset = block.start;

	return(0);
}

/*
 * bkr_block_read_raw(), bkr_block_write_raw()
 *
 * Read and write raw data, by-passing the sector layer.
 */

static int bkr_block_read_raw(f_flags_t f_flags, jiffies_t bailout)
{
	int  result;
	unsigned int  count;

	result = bkr_device_read(block.size, f_flags, bailout);
	if(result < 0)
		return(result);

	if(device.tail + block.size >= device.size)
		{
		count = device.size - device.tail;
		memcpy(block.start, device.buffer + device.tail, count);
		memcpy(block.start + count, device.buffer, block.size - count);
		device.tail -= device.size;
		}
	else
		memcpy(block.start, device.buffer + device.tail, block.size);
	device.tail += block.size;

	block.offset = block.start;

	return(1);
}

static int bkr_block_write_raw(f_flags_t f_flags, jiffies_t bailout)
{
	int  result;
	unsigned int  count;

	result = bkr_device_write(block.size, f_flags, bailout);
	if(result < 0)
		return(result);

	if(device.head + block.size >= device.size)
		{
		count = device.size - device.head;
		memcpy(device.buffer + device.head, block.start, count);
		memcpy(device.buffer, block.start + count, block.size - count);
		device.head -= device.size;
		}
	else
		memcpy(device.buffer + device.head, block.start, block.size);
	device.head += block.size;

	block.offset = block.start;

	return(0);
}


/*
 * ================================================================================================
 *
 *                                     SECTOR-LEVEL FORMATING
 *
 * ================================================================================================
 *
 * Any errors occuring at the device level cancel the current operation and
 * the error code is passed along to the block layer.
 */

/*
 * bkr_sector_blocks_remaining()
 *
 * Returns the number of blocks required to fill the rest of the current
 * sector.
 */

static int bkr_sector_blocks_remaining(void)
{
	return((sector.offset - sector.data) / block.size + 1);
}


/*
 * bkr_sector_randomize()
 *
 * (De-)Randomize the data area of a sector using the supplied seed.
 * The count of bytes to randomize must be a multiple of 4.
 *
 * For the random number generator used see:
 * Knuth, D. E. 1981, Semi-Numerical Algorithms, 2nd ed., vol 2 of The Art of
 *    Computer Programing.
 */

static void bkr_sector_randomize(void *location, int count, __u32 seed)
{
	unsigned int  index;
	unsigned int  history[4];

	for(index = 0; index < 4; index++)
		{
		seed = 1664525 * seed + 1013904223;
		history[index] = seed;
		}

	for(count >>= 2; count; count--)
		{
		seed = 1664525 * seed + 1013904223;
		index = seed >> 30;
		((__u32 *)location)[count] ^= history[index];
		history[index] = seed;
		}
}


/*
 * bkr_sector_read()
 *
 * Retrieves the next block from the current sector.  If we're at the end
 * of the current sector then we move to the next one.
 */

static int bkr_sector_read(f_flags_t f_flags, jiffies_t bailout)
{
	int  result;
	int  i;
	char  underflow_detect = 1;

	if(sector.offset == sector.data)
		{
		while(1)
			{
			/*
			 * Get the next sector.
			 */

			result = bkr_find_sector(f_flags, bailout);
			if(result < 0)
				return(result);
			if(result > sector.trailer+device.bytes_per_line)
				errors.frame++;

			/*
			 * De-interleave the sector data from the DMA
			 * buffer.
			 */

			if(device.tail + sector.data_size >= device.size)
				{
				for(i = 0; i < block.size; i += 1 - sector.data_size)
					{
					if(device.tail + sector.interleave >= device.size)
						{
						for(; device.tail < device.size; i += block.size)
							sector.data[i] = device.buffer[device.tail++];
						device.tail = 0;
						}
					for(; i < sector.data_size; i += block.size)
						sector.data[i] = device.buffer[device.tail++];
					}
				}
			else
				{
				for(i = 0; i < block.size; i += 1 - sector.data_size)
					for(; i < sector.data_size; i += block.size)
						sector.data[i] = device.buffer[device.tail++];
				}

			/*
			 * Do error correction.
			 */

			for(i = result = 0; (i < sector.data_size) && (result >= 0); i += block.size)
				{
				result = reed_solomon_decode(&sector.data[i], NULL, 0, &block.rs_format);
				if(result > (int) errors.symbol)
					errors.symbol = result;
				}

			if(result < 0)
				{
				errors.block++;
				sector.need_sequence_reset = 1;
				return(-ENODATA);
				}

			/*
			 * Recover the sector number and skip repeated
			 * sectors.
			 */

			if(sector.need_sequence_reset)
				sector.need_sequence_reset = 0;
			else
				{
				result = sector.header_loc->number - sector.header.number;

				if(result > 1)
					errors.overrun++;
				else if(result < 1)
					{
					errors.underflow += underflow_detect;
					underflow_detect = 0;
					continue;
					}
				}
			sector.header.number = sector.header_loc->number;
			break;
			}

		/*
		 * De-randomize the data and reset this sector.
		 */

		bkr_sector_randomize(sector.data, sector.data_size, sector.header.number);

		sector.offset = sector.data + sector.data_size;
		}

	sector.offset -= block.size;
	return(0);
}


/*
 * bkr_sector_write()
 *
 * Writes a block into the current sector.  If the current sector is full
 * then we write it to the data stream and start on the next one.  The
 * return value indicates whether or not the next block must be a key
 * block.
 */

static int bkr_sector_write(f_flags_t f_flags, jiffies_t bailout)
{
	int  result;
	int  i;

	if(sector.offset == sector.data)
		{
		result = bkr_device_write(sector.size+device.bytes_per_line, f_flags, bailout);
		if(result < 0)
			return(result);

		/*
		 * Put the finishing touches on the sector data
		 */

		bkr_sector_randomize(sector.data, sector.data_size, sector.header.number);
		*sector.header_loc = sector.header;
		for(i = 0; i < sector.data_size; i += block.size)
			reed_solomon_encode(&sector.data[i], &block.rs_format);

		/*
		 * Write the leader to the DMA buffer
		 */

		memset(device.buffer + device.head, BKR_FILLER, sector.leader);
		device.head += sector.leader;

		/*
		 * Interleave the data into the DMA buffer
		 */

		for(i = 0; i < block.size; i += 1 - sector.data_size)
			for(; i < sector.data_size; i += block.size)
				device.buffer[device.head++] = sector.data[i];

		/*
		 * Write the trailer to the DMA buffer
		 */

		memset(device.buffer + device.head, BKR_FILLER, sector.trailer);
		device.head += sector.trailer;

		if(sector.oddfield || (BKR_VIDEOMODE(device.mode) == BKR_PAL))
			{
			memset(device.buffer + device.head, BKR_FILLER, device.bytes_per_line);
			device.head += device.bytes_per_line;
			}
		sector.oddfield ^= 1;

		if(device.head == device.size)
			device.head = 0;

		/*
		 * Reset for the next sector
		 */

		sector.header.number++;
		sector.offset = sector.data + sector.data_size;
		}

	sector.offset -= block.size;
	if(sector.offset == sector.data)
		return(1);
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

static int bkr_find_sector(f_flags_t f_flags, jiffies_t bailout)
{
	int  result;
	unsigned int  i, j;
	unsigned int  correlation;
	unsigned int  skipped = -1;

	result = bkr_device_read(SECTOR_KEY_OFFSET(), f_flags, bailout);
	if(result < 0)
		return(result);

	device.tail += SECTOR_KEY_OFFSET() - 1;
	if(device.tail >= device.size)
		device.tail -= device.size;

	do
		{
		if(++device.tail == device.size)
			device.tail = 0;
		skipped++;

		if(skipped > 0)
			if(correlation > best_nonmatch)
				best_nonmatch = correlation;

		result = bkr_device_read(sector.size - SECTOR_KEY_OFFSET(), f_flags, bailout);
		if(result < 0)
			break;

		correlation = 0;
		j = KEY_LENGTH;
		i = device.tail + KEY_LENGTH*sector.interleave;
		if(i >= device.size)
			{
			i -= device.size;
			while(i >= sector.interleave)
				correlation += weight[device.buffer[i -= sector.interleave] ^ sector.header.key[--j]];
			i += device.size;
			}
		while(j)
			correlation += weight[device.buffer[i -= sector.interleave] ^ sector.header.key[--j]];
		correlation /= KEY_LENGTH;
		}
	while(correlation < CORRELATION_THRESHOLD);

	device.tail -= SECTOR_KEY_OFFSET();
	if(result >= 0)
		{
		if(correlation < worst_match)
			worst_match = correlation;
		if(skipped < least_skipped)
			least_skipped = skipped;
		if(skipped > most_skipped)
			most_skipped = skipped;

		result = skipped;
		device.tail += sector.leader;
		}
	if((int) device.tail < 0)
		device.tail += device.size;

	return(result);
}
