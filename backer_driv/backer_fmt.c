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
 */


#ifdef __KERNEL__

#include <linux/errno.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/types.h>
#define  malloc(x)    kmalloc((x), GFP_KERNEL)
#define  free(x)      kfree(x)

#else

#include <errno.h>
#include <malloc.h>
#include <string.h>
#include <sys/types.h>

#endif /* __KERNEL__ */

/* Use linux_compat.h if Linux headers aren't available */

#if 1
#include <asm/byteorder.h>
#include <asm/unaligned.h>
#else
#include "linux_compat.h"
#endif

#include "rs.h"
#include "backer.h"
#include "backer_device.h"
#include "backer_fmt.h"


/*
 * ========================================================================
 *
 *                              PARAMETERS
 *
 * ========================================================================
 */

#define  MIN_MATCH_FRACTION  35         /* percent */


/*
 * ========================================================================
 *
 *                              GLOBAL DATA
 *
 * ========================================================================
 */

static bkr_format_info_t format_info[] = BKR_FORMAT_INFO_INITIALIZER;

static unsigned char  key[] =           /* sector key sequence */
	{ 0xd4, 0x7c, 0xb1, 0x93, 0x66, 0x65, 0x6a, 0xb5,
	  0x63, 0xe4, 0x56, 0x59, 0x6c, 0xbe, 0xc5, 0xca,
	  0xf4, 0x9c, 0xa3, 0xac, 0x6d, 0xb3, 0xd2, 0x7e,
	  0x74, 0xa6, 0xe1, 0xa9, 0x5c, 0x9a, 0x4b, 0x5d };

static u_int16_t  gcr_encode[] =        /* GCR modulation table */
	{ 0x089, 0x08a, 0x08b, 0x08c, 0x08d, 0x08e, 0x091, 0x092,
	  0x093, 0x094, 0x095, 0x096, 0x099, 0x09a, 0x09b, 0x09c,
	  0x09d, 0x09e, 0x0a2, 0x0a3, 0x0a4, 0x0a5, 0x0a6, 0x0a9,
	  0x0aa, 0x0ab, 0x0ac, 0x0ad, 0x0ae, 0x0b1, 0x0b2, 0x0b3,
	  0x0b4, 0x0b5, 0x0b6, 0x0b9, 0x0ba, 0x0bb, 0x0bc, 0x0bd,
	  0x0be, 0x0c2, 0x0c3, 0x0c4, 0x0c5, 0x0c6, 0x0c9, 0x0ca,
	  0x0cb, 0x0cc, 0x0cd, 0x0ce, 0x0d1, 0x0d2, 0x0d3, 0x0d4,
	  0x0d5, 0x0d6, 0x0d9, 0x0da, 0x0db, 0x0dc, 0x0dd, 0x0de,
	  0x0e1, 0x0e2, 0x0e3, 0x0e4, 0x0e5, 0x0e6, 0x0e9, 0x0ea,
	  0x0eb, 0x0ec, 0x0ed, 0x0ee, 0x0f1, 0x0f2, 0x0f3, 0x0f4,
	  0x0f5, 0x0f6, 0x0f9, 0x0fa, 0x0fb, 0x0fc, 0x0fd, 0x109,
	  0x10a, 0x10b, 0x10c, 0x10d, 0x10e, 0x111, 0x112, 0x113,
	  0x114, 0x115, 0x116, 0x119, 0x11a, 0x11b, 0x11c, 0x11d,
	  0x11e, 0x121, 0x122, 0x123, 0x124, 0x125, 0x126, 0x129,
	  0x12a, 0x12b, 0x12c, 0x12d, 0x12e, 0x131, 0x132, 0x133,
	  0x134, 0x135, 0x136, 0x139, 0x13a, 0x13b, 0x13c, 0x13d,
	  0x13e, 0x142, 0x143, 0x144, 0x145, 0x146, 0x149, 0x14a,
	  0x14b, 0x14c, 0x14d, 0x14e, 0x151, 0x152, 0x153, 0x154,
	  0x155, 0x156, 0x159, 0x15a, 0x15b, 0x15c, 0x15d, 0x15e,
	  0x161, 0x162, 0x163, 0x164, 0x165, 0x166, 0x169, 0x16a,
	  0x16b, 0x16c, 0x16d, 0x16e, 0x171, 0x172, 0x173, 0x174,
	  0x175, 0x176, 0x179, 0x17a, 0x17b, 0x17c, 0x17d, 0x17e,
	  0x184, 0x185, 0x186, 0x189, 0x18a, 0x18b, 0x18c, 0x18d,
	  0x18e, 0x191, 0x192, 0x193, 0x194, 0x195, 0x196, 0x199,
	  0x19a, 0x19b, 0x19c, 0x19d, 0x19e, 0x1a1, 0x1a2, 0x1a3,
	  0x1a4, 0x1a5, 0x1a6, 0x1a9, 0x1aa, 0x1ab, 0x1ac, 0x1ad,
	  0x1ae, 0x1b1, 0x1b2, 0x1b3, 0x1b4, 0x1b5, 0x1b6, 0x1b9,
	  0x1ba, 0x1bb, 0x1bc, 0x1bd, 0x1be, 0x1c2, 0x1c3, 0x1c4,
	  0x1c5, 0x1c6, 0x1c9, 0x1ca, 0x1cb, 0x1cc, 0x1cd, 0x1ce,
	  0x1d1, 0x1d2, 0x1d3, 0x1d4, 0x1d5, 0x1d6, 0x1d9, 0x1da,
	  0x1db, 0x1dc, 0x1dd, 0x1de, 0x1e1, 0x1e2, 0x1e3, 0x1e4,
	  0x1e5, 0x1e6, 0x1e9, 0x1ea, 0x1eb, 0x1ec, 0x1ed, 0x1ee };

static unsigned char gcr_decode[512];   /* GCR demodulation table */


/*
 * ========================================================================
 *
 *                                MACROS
 *
 * ========================================================================
 */

static inline int sectors_per_second(bkr_device_t *device)
{
	return(BKR_VIDEOMODE(device->mode) == BKR_PAL ? 50 : 60);
}

static inline bkr_sector_header_t get_sector_header(bkr_sector_t *sector)
{
	u_int32_t  tmp;

	tmp = get_unaligned((u_int32_t *) (sector->buffer + sector->fmt.data_size - sizeof(bkr_sector_header_t)));
	tmp = __le32_to_cpu(tmp);

	return(*(bkr_sector_header_t *) &tmp);
}

static inline void put_sector_header(bkr_sector_t *sector)
{
	u_int32_t tmp;

	tmp = *(u_int32_t *) &sector->header;
	tmp = __cpu_to_le32(tmp);

	put_unaligned(tmp, (u_int32_t *) (sector->buffer + sector->fmt.data_size - sizeof(bkr_sector_header_t)));
}

static inline unsigned int hash_sector_length(unsigned int  length)
{
	return(length + (length / 15) + 1);
}

static inline unsigned int unhash_sector_length(unsigned int  length)
{
	return(length - (length / 16) - 1);
}

#define  HIGH_USED(x)   (x)[sector->fmt.data_size - sizeof(bkr_sector_header_t) - 1]
#define  GCR_MASK       ((u_int16_t) 0x01ff)

/* use inline assembly if possible */
#if 1
#define  rolw(x, n)  asm("rolw %2, %0" : "=r" (x) : "0" (x), "c" (n))
#else
#define  rolw(x, n)  do { x = __swab16(x) >> (8 - n) } while(0)
#endif


/*
 * ========================================================================
 *
 *                             FORMATED I/O
 *
 * ========================================================================
 */

/*
 * bkr_correlate() bkr_find_sector()
 *
 * bkr_correlate() computes the correlation coefficient (0 to 100) of the
 * current location in the data stream against the sector key pattern.
 *
 * bkr_find_sector() uses bkr_correlate() to scan the data stream until a
 * sector is found.  On success, the buffer tail is left pointing to the
 * start of the sector and the number of bytes skipped is returned
 * otherwise the return value is negative.
 */

static inline unsigned int bkr_correlate(bkr_device_t *device, bkr_sector_t *sector)
{
	int  i, j;
	unsigned int  percentage = 0;

	i = (device->tail +
	     sector->fmt.leader +
	     sector->fmt.key_length*sector->fmt.key_interval - 1) &
	    BKR_OFFSET_MASK;
	j = sector->fmt.key_length;
	while(--j >= 0)
		{
		if(device->buffer[i] == key[j])
			percentage++;
		i = (i - sector->fmt.key_interval) & BKR_OFFSET_MASK;
		}

	percentage = 100 * percentage / sector->fmt.key_length;

	return(percentage);
}

static inline int bkr_find_sector(bkr_device_t *device, bkr_sector_t *sector)
{
	int  result, skipped;

	for(skipped = 0; 1; skipped++, device->tail = (device->tail + 1) & BKR_OFFSET_MASK)
		{
		result = device->ops->read(device, sector->fmt.video_size);
		if(result < 0)
			return(result);

		result = bkr_correlate(device, sector);
		if(result >= MIN_MATCH_FRACTION)
			break;
		if(result > sector->health.best_nonkey)
			sector->health.best_nonkey = result;
		}

	if(result < sector->health.worst_key)
		sector->health.worst_key = result;
	if(skipped < sector->health.least_skipped)
		sector->health.least_skipped = skipped;
	if(skipped > sector->health.most_skipped)
		sector->health.most_skipped = skipped;
	if(skipped > sector->fmt.trailer+device->bytes_per_line)
		sector->errors.frame++;

	return(skipped);
}


/*
 * bkr_decode_sector()
 *
 * Decodes a sector from the current position in the data stream.
 */

static void bkr_decode_sector(bkr_device_t *device, bkr_sector_t *sector)
{
	int  i, j;
	u_int16_t  state, tmp;
	unsigned int  in_off, out_off;
	signed char  shift;

	/*
	 * Skip over the leader.
	 */

	device->tail = (device->tail + sector->fmt.leader) & BKR_OFFSET_MASK;

	/*
	 * Remove the key sequence.
	 */

	out_off = (device->tail + sector->fmt.key_length*sector->fmt.key_interval - 1) & BKR_OFFSET_MASK;
	in_off = (out_off - 1) & BKR_OFFSET_MASK;
	for(j = sector->fmt.key_length; j; j--)
		{
		for(i = sector->fmt.key_interval-1; i; i--)
			{
			device->buffer[out_off] = device->buffer[in_off];
			out_off = (out_off - 1) & BKR_OFFSET_MASK;
			in_off = (in_off - 1) & BKR_OFFSET_MASK;
			}
		in_off = (in_off - 1) & BKR_OFFSET_MASK;
		}
	device->tail = (device->tail + sector->fmt.key_length) & BKR_OFFSET_MASK;

	/*
	 * Demodulate to the data stream.
	 */

	if(sector->fmt.modulation == GCR)
		{
		in_off = out_off = device->tail;
		state = 0;
		shift = 1;
		while(1)
			{
			tmp  = device->buffer[in_off];
			in_off = (in_off + 1) & BKR_OFFSET_MASK;
			tmp |= device->buffer[in_off] << 8;
			rolw(tmp, shift);
			if(state & 1)
				tmp = ~tmp;
			state ^= tmp;
			device->buffer[out_off] = gcr_decode[tmp & GCR_MASK];
			out_off = (out_off + 1) & BKR_OFFSET_MASK;
			if(++shift > 8)
				{
				if(out_off - device->tail >= sector->fmt.buffer_size)
					break;
				in_off = (in_off + 1) & BKR_OFFSET_MASK;
				shift = 1;
				}
			}
		}

	/*
	 * Deinterleave the sector from the I/O buffer into the sector buffer.
	 */

	for(i = 0; i < sector->rs_format.k; i += 1 - sector->fmt.data_size)
		for(; i < sector->fmt.data_size; i += sector->rs_format.k)
			{
			sector->buffer[i] = device->buffer[device->tail];
			device->tail = (device->tail + 1) & BKR_OFFSET_MASK;
			}
	for(i = sector->fmt.data_size; i < sector->fmt.data_size + sector->rs_format.parity; i += 1 - sector->fmt.parity_size)
		for(; i < sector->fmt.buffer_size; i += sector->rs_format.parity)
			{
			sector->buffer[i] = device->buffer[device->tail];
			device->tail = (device->tail + 1) & BKR_OFFSET_MASK;
			}
	device->tail = (device->tail + sector->fmt.modulation_pad - sector->fmt.key_length) & BKR_OFFSET_MASK;
}


/*
 * bkr_encode_sector()
 *
 * Encode a sector into the data stream.  This involves adding the header,
 * the footer, applying the appropriate bit modulation, mixing with the key
 * sequence and writing the whole thing to the I/O buffer.
 */

static int bkr_encode_sector(bkr_device_t *device, bkr_sector_t *sector)
{
	int  i, j;
	u_int16_t  state, tmp;
	unsigned char  *in_pos, *out_pos;
	unsigned char  *data, *parity;
	signed char  shift;

	/*
	 * Computer Reed-Solomon parity symbols.
	 */

	data = sector->buffer;
	parity = sector->buffer + sector->fmt.data_size;
	for(i = 0; i < sector->fmt.interleave; data += sector->rs_format.k, parity += sector->rs_format.parity, i++)
		reed_solomon_encode(parity, data, &sector->rs_format);

	/*
	 * Write the leader to the I/O buffer
	 */

	memset(device->buffer + device->head, BKR_LEADER, sector->fmt.leader);
	device->head += sector->fmt.leader;

	/*
	 * Interleave the data into the I/O buffer
	 */

	out_pos = &device->buffer[device->head + sector->fmt.modulation_pad];
	for(i = 0; i < sector->rs_format.k; i += 1 - sector->fmt.data_size)
		for(; i < sector->fmt.data_size; i += sector->rs_format.k)
			*(out_pos++) = sector->buffer[i];
	for(i = sector->fmt.data_size; i < sector->fmt.data_size + sector->rs_format.parity; i += 1 - sector->fmt.parity_size)
		for(; i < sector->fmt.buffer_size; i += sector->rs_format.parity)
			*(out_pos++) = sector->buffer[i];

	/*
	 * Apply the appropriate modulation
	 */

	if(sector->fmt.modulation == GCR)
		{
		in_pos = &device->buffer[device->head + sector->fmt.modulation_pad];
		out_pos = &device->buffer[device->head + sector->fmt.key_length];
		state = 0;
		tmp = 0;
		shift = 7;
		while(1)
			{
			if(state &= 1)
				state = GCR_MASK;
			state ^= gcr_encode[*(in_pos++)];
			tmp |= state << shift;
			if(--shift >= 0)
				{
				tmp = __swab16(tmp);
				*(out_pos++) = tmp; /* write low byte */
				tmp &= (u_int16_t) 0xff00;
				}
			else
				{
				put_unaligned(__cpu_to_be16(tmp), (u_int16_t *) out_pos);
				out_pos += sizeof(u_int16_t);
				if(out_pos >= in_pos)
					break;
				tmp = 0;
				shift = 7;
				}
			}
		}

	/*
	 * Insert the sector key sequence
	 */

	in_pos = &device->buffer[device->head + sector->fmt.key_length];
	out_pos = &device->buffer[device->head];
	for(j = 0; j < sector->fmt.key_length; *(out_pos++) = key[j++])
		for(i = sector->fmt.key_interval-1; i; i--)
			*(out_pos++) = *(in_pos++);
	device->head += sector->fmt.active_size;

	/*
	 * Write the trailer to the I/O buffer
	 */

	i = sector->fmt.trailer;
	if(sector->oddfield && (BKR_VIDEOMODE(device->mode) == BKR_NTSC))
		i += device->bytes_per_line;
	memset(device->buffer + device->head, BKR_TRAILER, i);
	device->head += i;
	if(device->head == device->size)
		device->head = 0;
	sector->oddfield ^= 1;

	return(0);
}


/*
 * bkr_sector_randomize()
 *
 * (De-)Randomize a buffer using the supplied seed.  The count is rounded
 * up to the nearest multiple of 4 so make sure the buffer is sized
 * accordingly!  This function is its own inverse.
 *
 * For the random number generator used see:  Knuth, D. E. 1981,
 * Semi-Numerical Algorithms, 2nd ed., vol 2 of The Art of Computer
 * Programing.
 */

static void bkr_sector_randomize(u_int32_t *location, int count, u_int32_t seed)
{
	int  index;
	u_int32_t  history[4];

	for(index = 3; index >= 0; index--)
		{
		seed = 1664525 * seed + 1013904223;
		history[index] = seed;
		}

	for(count = (count-1) >> 2; count >= 0; count--)
		{
		seed = 1664525 * seed + 1013904223;
		index = seed >> 30;
		location[count] ^= __cpu_to_le32(history[index]);
		history[index] = seed;
		}
}


/*
 * bkr_sector_read_data()
 *
 * Retrieves the next sector from the I/O buffer.  The algorithm is to loop
 * until we either find an acceptable sector (it's a type we understand and
 * is in the correct order) or we encounter an error or EOR.  If we find a
 * sector we like, we set it up for a read out and return 0;  otherwise we
 * return < 0 on error or > 0 on EOR.  On error, a retry can be attempted
 * by simply re-calling this function.
 *
 * We tolerate junk sectors for the first little while in order to
 * transparently skip over the noise found at the beginning of recordings.
 * This is done by silently skipping over all errors (except -EAGAIN) until
 * we find a valid DATA sector after which full error reporting is
 * restored.  When reporting errors, -ENODATA is returned exactly once for
 * each irrecoverable sector i.e. for each uncorrectable sector and for
 * each sector overrun.
 */

static int bkr_sector_read_data(bkr_device_t *device, bkr_sector_t *sector)
{
	int  result;
	unsigned int  i;
	unsigned char  *data, *parity;

	sector->offset = sector->end;

	if(sector->op_count)
		{
		if(--sector->op_count > 0)
			return(-ENODATA);
		sector->offset = sector->buffer;
		return(0);
		}

	while(1)
		{
		/*
		 * Get the next sector.
		 */

		next:
		result = bkr_find_sector(device, sector);
		if(result < 0)
			return(result);
		bkr_decode_sector(device, sector);

		/*
		 * Do error correction.
		 */

		data = sector->buffer;
		parity = sector->buffer + sector->fmt.data_size;
		for(i = 0; i < sector->fmt.interleave; data += sector->rs_format.k, parity += sector->rs_format.parity, i++)
			{
			result = reed_solomon_decode(parity, data, NULL, 0, &sector->rs_format);
			if(result < 0)
				{
				if(!sector->found_data)
					{
					/* FIXME: potential infinite loop:
					 * if only corrupt sectors are
					 * found and the device buffer
					 * never empties then we get stuck.
					 */
					sector->need_sequence_reset = 1;
					goto next;
					}
				sector->header.number++;
				sector->errors.block++;
				return(-ENODATA);
				}
			sector->health.total_errors += result;
			if((unsigned int) result > sector->errors.symbol)
				sector->errors.symbol = result;
			if((unsigned int) result > sector->errors.recent_symbol)
				sector->errors.recent_symbol = result;
			}

		/*
		 * Check sector order.
		 */

		result = get_sector_header(sector).number - sector->header.number - 1;
		if((result == 0) || sector->need_sequence_reset)
			sector->need_sequence_reset = 0;
		else if(result > 0)
			{
			sector->errors.overrun++;
			sector->op_count = result;
			}
		else
			{
			sector->errors.underflow += sector->underflow_detect;
			sector->underflow_detect = 0;
			continue;
			}
		sector->underflow_detect = 1;

		/*
		 * Extract header and process the sector type.
		 */

		sector->header = get_sector_header(sector);
		switch((bkr_sector_type_t) sector->header.type)
			{
			case DATA_SECTOR:
			sector->found_data = 1;
			bkr_sector_randomize((unsigned int *) sector->buffer, sector->fmt.data_size - sizeof(bkr_sector_header_t), sector->header.number);
			if(sector->header.low_used)
				{
				i = ((unsigned int) HIGH_USED(sector->buffer) << 4) + sector->header.low_used;
				sector->end = sector->buffer + unhash_sector_length(i);
				}
			else
				sector->end = sector->buffer + sector->fmt.data_size - sizeof(bkr_sector_header_t);
			if(sector->op_count == 0)
				{
				sector->offset = sector->buffer;
				return(0);
				}
			sector->offset = sector->end;
			return(-ENODATA);

			case EOR_SECTOR:
			sector->op_count = 0;
			return(1);

			case BOR_SECTOR:
			sector->op_count = 0;
			sector->errors = BKR_ERRORS_INITIALIZER;
			sector->health = BKR_HEALTH_INITIALIZER;
			break;
			}
		}
}


/*
 * bkr_sector_write_data()
 *
 * Writes the current sector to the I/O buffer and resets for the next one.
 * Returns 0 on success, < 0 on error in which case a retry can be
 * attempted by simply re-calling this function.
 */

static int bkr_sector_write_data(bkr_device_t *device, bkr_sector_t *sector)
{
	int  result;

	/*
	 * Set the sector length as needed.
	 */

	if(sector->offset < sector->end)
		{
		result = hash_sector_length(sector->offset - sector->buffer);
		sector->header.low_used = result;
		HIGH_USED(sector->buffer) = result >> 4;
		sector->offset = sector->end;
		}

	/*
	 * Make room in the I/O buffer.
	 */

	result = device->ops->write(device, sector->fmt.video_size+device->bytes_per_line);
	if(result < 0)
		return(result);

	/*
	 * Put the finishing touches on the sector and commit it to the
	 * data stream.
	 */

	if(sector->header.type == DATA_SECTOR)
		bkr_sector_randomize((unsigned int *) sector->buffer, sector->fmt.data_size - sizeof(bkr_sector_header_t), sector->header.number);

	put_sector_header(sector);

	bkr_encode_sector(device, sector);

	/*
	 * Reset for the next sector
	 */

	sector->header.number++;
	sector->header.low_used = 0;
	sector->offset = sector->buffer;

	return(0);
}


/*
 * bkr_sector_write_bor()
 *
 * Generate Begining-Of-Record mark.  Return values are the same as for
 * bkr_sector_write_data().
 */

static int bkr_sector_write_bor(bkr_device_t *device, bkr_sector_t *sector)
{
	int  result;

	if(sector->op_count == 0)
		sector->op_count = sectors_per_second(device) * BOR_LENGTH;

	memset(sector->buffer, BKR_FILLER, sector->end - sector->buffer);
	for(; sector->op_count; sector->op_count--)
		{
		sector->offset = sector->end;
		result = bkr_sector_write_data(device, sector);
		if(result < 0)
			return(result);
		}

	sector->header.type = DATA_SECTOR;
	sector->write = bkr_sector_write_data;

	return(0);
}


/*
 * bkr_sector_write_eor()
 *
 * Generate End-Of-Record mark.  Return values are the same as for
 * bkr_sector_write_data().
 */

int bkr_sector_write_eor(bkr_device_t *device, bkr_sector_t *sector)
{
	int  result;

	if(sector->op_count == 0)
		{
		if(sector->offset != sector->buffer)
			{
			result = bkr_sector_write_data(device, sector);
			if(result < 0)
				return(result);
			}
		sector->header.type = EOR_SECTOR;
		sector->op_count = sectors_per_second(device) * EOR_LENGTH;
		}

	memset(sector->buffer, BKR_FILLER, sector->end - sector->buffer);
	for(; sector->op_count; sector->op_count--)
		{
		sector->offset = sector->end;
		result = bkr_sector_write_data(device, sector);
		if(result < 0)
			return(result);
		}

	return(0);
}


/*
 * ========================================================================
 *
 *                              BY-PASS I/O
 *
 * ========================================================================
 */

/*
 * bkr_sector_read_raw(), bkr_sector_write_raw()
 *
 * Read and write raw data.  Return codes are the same as for formated
 * reading/writing.
 */

static int bkr_sector_read_raw(bkr_device_t *device, bkr_sector_t *sector)
{
	int  result;

	result = device->ops->read(device, sector->fmt.buffer_size);
	if(result < 0)
		return(result);

	if(device->tail + sector->fmt.buffer_size > device->size)
		{
		result = device->size - device->tail;
		memcpy(sector->buffer, device->buffer + device->tail, result);
		memcpy(sector->buffer + result, device->buffer, sector->fmt.buffer_size - result);
		}
	else
		memcpy(sector->buffer, device->buffer + device->tail, sector->fmt.buffer_size);
	device->tail = (device->tail + sector->fmt.buffer_size) & BKR_OFFSET_MASK;

	sector->offset = sector->buffer;

	return(0);
}

static int bkr_sector_write_raw(bkr_device_t *device, bkr_sector_t *sector)
{
	int  result;

	result = device->ops->write(device, sector->fmt.buffer_size);
	if(result < 0)
		return(result);

	memcpy(device->buffer + device->head, sector->buffer, sector->fmt.buffer_size);
	device->head += sector->fmt.buffer_size;
	if(device->head == device->size)
		device->head = 0;

	sector->offset = sector->buffer;

	return(0);
}


/*
 * ========================================================================
 *
 *                             MISCELLANEOUS
 *
 * ========================================================================
 */

/*
 * bkr_format_reset()
 *
 * Reset the formating layer (assumes device is initialized!).  The return
 * code indicates success or failure.
 */

int bkr_format_reset(bkr_device_t *device, bkr_sector_t *sector, int mode, bkr_state_t direction)
{
	int  i;

	/*
	 * Set up the device parameters.
	 */

	device->mode = mode;
	device->control = BKR_BIT_DMA_REQUEST;

	if(BKR_DENSITY(mode) == BKR_HIGH)
		{
		device->control |= BKR_BIT_HIGH_DENSITY;
		device->bytes_per_line = BYTES_PER_LINE_HIGH;
		}
	else
		device->bytes_per_line = BYTES_PER_LINE_LOW;

	if(BKR_VIDEOMODE(mode) == BKR_NTSC)
		{
		device->control |= BKR_BIT_NTSC_VIDEO;
		device->frame_size = device->bytes_per_line * (LINES_PER_FIELD_NTSC * 2 + 1);
		}
	else
		device->frame_size = device->bytes_per_line * LINES_PER_FIELD_PAL * 2;

	device->size = BKR_BUFFER_SIZE;
	if(direction == BKR_WRITING)
		device->size -= BKR_BUFFER_SIZE % device->frame_size;

	/*
	 * Initialize the modulation tables.
	 */

	memset(gcr_decode, (signed char) -1, 512);
	for(i = 0; i < 256; i++)
		gcr_decode[gcr_encode[i]] = i;

	/*
	 * Set up the software parameters.
	 */

	free(sector->buffer);

	if(BKR_FORMAT(mode) == BKR_RAW)
		{
		sector->fmt.buffer_size = device->frame_size;

		sector->buffer = (unsigned char *) malloc(sector->fmt.buffer_size);
		if(sector->buffer == NULL)
			return(-ENOMEM);

		sector->read = bkr_sector_read_raw;
		sector->write = bkr_sector_write_raw;
		sector->end = sector->buffer + sector->fmt.buffer_size;
		if(direction == BKR_WRITING)
			sector->offset = sector->buffer;
		else
			sector->offset = sector->end;
		}
	else
		{
		sector->fmt = format_info[bkr_mode_to_format(mode)];

		sector->oddfield            = 1;
		sector->need_sequence_reset = 1;
		sector->underflow_detect    = 1;
		sector->found_data          = 0;
		sector->op_count            = 0;

		sector->buffer = (unsigned char *) malloc(sector->fmt.buffer_size);
		if(sector->buffer == NULL)
			return(-ENOMEM);

		sector->read = bkr_sector_read_data;
		sector->write = bkr_sector_write_bor;
		sector->end = sector->buffer + bkr_sector_capacity(&sector->fmt);
		sector->offset = sector->end;

		reed_solomon_init(sector->fmt.buffer_size / sector->fmt.interleave, sector->fmt.data_size / sector->fmt.interleave, &sector->rs_format);
		}

	sector->header = SECTOR_HEADER_INITIALIZER;
	sector->errors = BKR_ERRORS_INITIALIZER;
	sector->health = BKR_HEALTH_INITIALIZER;

	return(0);
}
