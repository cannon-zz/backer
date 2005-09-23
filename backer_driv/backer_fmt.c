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

#define  TRUNC_CAPACITY  (*(block.buffer + block.size - 1))     /* capacity byte */

#define  BLOCKS_PER_SECOND  (device.bytes_per_line*2*(BKR_VIDEOMODE(device.mode)==BKR_PAL?50:60))
#define  SECTOR_KEY_OFFSET  (sector.header_length+(block.ecc_length+sizeof(header_t))*device.bytes_per_line)


/*
 * Function prototypes
 */

int           bkr_block_read_fmt(unsigned long);
void          bkr_block_write_fmt(unsigned long);
int           bkr_block_read_raw(unsigned long bailout);
void          bkr_block_write_raw(unsigned long bailout);
void          bkr_block_randomize(__u32);
unsigned int  bkr_sector_blocks_remaining(void);
void          bkr_sector_read(unsigned long);
unsigned int  bkr_sector_write(unsigned long);
unsigned int  bkr_find_sector(unsigned long);


/*
 * Global Data
 */

struct bkr_format  format[] = BKR_FORMATS;
unsigned char  key[] = KEY_INITIALIZER;         /* white noise;  use as much as required */
unsigned char  bits[] = BITS_INITIALIZER;       /* bits[x1 ^ x2] == anti-correlation weight */


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
	sector.footer_length = format[BKR_MODE_TO_FORMAT(device.mode)].footer_length;
	sector.footer_offset = sector.size - sector.footer_length;
	sector.aux_length = format[BKR_MODE_TO_FORMAT(device.mode)].aux_length;

	switch(BKR_FORMAT(device.mode))
		{
		case BKR_FMT:
		block.read = bkr_block_read_fmt;
		block.write = bkr_block_write_fmt;
		block.size = (sector.size - sector.header_length - sector.footer_length -
		              sector.aux_length) / device.bytes_per_line / 2;
		block.ecc_length = format[BKR_MODE_TO_FORMAT(device.mode)].ecc_length;
		block.header = (header_t *) block.ecc_length;
		block.start = (unsigned char *) block.ecc_length + sizeof(header_t);
		break;

		case BKR_RAW:
		/*
		 * For debugging.  Uncommenting this makes the DMA buffer a little
		 * bit smaller than an integer number of video frames so that a
		 * buffer overrun can be seen as a shift in the pattern generated
		 * by bkrtest in raw mode.
		 */
		device.size -= (sector.size / device.bytes_per_line) / 4 * device.bytes_per_line;

		block.read = bkr_block_read_raw;
		block.write = bkr_block_write_raw;
		block.size = sector.size / device.bytes_per_line;
		block.ecc_length = 0;
		block.header = 0;
		block.start = 0;
		break;
		}

	sector.aux_offset = sector.header_length + block.size * device.bytes_per_line;

	if(sector.aux != NULL)
		free(sector.aux);
	if(block.buffer != NULL)
		free(block.buffer);
	sector.aux = (unsigned char *) malloc(sector.aux_length);
	block.buffer = (unsigned char *) malloc(block.size);

	if((sector.aux == NULL) || (block.buffer == NULL))
		return(-ENOMEM);

	block.header = (header_t *) (block.buffer + (unsigned int) block.header);
	block.start = block.buffer + (unsigned int) block.start;

	reed_solomon_init(block.size, block.size - block.ecc_length, &block.rs_format);

	return(0);
}


/*
 * bkr_format_reset()
 *
 * Reset the block and sector formating layers.  Note that this also resets
 * the head and tail offsets for the device buffer since these need to have
 * certain relationships with respect to one another in order to force the
 * sector layer to update them upon the first read/write operation.  Note
 * that the device begins by generating an odd field of video but since the
 * first thing the sector layer will do is toggle the oddfield flag we need
 * to set it to false at this point.
 */

void bkr_format_reset(void)
{
	memset(sector.aux, BKR_FILLER, sector.aux_length);

	if((device.direction == O_WRONLY) && (BKR_FORMAT(device.mode) != BKR_RAW))
		{
		device.head = device.size - sector.size;
		device.tail = 1;
		}
	else
		{
		device.head = 0;
		device.tail = 0;
		}
	device.oddfield = 0;

	block.end = block.start;
	block.offset = block.end;
	block.sequence = 0;
	*block.header = DATA_BLOCK | BLOCK_SEQ(block.sequence);

	sector.block_offset = sector.aux_offset;

	errors.symbol = 0;
	errors.block = 0;
	errors.sector = 0;
	errors.overrun = 0;

	return;
}


/*
 * space_in_buffer(), bytes_in_buffer()
 *
 * Return the space and bytes available in the DMA buffer.  Note that
 * space_in_buffer() + bytes_in_buffer() == device.size - 1.
 *
 * These functions are not, strictly speaking, part of the "block" and
 * "sector" layers but should really be associated with the "device" layer.
 * However, since the definition of these functions is universal to all
 * device layers due to the expectations of the code in the formating layers
 * it is reasonable to include them here to save the effort of duplicating
 * them in each implementation.
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
 */

/*
 * bkr_write_bor() bkr_write_eor()
 *
 * Generate Begining-Of-Record and End-Of-Record marks
 */

void bkr_write_bor(unsigned long bailout)
{
	unsigned int  i;

	block.end = block.buffer + block.size;

	*block.header &= ~BLOCK_TYPE(-1);
	*block.header |= BOR_BLOCK;

	for(i = BLOCKS_PER_SECOND * BOR_LENGTH; i && (jiffies < bailout); i--)
		{
		memset(block.offset, BKR_FILLER, block.end - block.offset);
		block.offset = block.end;
		bkr_block_write_fmt(bailout);
		}

	*block.header &= ~BLOCK_TYPE(-1);
	*block.header |= DATA_BLOCK;

	return;
}

void bkr_write_eor(unsigned long bailout)
{
	unsigned int  i;

	if(block.offset != block.start)
		bkr_block_write_fmt(bailout);

	*block.header &= ~BLOCK_TYPE(-1);
	*block.header |= EOR_BLOCK;

	for(i = bkr_sector_blocks_remaining() + BLOCKS_PER_SECOND * EOR_LENGTH;
	    i && (jiffies < bailout); i--)
		{
		memset(block.offset, BKR_FILLER, block.end - block.offset);
		block.offset = block.end;
		bkr_block_write_fmt(bailout);
		}

	bkr_device_flush(bailout);

	return;
}

/*
 * bkr_block_read_fmt()
 *
 * Read and decode one block of formated data.  If a BOR block is found then
 * the block sequence counter is reset and the block skipped.  If a non-BOR
 * block is found but it is out of sequence then it too is skipped.  Otherwise
 * the block capacity is set appropriately and the block type returned to the
 * calling function.
 */

int bkr_block_read_fmt(unsigned long bailout)
{
	unsigned int  tmp;

	do
		{
		bkr_sector_read(bailout);
		tmp = reed_solomon_decode(block.buffer, NULL, 0, &block.rs_format);
		if((int) tmp < 0)
			errors.block++;
		if(tmp > errors.symbol)
			errors.symbol = tmp;
		if(BLOCK_TYPE(*block.header) == BOR_BLOCK)
			{
			block.sequence = BLOCK_SEQ(*block.header);
			least_skipped = 1000000;
			most_skipped = 0;
			errors.symbol = 0;
			}
		else if(BLOCK_SEQ(*block.header - block.sequence) == 1)
			{
			block.sequence = BLOCK_SEQ(*block.header);
			break;
			}
		}
	while(jiffies < bailout);

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
		block.end += TRUNC_CAPACITY;
	else
		block.end += block.size;

	return(DATA_BLOCK);
}

/*
 * bkr_block_write_fmt()
 *
 * Encode and write one block of formated data.  After writing, the block
 * buffer is prepared for the next block:  the header is set up appropriately,
 * and the sector key is inserted if needed.
 */

void bkr_block_write_fmt(unsigned long bailout)
{
	unsigned int  result;

	if(block.offset < block.end)
		{
		*block.header |= TRUNCATE_BLOCK(-1);
		TRUNC_CAPACITY = block.offset - block.buffer;
		memset(block.offset, BKR_FILLER, block.end - block.offset - 1);
		}

	bkr_block_randomize(block.sequence);
	reed_solomon_encode(block.buffer, &block.rs_format);

	result = bkr_sector_write(bailout);

	*block.header &= BLOCK_TYPE(-1);
	*block.header |= block.sequence = BLOCK_SEQ(block.sequence + 1);

	block.offset = block.start;
	if(result == KEY_BLOCK(-1))
		{
		*block.header |= KEY_BLOCK(-1);
		memcpy(block.offset, key, KEY_LENGTH);
		block.offset += KEY_LENGTH;
		}

	return;
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

void  bkr_block_randomize(__u32 num)
{
	unsigned int  i, last;

	if(BLOCK_TYPE(*block.header) != DATA_BLOCK)
		return;

	if(KEY_BLOCK(*block.header))
		last = (block.ecc_length + sizeof(header_t) + KEY_LENGTH) >> 1;
	else
		last = (block.ecc_length + sizeof(header_t)) >> 1;

	for(i = block.size >> 1; i > last; )
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

int bkr_block_read_raw(unsigned long bailout)
{
	bkr_device_read(block.size, bailout);

	block.end = block.buffer + block.size;
	block.offset = block.buffer;

	for(; block.offset != block.end; block.offset++)
		{
		*block.offset = device.buffer[device.tail];
		if(++device.tail >= device.size)
			device.tail = 0;
		}

	block.offset = block.buffer;

	return(DATA_BLOCK);
}

void bkr_block_write_raw(unsigned long bailout)
{
	bkr_device_write(block.size, bailout);

	for(block.offset = block.buffer; block.offset != block.end; block.offset++)
		{
		device.buffer[device.head] = *block.offset;
		if(++device.head >= device.size)
			device.head = 0;
		}

	block.end = block.buffer + block.size;
	block.offset = block.buffer;
}


/*
 * ================================================================================================
 *
 *                                     SECTOR-LEVEL FORMATING
 *
 * ================================================================================================
 */

/*
 * bkr_sector_blocks_remaining()
 *
 * Returns the number of blocks required to fill the rest of the current
 * sector.
 */

unsigned int bkr_sector_blocks_remaining(void)
{
	if(sector.block_offset >= sector.footer_offset)
		return(sector.block_offset - sector.footer_offset + device.bytes_per_line);
	return(sector.block_offset - sector.aux_offset);
}


/*
 * bkr_sector_read()
 *
 * Retrieves the next block from the current sector.  If we're at the end of
 * the current sector then we move to the next one.
 */

void  bkr_sector_read(unsigned long bailout)
{
	int  count, offset;

	if(sector.block_offset == sector.aux_offset)
		{
		if(bkr_find_sector(bailout) > sector.footer_length)
			errors.sector++;

		bkr_device_read(sector.size + device.bytes_per_line, bailout);

		if((offset = device.tail + sector.aux_offset) >= device.size)
			offset -= device.size;
		if(offset + sector.aux_length > device.size)
			{
			memcpy(sector.aux, device.buffer + offset, device.size - offset);
			offset += sector.aux_length - device.size;
			memcpy(sector.aux + sector.aux_length - offset, device.buffer, offset);
			}
		else
			memcpy(sector.aux, device.buffer + offset, sector.aux_length);

		sector.block_offset = sector.footer_offset + device.bytes_per_line;
		}
	else if(sector.block_offset == sector.footer_offset)
		sector.block_offset = sector.aux_offset + device.bytes_per_line;

	if((offset = device.tail + --sector.block_offset) >= device.size)
		offset -= device.size;

	for(count = block.size; count;)
		{
		if((offset -= device.bytes_per_line) >= device.size)
			offset += device.size;
		block.buffer[--count] = device.buffer[offset];
		}

	return;
}


/*
 * bkr_sector_write()
 *
 * Writes a block into the current sector.  If the current sector is full
 * then we move to the next one.  The return value indicates whether or not
 * not the next block must be a key block.
 */

unsigned int bkr_sector_write(unsigned long bailout)
{
	int  count, offset;

	if(sector.block_offset == sector.aux_offset)
		{
		if((device.head += sector.size) >= device.size)
			device.head -= device.size;

		if(device.oddfield)
			{
			memset(device.buffer + device.head, BKR_FILLER, device.bytes_per_line);
			device.head += device.bytes_per_line;
			}
		device.oddfield ^= 1;

		bkr_device_write(sector.size+device.bytes_per_line, bailout);

		memset(device.buffer + device.head, BKR_FILLER, sector.header_length);
		memset(device.buffer + device.head + sector.footer_offset, BKR_FILLER, sector.footer_length);
		memcpy(device.buffer + device.head + sector.aux_offset, sector.aux, sector.aux_length);

		sector.block_offset = sector.footer_offset + device.bytes_per_line;
		}
	else if(sector.block_offset == sector.footer_offset)
		sector.block_offset = sector.aux_offset + device.bytes_per_line;

	offset = device.head + --sector.block_offset;

	for(count = block.size; count; )
		device.buffer[offset -= device.bytes_per_line] = block.buffer[--count];

	if(sector.block_offset == sector.aux_offset+1)
		return(KEY_BLOCK(-1));
	return(~KEY_BLOCK(-1));
}


/*
 * bkr_find_sector()
 *
 * Finds the next sector in the data stream and updates the device tail
 * appropriately.  The search is done by computing a correlation
 * coefficient for the data stream against a copy of the key block.  The
 * correlation test is a count of the total number of bits that are in error
 * (thus really an anti-correlation test).  If this number drops below a
 * threshold then we've found the key block and from its location the start
 * of the sector can be identified.  The value returned is the number of
 * bytes in the data stream that were skipped before the header block was
 * found.
 */

unsigned int bkr_find_sector(unsigned long bailout)
{
	unsigned int  i, j, bad_bits;
	unsigned int  skipped = -1;

	device.tail += sector.footer_offset + SECTOR_KEY_OFFSET - 1;
	if(device.tail >= device.size)
		device.tail -= device.size;
	do
		{
		if(++device.tail == device.size)
			device.tail = 0;
		skipped++;

		if(skipped > 0)
			if(bad_bits < best_nonmatch)
				best_nonmatch = bad_bits;

		bkr_device_read(KEY_LENGTH*device.bytes_per_line, bailout);

		bad_bits = 0;
		j = KEY_LENGTH;
		i = device.tail + KEY_LENGTH*device.bytes_per_line;
		if(i >= device.size)
			{
			i -= device.size;
			while(i >= device.bytes_per_line)
				bad_bits += bits[device.buffer[i -= device.bytes_per_line] ^ key[--j]];
			i += device.size;
			}
		while(j)
			bad_bits += bits[device.buffer[i -= device.bytes_per_line] ^ key[--j]];
		}
	while((bad_bits > CORR_THRESHOLD(KEY_LENGTH)) && (jiffies < bailout));

	device.tail -= SECTOR_KEY_OFFSET;
	if(device.tail >= device.size)
		device.tail += device.size;

	if(bad_bits > worst_match)
		worst_match = bad_bits;
	if(skipped < least_skipped)
		least_skipped = skipped;
	if(skipped > most_skipped)
		most_skipped = skipped;

	return(skipped);
}
