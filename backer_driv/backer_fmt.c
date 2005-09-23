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

#define malloc(x)  kmalloc((x), GFP_KERNEL)
#define free(x)    kfree(x)

#else

#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <string.h>

#define jiffies  0

#endif /* __KERNEL__ */

#include "rs.h"
#include "backer.h"
#include "backer_device.h"
#include "backer_fmt.h"

#define  BLOCKS_PER_SECOND  (device.bytes_per_line*2*(BKR_VIDEOMODE(device.mode)==BKR_PAL?50:60))
#define  SECTOR_KEY_OFFSET  (sector.header_length+(block.parity+sizeof(header_t))*device.bytes_per_line)


/*
 * Function prototypes
 */

static int           bkr_block_read_fmt(f_flags_t, jiffies_t);
static int           bkr_block_write_fmt(f_flags_t, jiffies_t);
static int           bkr_block_read_raw(f_flags_t, jiffies_t);
static int           bkr_block_write_raw(f_flags_t, jiffies_t);
static void          bkr_block_randomize(__u32);
static unsigned int  bkr_sector_blocks_remaining(void);
static int           bkr_sector_read(f_flags_t, jiffies_t);
static int           bkr_sector_write(f_flags_t, jiffies_t);
static int           bkr_get_sector(f_flags_t, jiffies_t);


/*
 * Global Data
 */

static struct bkr_format  format[] = BKR_FORMATS;       /* format information */
static unsigned char  key[] = KEY_INITIALIZER;          /* white noise */
static unsigned char  weight[] = WEIGHT_INITIALIZER;    /* anti-correlation weights */


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
 * Sets all the format parameters based on the selected mode.  The mode
 * parameter is as in bkrmode.  Returns -EINVAL if the supplied mode is
 * invalid, -ENOMEM if the various buffers couldn't be allocated/resized,
 * or 0 if everything is A-OK.
 *
 * An even number of bytes is always allocated for the block buffer so the
 * randomizer can work 16 bits at a time.
 */

int  bkr_set_parms(unsigned int mode, unsigned int max_buffer)
{
	if(((BKR_DENSITY(mode) != BKR_HIGH) && (BKR_DENSITY(mode) != BKR_LOW)) ||
	   ((BKR_VIDEOMODE(mode) != BKR_NTSC) && (BKR_VIDEOMODE(mode) != BKR_PAL)) ||
	   ((BKR_FORMAT(mode) != BKR_FMT) && (BKR_FORMAT(mode) != BKR_RAW)))
		return(-EINVAL);

	device.mode = mode;

	switch(BKR_DENSITY(device.mode))
		{
		case BKR_HIGH:
		device.command = BIT_HIGH_DENSITY;
		device.bytes_per_line = BYTES_PER_LINE_HIGH;
		break;

		case BKR_LOW:
		device.command = 0;
		device.bytes_per_line = BYTES_PER_LINE_LOW;
		break;
		}

	switch(BKR_VIDEOMODE(device.mode))
		{
		case BKR_NTSC:
		device.command |= BIT_NTSC_VIDEO;
		sector.size = device.bytes_per_line * LINES_PER_FIELD_NTSC;
		break;

		case BKR_PAL:
		sector.size = device.bytes_per_line * LINES_PER_FIELD_PAL;
		break;
		}

	device.size = max_buffer - max_buffer % (sector.size * 2 + device.bytes_per_line);

	sector.header_length = format[BKR_MODE_TO_FORMAT(device.mode)].header_length;
	sector.aux_length = format[BKR_MODE_TO_FORMAT(device.mode)].aux_length;
	sector.footer_length = format[BKR_MODE_TO_FORMAT(device.mode)].footer_length;
	sector.footer_offset = sector.size - sector.footer_length;

	switch(BKR_FORMAT(device.mode))
		{
		case BKR_FMT:
		block.read = bkr_block_read_fmt;
		block.write = bkr_block_write_fmt;
		block.size = (sector.size - sector.header_length - sector.footer_length -
		              sector.aux_length) / device.bytes_per_line / 2;
		block.parity = format[BKR_MODE_TO_FORMAT(device.mode)].parity;
		block.header = (header_t *) block.parity;
		block.start = (unsigned char *) block.parity + sizeof(header_t);
		break;

		case BKR_RAW:
		block.read = bkr_block_read_raw;
		block.write = bkr_block_write_raw;
		block.size = sector.size;
		block.parity = 0;
		block.header = 0;
		block.start = 0;
		break;
		}

	free(sector.buffer);
	free(block.buffer);
	sector.buffer = (unsigned char *) malloc(sector.size);
	block.buffer = (unsigned char *) malloc(((block.size + 1) >> 1) << 1);
	if((sector.buffer == NULL) || (block.buffer == NULL))
		return(-ENOMEM);

	sector.aux = sector.buffer + sector.header_length + block.size * device.bytes_per_line;
	sector.footer = sector.buffer + sector.footer_offset;

	block.header = (header_t *) (block.buffer + (unsigned int) block.header);
	block.start = block.buffer + (unsigned int) block.start;

	reed_solomon_init(block.size, block.size - block.parity, &block.rs_format);

	return(0);
}


/*
 * bkr_format_reset()
 *
 * Reset the block and sector formating layers.  This also resets the head
 * and tail offsets for the device buffer.  NOTE:  this has no bearing on
 * the actual I/O transfers which must always start at 0, this is done
 * merely to force the sector layer to start at 0 as well.
 */

void bkr_format_reset(void)
{
	/*
	 * Block and sector layers
	 */

	block.end = block.buffer + block.size;
	block.sequence = 0;
	memset(sector.buffer, BKR_FILLER, sector.size);
	if(device.direction == O_WRONLY)
		{
		*block.header = DATA_BLOCK | BLOCK_SEQ(block.sequence);
		block.offset = block.start;
		sector.block = sector.footer + device.bytes_per_line;
		}
	else
		{
		block.offset = block.end;
		sector.block = sector.aux;
		}

	/*
	 * Device layer
	 */

	device.oddfield = 1;
	device.head = 0;
	if(device.direction == O_WRONLY)
		device.tail = 1;
	else
		device.tail = 0;

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

	block.offset = block.start;

	*block.header &= ~BLOCK_TYPE(-1);
	*block.header |= BOR_BLOCK;

	for(i = BLOCKS_PER_SECOND * BOR_LENGTH; i && (jiffies < bailout) && !result; i--)
		{
		memset(block.offset, BKR_FILLER, block.end - block.offset);
		block.offset = block.end;
		result = bkr_block_write_fmt(0, bailout);
		}

	*block.header &= ~BLOCK_TYPE(-1);
	*block.header |= DATA_BLOCK;

	return(result);
}

int bkr_write_eor(jiffies_t bailout)
{
	int  result;
	unsigned int  i;

	if(block.offset != block.start)
		{
		result = bkr_block_write_fmt(0, bailout);
		if(result < 0)
			return(result);
		}

	*block.header &= ~BLOCK_TYPE(-1);
	*block.header |= EOR_BLOCK;

	for(i = bkr_sector_blocks_remaining() + BLOCKS_PER_SECOND * EOR_LENGTH;
	    i && (jiffies < bailout); i--)
		{
		memset(block.offset, BKR_FILLER, block.end - block.offset);
		block.offset = block.end;
		result = bkr_block_write_fmt(0, bailout);
		if(result < 0)
			return(result);
		}

	result = bkr_device_flush(bailout);

	return(result);
}

/*
 * bkr_block_read_fmt()
 *
 * Read and decode one block of formated data.  If a BOR block is found
 * then the block sequence counter is reset and the block skipped.  If a
 * non-BOR block is found but it is out of sequence then it too is skipped.
 * Otherwise the block capacity is set appropriately and the block type
 * returned to the calling function.
 */

static int bkr_block_read_fmt(f_flags_t f_flags, jiffies_t bailout)
{
	int  result;
	static char  need_sequence_reset = 0;

	while(1)
		{
		result = bkr_sector_read(f_flags, bailout);

		if(result < 0)
			{
			block.offset = block.end;
			return(result);
			}

		result = reed_solomon_decode(block.buffer, NULL, 0, &block.rs_format);

		if(result < 0)
			{
			errors.block++;
			block.offset = block.end;
			need_sequence_reset = 1;
			return(-ENODATA);
			}

		if(result > errors.symbol)
			errors.symbol = result;

		if(BLOCK_TYPE(*block.header) == BOR_BLOCK)
			{
			block.sequence = BLOCK_SEQ(*block.header);
			errors = ERRORS_INITIALIZER;
			best_nonmatch = 1000000;
			worst_match = 0;
			least_skipped = 1000000;
			most_skipped = 0;
			continue;
			}

		result = BLOCK_SEQ(BLOCK_SEQ(*block.header) - block.sequence);

		if((result > 1) && (result <= BLOCK_SEQ(-1) >> 1))
			{
			errors.overrun++;
			need_sequence_reset = 1;
			}

		if((result == 1) || need_sequence_reset)
			{
			block.sequence = BLOCK_SEQ(*block.header);
			need_sequence_reset = 0;
			break;
			}
		}

	bkr_block_randomize(block.sequence);

	block.offset = block.start;
	if(KEY_BLOCK(*block.header))
		block.offset += KEY_LENGTH;

	if(BLOCK_TYPE(*block.header) == EOR_BLOCK)
		{
		block.end = block.offset;
		return(EOR_BLOCK);
		}

	block.end = block.buffer;
	if(TRUNCATE_BLOCK(*block.header))
		block.end += block.buffer[block.size-1];
	else
		block.end += block.size;

	return(0);
}

/*
 * bkr_block_write_fmt()
 *
 * Encode and write one block of formated data.  After writing, the block
 * buffer is prepared for the next block:  the header is set up
 * appropriately, and the sector key is inserted if needed.
 */

static int bkr_block_write_fmt(f_flags_t f_flags, jiffies_t bailout)
{
	int  result;

	if(block.offset < block.end)
		{
		*block.header |= TRUNCATE_BLOCK(-1);
		block.buffer[block.size-1] = block.offset - block.buffer;
		}

	bkr_block_randomize(block.sequence);
	reed_solomon_encode(block.buffer, &block.rs_format);

	result = bkr_sector_write(f_flags, bailout);

	if(result < 0)
		{
		*block.header &= ~TRUNCATE_BLOCK(-1);

		block.offset = block.start;
		if(KEY_BLOCK(*block.header))
			block.offset += KEY_LENGTH;

		return(result);
		}

	*block.header &= BLOCK_TYPE(-1);
	*block.header |= block.sequence = BLOCK_SEQ(block.sequence + 1);

	block.offset = block.start;
	if(result == KEY_BLOCK(-1))
		{
		*block.header |= KEY_BLOCK(-1);
		memcpy(block.offset, key, KEY_LENGTH);
		block.offset += KEY_LENGTH;
		}

	return(0);
}

/*
 * bkr_block_randomize()
 *
 * (De-)Randomize the data area of a block based on the supplied seed.
 *
 * For the random number generator used see:
 * Knuth, D. E. 1981, Semi-Numerical Algorithms, 2nd ed., vol 2 of The Art of
 *    Computer Programing.
 */

static void bkr_block_randomize(__u32 num)
{
	unsigned int  i, last;

	if(BLOCK_TYPE(*block.header) != DATA_BLOCK)
		return;

	if(KEY_BLOCK(*block.header))
		last = (block.parity + sizeof(header_t) + KEY_LENGTH) >> 1;
	else
		last = (block.parity + sizeof(header_t)) >> 1;

	for(i = (block.size+1) >> 1; i > last; )
		{
		num = 1664525 * num + 1013904223;
		*(((__u16 *) block.buffer) + --i) ^= num >> 16;
		}
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
		{
		block.offset = block.end;
		return(result);
		}

	if(device.tail + block.size >= device.size)
		{
		count = device.size - device.tail;
		memcpy(block.buffer, device.buffer + device.tail, count);
		memcpy(block.buffer + count, device.buffer, block.size - count);
		device.tail -= device.size;
		}
	else
		memcpy(block.buffer, device.buffer + device.tail, block.size);
	device.tail += block.size;
	block.offset = block.buffer;

	return(0);
}

static int bkr_block_write_raw(f_flags_t f_flags, jiffies_t bailout)
{
	int  result;
	unsigned int  count;

	result = bkr_device_write(block.size, f_flags, bailout);
	if(result < 0)
		{
		block.offset = block.buffer;
		return(result);
		}

	if(device.head + block.size >= device.size)
		{
		count = device.size - device.head;
		memcpy(device.buffer + device.head, block.buffer, count);
		memcpy(device.buffer, block.buffer + count, block.size - count);
		device.head -= device.size;
		}
	else
		memcpy(device.buffer + device.head, block.buffer, block.size);
	device.head += block.size;
	block.offset = block.buffer;

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

static unsigned int bkr_sector_blocks_remaining(void)
{
	if(sector.block >= sector.footer)
		return(sector.block - sector.footer + device.bytes_per_line);
	return(sector.block - sector.aux);
}


/*
 * bkr_sector_read()
 *
 * Retrieves the next block from the current sector.  If we're at the end
 * of the current sector then we move to the next one.
 */

static int bkr_sector_read(f_flags_t f_flags, jiffies_t bailout)
{
	int  result, count;
	unsigned char  *location;

	if(sector.block == sector.aux)
		{
		result = bkr_get_sector(f_flags, bailout);
		if(result < 0)
			return(result);
		sector.block = sector.footer + device.bytes_per_line;
		}
	else if(sector.block == sector.footer)
		sector.block = sector.aux + device.bytes_per_line;

	location = --sector.block;
	for(count = block.size; count;)
		block.buffer[--count] = *(location -= device.bytes_per_line);

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
	int  result, count;
	unsigned char  *location;

	if(sector.block == sector.aux)
		{
		result = bkr_device_write(sector.size+device.bytes_per_line, f_flags, bailout);
		if(result < 0)
			return(result);

		memcpy(device.buffer + device.head, sector.buffer, sector.size);
		device.head += sector.size;
		if(device.oddfield)
			{
			memset(device.buffer + device.head, BKR_FILLER, device.bytes_per_line);
			device.head += device.bytes_per_line;
			}
		device.oddfield ^= 1;

		if(device.head == device.size)
			device.head = 0;

		sector.block = sector.footer + device.bytes_per_line;
		}
	else if(sector.block == sector.footer)
		sector.block = sector.aux + device.bytes_per_line;

	location = --sector.block;
	for(count = block.size; count; )
		*(location -= device.bytes_per_line) = block.buffer[--count];

	if(sector.block == sector.aux + 1)
		return(KEY_BLOCK(-1));
	return(0);
}


/*
 * bkr_get_sector()
 *
 * Finds the next sector in the data stream and updates the device tail
 * appropriately.  The search is done by computing a correlation
 * coefficient for the data stream against a copy of the key block.  The
 * correlation test is a count of the total number of bits that are in
 * error (thus really an anti-correlation test).  If this number drops
 * below a threshold then we've found the key block and from its location
 * the start of the sector can be identified.  The value returned is the
 * number of bytes in the data stream that were skipped before the header
 * block was found.
 */

static int bkr_get_sector(f_flags_t f_flags, jiffies_t bailout)
{
	int  result;
	unsigned int  i, j;
	unsigned int  anti_corr;
	unsigned int  skipped = -1;

	result = bkr_device_read(SECTOR_KEY_OFFSET, f_flags, bailout);
	if(result < 0)
		return(result);

	device.tail += SECTOR_KEY_OFFSET - 1;
	if(device.tail >= device.size)
		device.tail -= device.size;

	do
		{
		if(++device.tail == device.size)
			device.tail = 0;
		skipped++;

		if(skipped > 0)
			if(anti_corr < best_nonmatch)
				best_nonmatch = anti_corr;

		result = bkr_device_read(KEY_LENGTH*device.bytes_per_line, f_flags, bailout);
		if(result < 0)
			break;

		anti_corr = 0;
		j = KEY_LENGTH;
		i = device.tail + KEY_LENGTH*device.bytes_per_line;
		if(i >= device.size)
			{
			i -= device.size;
			while(i >= device.bytes_per_line)
				anti_corr += weight[device.buffer[i -= device.bytes_per_line] ^ key[--j]];
			i += device.size;
			}
		while(j)
			anti_corr += weight[device.buffer[i -= device.bytes_per_line] ^ key[--j]];
		}
	while((anti_corr > CORR_THRESHOLD(KEY_LENGTH)) && (jiffies < bailout));

	device.tail -= SECTOR_KEY_OFFSET;
	if(device.tail >= device.size)
		device.tail += device.size;

	if(result < 0)
		return(result);

	result = bkr_device_read(sector.footer_offset, f_flags, bailout);
	if(result < 0)
		return(result);

	if(device.tail + sector.footer_offset >= device.size)
		{
		i = device.size - device.tail;
		memcpy(sector.buffer, device.buffer + device.tail, i);
		memcpy(sector.buffer + i, device.buffer, sector.footer_offset - i);
		device.tail += sector.footer_offset - device.size;
		}
	else
		{
		memcpy(sector.buffer, device.buffer + device.tail, sector.footer_offset);
		device.tail += sector.footer_offset;
		}

	if(skipped > sector.footer_length+device.bytes_per_line)
		errors.sector++;

	if(anti_corr > worst_match)
		worst_match = anti_corr;
	if(skipped < least_skipped)
		least_skipped = skipped;
	if(skipped > most_skipped)
		most_skipped = skipped;

	return(skipped);
}
