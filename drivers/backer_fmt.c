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
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#define  malloc(x)  kmalloc((x), GFP_KERNEL)
#define  free(x)    kfree(x)

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

#define  MIN_MATCH_WEIGHT       21      /* range: [0, BKR_MAX_KEY_WEIGHT] */
#define  BKR_LEADER             0xe2    /* leader is filled with this */
#define  BKR_TRAILER            0x33    /* trailer is filled with this */
#define  BKR_FILLER             0x33    /* unused space is filled with this */


/*
 * ========================================================================
 *
 *                              GLOBAL DATA
 *
 * ========================================================================
 */

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

static inline bkr_sector_header_t get_sector_header(bkr_stream_t *sector)
{
	u_int32_t  tmp;

	tmp = get_unaligned((u_int32_t *) (sector->buffer + bkr_sector_capacity(&sector->fmt)));
	tmp = __le32_to_cpu(tmp);

	return(*(bkr_sector_header_t *) &tmp);
}

static inline void put_sector_header(bkr_stream_t *sector)
{
	u_int32_t tmp;

	tmp = *(u_int32_t *) &sector->header;
	tmp = __cpu_to_le32(tmp);

	put_unaligned(tmp, (u_int32_t *) (sector->buffer + bkr_sector_capacity(&sector->fmt)));
}

static inline unsigned int encode_sector_length(unsigned int length)
{
	return(length + (length / 15) + 1);
}

static inline unsigned int decode_sector_length(unsigned int length)
{
	return(length - (length / 16) - 1);
}

#define  HIGH_USED(x)   (x)->buffer[bkr_sector_capacity(&(x)->fmt) - 1]
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
 * bkr_correlate() computes the correlation weight (0 to 256) of the
 * current location in the data stream against the sector key pattern.
 *
 * bkr_find_sector() uses bkr_correlate() to scan the data stream until a
 * sector is found.  On success, the buffer tail is left pointing to the
 * first byte following the sector leader and the number of bytes skipped
 * is returned;  otherwise the return value is negative.
 */

static inline unsigned int bkr_correlate(bkr_device_t *device, bkr_stream_t *sector)
{
	int  i, j;
	unsigned int  weight = 0;

	i = (device->io_tail + sector->fmt.key_length*sector->fmt.key_interval - 1) & BKR_OFFSET_MASK;
	j = sector->fmt.key_length;
	while(--j >= 0)
		{
		if(device->io_buffer[i] == key[j])
			weight++;
		i = (i - sector->fmt.key_interval) & BKR_OFFSET_MASK;
		}

	weight = (weight * BKR_MAX_KEY_WEIGHT) / sector->fmt.key_length;

	return(weight);
}

static inline int bkr_find_sector(bkr_device_t *device, bkr_stream_t *sector)
{
	int  result, field_size;

	while(1)
		{
		result = device->ops->read(device, sector->fmt.field_size);
		if(result < 0)
			return(result);

		result = bkr_correlate(device, sector);
		if(result >= MIN_MATCH_WEIGHT)
			break;
		if(result > sector->health.best_nonkey)
			sector->health.best_nonkey = result;
		device->io_tail = (device->io_tail + 1) & BKR_OFFSET_MASK;
		}
	if(result < sector->health.worst_key)
		sector->health.worst_key = result;

	if(sector->health.last_field_offset >= 0)
		{
		field_size = (device->io_tail - sector->health.last_field_offset) & BKR_OFFSET_MASK;
		if(field_size < sector->health.smallest_field)
			sector->health.smallest_field = field_size;
		else if(field_size > sector->health.largest_field)
			sector->health.largest_field = field_size;
		if(field_size > sector->fmt.field_size + sector->fmt.interlace)
			sector->errors.frame++;
		}
	sector->health.last_field_offset = device->io_tail;

	return(0);
}


/*
 * bkr_decode_sector()
 *
 * Decodes a sector from the current position in the data stream.
 */

static int bkr_decode_sector(bkr_device_t *device, bkr_stream_t *sector)
{
	int  i, j;
	u_int16_t  state, tmp;
	unsigned int  in_off, out_off;
	unsigned char  *data, *parity;
	signed char  shift;

	/*
	 * Remove the key sequence.
	 */

	out_off = (device->io_tail + sector->fmt.key_interval - 1) & BKR_OFFSET_MASK;
	for(j = 1; j < sector->fmt.key_length; j++)
		for(i = sector->fmt.key_interval - 1; i; i--)
			{
			device->io_buffer[out_off] = device->io_buffer[(out_off + j) & BKR_OFFSET_MASK];
			out_off = (out_off + 1) & BKR_OFFSET_MASK;
			}

	/*
	 * Demodulate the bit stream.
	 */

	if(sector->fmt.gcr)
		{
		in_off = out_off = device->io_tail;
		state = 0;
		shift = 1;
		while(1)
			{
			tmp  = device->io_buffer[in_off];
			in_off = (in_off + 1) & BKR_OFFSET_MASK;
			tmp |= device->io_buffer[in_off] << 8;
			rolw(tmp, shift);
			if(state & 1)
				tmp = ~tmp;
			state ^= tmp;
			device->io_buffer[out_off] = gcr_decode[tmp & GCR_MASK];
			out_off = (out_off + 1) & BKR_OFFSET_MASK;
			if(++shift > 8)
				{
				if(out_off - device->io_tail >= sector->fmt.buffer_size)
					break;
				in_off = (in_off + 1) & BKR_OFFSET_MASK;
				shift = 1;
				}
			}
		}

	/*
	 * Deinterleave the sector from the I/O buffer into the sector
	 * buffer.  Note that after being advanced the I/O tail is backed
	 * off by the size of the leader.  This is done to accomodate the
	 * possibility of the VCR/Backer skipping some leader lines as well
	 * trailer lines.
	 */

	for(i = 0; i < sector->rs_format.k; i += 1 - sector->fmt.data_size)
		for(; i < sector->fmt.data_size; i += sector->rs_format.k)
			{
			sector->buffer[i] = device->io_buffer[device->io_tail];
			device->io_tail = (device->io_tail + 1) & BKR_OFFSET_MASK;
			}
	for(i = sector->fmt.data_size; i < sector->fmt.data_size + sector->rs_format.parity; i += 1 - sector->fmt.parity_size)
		for(; i < sector->fmt.buffer_size; i += sector->rs_format.parity)
			{
			sector->buffer[i] = device->io_buffer[device->io_tail];
			device->io_tail = (device->io_tail + 1) & BKR_OFFSET_MASK;
			}
	device->io_tail = (device->io_tail + sector->fmt.modulation_pad) & BKR_OFFSET_MASK;

	/*
	 * Do error correction.
	 */

	data = sector->buffer;
	parity = sector->buffer + sector->fmt.data_size;
	for(i = 0; i < sector->fmt.interleave; data += sector->rs_format.k, parity += sector->rs_format.parity, i++)
		{
		j = reed_solomon_decode(parity, data, NULL, 0, &sector->rs_format);
		if(j < 0)
			return(-ENODATA);
		sector->health.total_errors += j;
		if((unsigned int) j > sector->errors.symbol)
			sector->errors.symbol = j;
		if((unsigned int) j > sector->errors.recent_symbol)
			sector->errors.recent_symbol = j;
		}

	return(0);
}


/*
 * bkr_encode_sector()
 *
 * Encode a sector into the data stream.  This involves adding the header,
 * the footer, applying the appropriate bit modulation, mixing with the key
 * sequence and writing the whole thing to the I/O buffer.
 */

static void bkr_encode_sector(bkr_device_t *device, bkr_stream_t *sector)
{
	int  i;
	u_int16_t  state, tmp;
	unsigned char  *in_pos, *out_pos;
	unsigned char  *data, *parity;
	signed char  shift;

	/*
	 * Write the leader to the I/O buffer.
	 */

	memset(device->io_buffer + device->io_head, BKR_LEADER, sector->fmt.leader);
	device->io_head += sector->fmt.leader;

	/*
	 * Compute Reed-Solomon parity symbols.
	 */

	data = sector->buffer;
	parity = sector->buffer + sector->fmt.data_size;
	for(i = 0; i < sector->fmt.interleave; data += sector->rs_format.k, parity += sector->rs_format.parity, i++)
		reed_solomon_encode(parity, data, &sector->rs_format);

	/*
	 * Interleave the data into the I/O buffer.
	 */

	out_pos = &device->io_buffer[device->io_head + sector->fmt.modulation_pad];
	for(i = 0; i < sector->rs_format.k; i += 1 - sector->fmt.data_size)
		for(; i < sector->fmt.data_size; i += sector->rs_format.k)
			*(out_pos++) = sector->buffer[i];
	for(i = sector->fmt.data_size; i < sector->fmt.data_size + sector->rs_format.parity; i += 1 - sector->fmt.parity_size)
		for(; i < sector->fmt.buffer_size; i += sector->rs_format.parity)
			*(out_pos++) = sector->buffer[i];

	/*
	 * Modulate the bit stream.
	 */

	if(sector->fmt.gcr)
		{
		in_pos = &device->io_buffer[device->io_head + sector->fmt.modulation_pad];
		out_pos = &device->io_buffer[device->io_head + sector->fmt.key_length];
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
	 * Insert the key sequence.
	 */

	out_pos = &device->io_buffer[device->io_head];
	for(i = 0; i < sector->fmt.key_length; *(out_pos++) = key[i++])
		{
		memmove(out_pos, out_pos + sector->fmt.key_length - i, sector->fmt.key_interval - 1);
		out_pos += sector->fmt.key_interval - 1;
		}
	device->io_head += sector->fmt.active_size;

	/*
	 * Write the trailer to the I/O buffer.
	 */

	i = sector->fmt.trailer;
	if(device->odd_field)
		i += sector->fmt.interlace;
	memset(device->io_buffer + device->io_head, BKR_TRAILER, i);
	device->io_head += i;
	if(device->io_head == device->io_size)
		device->io_head = 0;
	device->odd_field ^= 1;
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

	if(!count)
		return;

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
 * until we either find an acceptable sector (it's a non-BOR sector and is
 * in the correct order) or we encounter an error.  If we find a sector we
 * like, we set it up for a read out and return 0;  otherwise we return < 0
 * on error.  On error, a retry can be attempted by simply re-calling this
 * function.  EOF is indicated by a sector being returned with no data in
 * it.
 *
 * We tolerate junk sectors for the first little while in order to
 * transparently skip over the noise found at the beginning of recordings.
 * This is done by silently skipping over all errors (except -EAGAIN) until
 * we find a valid data sector after which full error reporting is
 * restored.  When reporting errors, -ENODATA is returned exactly once for
 * each irrecoverable sector i.e. for each uncorrectable sector and for
 * each sector lost in an overrun.
 */

static int bkr_sector_read_data(bkr_device_t *device, bkr_stream_t *sector)
{
	bkr_sector_header_t  header;
	int  length, result;

	/*
	 * Prevent further read-outs until we say so.
	 */

	sector->pos = sector->end;

	/*
	 * Do we need to generate more -ENODATAs?
	 */

	if(sector->op_count)
		{
		if(--sector->op_count > 0)
			return(-ENODATA);
		sector->pos = sector->buffer;
		return(0);
		}

	/*
	 * Main loop.
	 */

	while(1)
		{
		/*
		 * Get the next sector.
		 */

		result = bkr_find_sector(device, sector);
		if(result < 0)
			break;
		result = bkr_decode_sector(device, sector);
		if(result < 0)
			{
			if(sector->header.number < 0)	/* still in BOR? */
				{
				sector->need_sequence_reset = 1;
				continue;
				}
			sector->header.number++;
			sector->errors.block++;
			break;
			}
		header = get_sector_header(sector);

		/*
		 * Check sector order.
		 */

		result = header.number - sector->header.number - 1;
		if((result == 0) || sector->need_sequence_reset)
			sector->need_sequence_reset = 0;
		else if(result > 0)  /* overrun? */
			{
			sector->errors.overrun++;
			sector->op_count = result;	/* return this many -ENODATAs */
			}
		else  /* underrun */
			{
			sector->errors.underrun += sector->not_underrunning;
			sector->not_underrunning = 0;
			continue;
			}
		sector->not_underrunning = 1;
		sector->header = header;

		/*
		 * If this is part of the BOR marker, reset the error
		 * counters and skip it.
		 */

		if(header.number < 0)
			{
			sector->op_count = 0;
			sector->errors = BKR_ERRORS_INITIALIZER;
			sector->health = BKR_HEALTH_INITIALIZER;
			continue;
			}

		/*
		 * Determine the sector length and de-randomize it (we've
		 * already set things for a standard length).
		 */

		sector->pos = sector->buffer;
		if(header.low_used)
			length = decode_sector_length( ((unsigned int) HIGH_USED(sector) << 4) + header.low_used );
		else
			length = bkr_sector_capacity(&sector->fmt);
		sector->end = sector->buffer + length;
		bkr_sector_randomize((unsigned int *) sector->buffer, length, header.number);

		if(sector->op_count)
			{
			sector->pos = sector->end;
			result = -ENODATA;
			}
		else
			result = 0;
		break;
		}

	return(result);
}


/*
 * bkr_sector_write_data()
 *
 * Writes the current sector to the I/O buffer and resets for the next one.
 * Returns 0 on success, < 0 on error in which case a retry can be
 * attempted by simply re-calling this function.
 */

static int bkr_sector_write_data(bkr_device_t *device, bkr_stream_t *sector)
{
	int  length, result;

	/*
	 * Make sure there's room in the I/O buffer.
	 */

	result = device->ops->write(device, sector->fmt.field_size + sector->fmt.interlace);
	if(result < 0)
		return(result);

	/*
	 * Randomize the used portion of the sector.
	 */

	length = sector->pos - sector->buffer;
	bkr_sector_randomize((unsigned int *) sector->buffer, length, sector->header.number);

	/*
	 * If there is unused space, pad it and encode the length.
	 */

	if(sector->pos < sector->end)
		{
		memset(sector->pos, BKR_FILLER, sector->end - sector->pos - 1);
		result = encode_sector_length(length);
		sector->header.low_used = result;
		HIGH_USED(sector) = result >> 4;
		}

	/*
	 * Write the header and encode the sector into the data stream.
	 */

	put_sector_header(sector);
	bkr_encode_sector(device, sector);

	/*
	 * Reset for the next sector.
	 */

	sector->header.number++;
	sector->header.low_used = 0;
	sector->pos = sector->buffer;

	return(0);
}


/*
 * bkr_sector_write_bor()
 *
 * Generate the Begining-Of-Record mark.  Return values are the same as for
 * bkr_sector_write_data().
 */

static int bkr_sector_write_bor(bkr_device_t *device, bkr_stream_t *sector)
{
	int  result;

	if(sector->op_count == 0)
		{
		sector->op_count = sectors_per_second(device) * BOR_LENGTH;
		sector->header.number = -sector->op_count;
		}

	for(; sector->op_count; sector->op_count--)
		{
		sector->pos = sector->buffer;
		result = bkr_sector_write_data(device, sector);
		if(result < 0)
			{
			sector->pos = sector->end;
			return(result);
			}
		}

	sector->write = bkr_sector_write_data;

	return(0);
}


/*
 * bkr_sector_write_eor()
 *
 * Generate the End-Of-Record mark.  Return values are the same as for
 * bkr_sector_write_data().
 */

int bkr_sector_write_eor(bkr_device_t *device, bkr_stream_t *sector)
{
	int  result = 0;

	if(sector->op_count == 0)
		{
		if(sector->pos != sector->buffer)
			{
			result = bkr_sector_write_data(device, sector);
			if(result < 0)
				return(result);
			}
		sector->op_count = sectors_per_second(device) * EOR_LENGTH;
		}

	for(; sector->op_count; sector->op_count--)
		{
		result = bkr_sector_write_data(device, sector);
		if(result < 0)
			break;
		}

	return(result);
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

static int bkr_sector_read_raw(bkr_device_t *device, bkr_stream_t *sector)
{
	int  tmp;

	tmp = device->ops->read(device, sector->fmt.buffer_size);
	if(tmp < 0)
		return(tmp);

	tmp = device->io_size - device->io_tail;
	if(sector->fmt.buffer_size > tmp)
		{
		memcpy(sector->buffer, device->io_buffer + device->io_tail, tmp);
		memcpy(sector->buffer + tmp, device->io_buffer, sector->fmt.buffer_size - tmp);
		}
	else
		memcpy(sector->buffer, device->io_buffer + device->io_tail, sector->fmt.buffer_size);
	device->io_tail = (device->io_tail + sector->fmt.buffer_size) & BKR_OFFSET_MASK;

	sector->pos = sector->buffer;

	return(0);
}

static int bkr_sector_write_raw(bkr_device_t *device, bkr_stream_t *sector)
{
	int  result;

	result = device->ops->write(device, sector->fmt.buffer_size);
	if(result < 0)
		return(result);

	memcpy(device->io_buffer + device->io_head, sector->buffer, sector->fmt.buffer_size);
	device->io_head += sector->fmt.buffer_size;
	if(device->io_head == device->io_size)
		device->io_head = 0;

	sector->pos = sector->buffer;

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

int bkr_format_reset(bkr_device_t *device, bkr_stream_t *sector, int mode, bkr_state_t direction)
{
	int  i;

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

	sector->need_sequence_reset = 1;
	sector->not_underrunning    = 1;
	sector->op_count            = 0;
	sector->fmt = device->format_info[bkr_mode_to_format(mode)];

	sector->buffer = (unsigned char *) malloc(sector->fmt.buffer_size);
	if(sector->buffer == NULL)
		return(-ENOMEM);

	if(BKR_FORMAT(mode) == BKR_RAW)
		{
		sector->read = bkr_sector_read_raw;
		sector->write = bkr_sector_write_raw;
		sector->end = sector->buffer + sector->fmt.buffer_size;
		if(direction == BKR_WRITING)
			sector->pos = sector->buffer;
		else
			sector->pos = sector->end;
		}
	else
		{
		sector->read = bkr_sector_read_data;
		sector->write = bkr_sector_write_bor;
		sector->end = sector->buffer + bkr_sector_capacity(&sector->fmt);
		sector->pos = sector->end;

		reed_solomon_init(sector->fmt.buffer_size / sector->fmt.interleave, sector->fmt.data_size / sector->fmt.interleave, &sector->rs_format);
		}

	sector->header = SECTOR_HEADER_INITIALIZER;
	sector->errors = BKR_ERRORS_INITIALIZER;
	sector->health = BKR_HEALTH_INITIALIZER;

	/*
	 * Set up the device parameters.
	 */

	device->mode       = mode;
	device->odd_field  = 1;
	device->frame_size = sector->fmt.frame_size;

	device->control = BKR_BIT_DMA_REQUEST;
	if(BKR_DENSITY(mode) == BKR_HIGH)
		device->control |= BKR_BIT_HIGH_DENSITY;
	if(BKR_VIDEOMODE(mode) == BKR_NTSC)
		device->control |= BKR_BIT_NTSC_VIDEO;

	device->io_size = BKR_BUFFER_SIZE;
	if(direction == BKR_WRITING)
		{
		device->io_size -= BKR_BUFFER_SIZE % device->frame_size;
		device->control |= BKR_BIT_TRANSMIT;
		}
	else
		device->control |= BKR_BIT_RECEIVE;
	device->io_head = device->io_tail = 0;

	return(0);
}
