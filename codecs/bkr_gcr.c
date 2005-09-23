/*
 * Driver for Danmere's Backer 16/32 video tape backup cards.
 *
 *                             Formating Layer
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


#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/slab.h>
static void *malloc(size_t size)  { return(kmalloc(size, GFP_KERNEL)); }
static void free(void *ptr) { kfree(ptr); }
#else
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#endif /* __KERNEL__ */

#include <backer.h>
#include <bkr_bytes.h>
#include <bkr_gcr.h>
#include <bkr_ring_buffer.h>
#include <bkr_stream.h>


/*
 * ========================================================================
 *
 *                              Parameters
 *
 * ========================================================================
 */

#define  GCR_BUFFER_SIZE   (1<<14)
#define  GCR_TIMEOUT_MULT  1


/*
 * ========================================================================
 *
 *                     8/9 GCR Modem Implementation
 *
 * ========================================================================
 */

/* use inline assembly if possible */
#if 1
#define  rolw(x, n)  asm("rolw %2, %0" : "=r" (x) : "0" (x), "c" (n))
#else
#define  rolw(x, n)  do { x = bswap_16(x) >> (8 - (n)) } while(0)
#endif

static const u_int16_t gcr_encode[] = {
	0x089, 0x08a, 0x08b, 0x08c, 0x08d, 0x08e, 0x091, 0x092,
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
	0x1e5, 0x1e6, 0x1e9, 0x1ea, 0x1eb, 0x1ec, 0x1ed, 0x1ee
};
static unsigned char gcr_decode[512];
#define  GCR_MASK  ((u_int16_t) 0x01ff)


static void gcr_demodulate(unsigned char *src, unsigned char *dst, size_t n)
{
	u_int16_t  state = 0, rgstr;
	signed char  shift = 1;

	n >>= 3;

	while(1) {
		rgstr = __le16_to_cpu(get_unaligned((u_int16_t *) src++));
		rolw(rgstr, shift);
		if(state & 1)
			rgstr = ~rgstr;
		state ^= rgstr;
		*dst++ = gcr_decode[rgstr & GCR_MASK];
		if(++shift > 8) {
			if(!--n)
				break;
			src++;
			shift = 1;
		}
	}
}


static void gcr_modulate(unsigned char *src, unsigned char *dst, size_t n)
{
	u_int16_t  state = 0, rgstr = 0;
	signed char  shift = 7;

	n >>= 3;

	while(1) {
		if(state &= 1)
			state = GCR_MASK;
		state ^= gcr_encode[*src++];
		rgstr |= state << shift;
		if(--shift >= 0) {
			rgstr = bswap_16(rgstr);
			*dst++ = rgstr; /* write low byte */
			rgstr &= (u_int16_t) 0xff00;
		} else {
			put_unaligned(__cpu_to_be16(rgstr), (u_int16_t *) dst);
			if(!--n)
				break;
			dst += sizeof(u_int16_t);
			rgstr = 0;
			shift = 7;
		}
	}
}


static int flush(struct bkr_stream_t *stream)
{
	return(bkr_stream_fill_to(stream, stream->capacity, BKR_FILLER) < 0 ? -EAGAIN : bytes_in_ring(stream->ring) ? bkr_source_write_status(stream) : 0);
}


/*
 * ========================================================================
 *
 *                        I/O Activity Callbacks
 *
 * ========================================================================
 */

ring_data_t *ring_head(struct ring *ring)
{
	ring_data_t *ptr;

	ring_lock(ring);
	ptr = ring->buffer + ring->head;
	ring_unlock(ring);

	return(ptr);
}


ring_data_t *ring_tail(struct ring *ring)
{
	ring_data_t *ptr;

	ring_lock(ring);
	ptr = ring->buffer + ring->tail;
	ring_unlock(ring);

	return(ptr);
}


static void read_callback(struct bkr_stream_t *stream)
{
	int  source_capacity = stream->source->capacity;
	int  capacity = stream->capacity;
	struct ring  *src_ring = stream->source->ring;
	struct ring  *dst_ring = stream->ring;

	while((bytes_in_ring(src_ring) >= source_capacity) && (space_in_ring(dst_ring) >= capacity)) {
		gcr_demodulate(ring_tail(src_ring), ring_head(dst_ring), capacity);
		ring_drain(src_ring, source_capacity);
		ring_fill(dst_ring, capacity);
	}

	bkr_stream_do_callback(stream);
}


static void write_callback(struct bkr_stream_t *stream)
{
	int  source_capacity = stream->source->capacity;
	int  capacity = stream->capacity;
	struct ring  *src_ring = stream->ring;
	struct ring  *dst_ring = stream->source->ring;

	while((bytes_in_ring(src_ring) >= capacity) && (space_in_ring(dst_ring) >= source_capacity)) {
		gcr_modulate(ring_tail(src_ring), ring_head(dst_ring), capacity);
		ring_drain(src_ring, capacity);
		ring_fill(dst_ring, source_capacity);
	}

	bkr_stream_do_callback(stream);
}


/*
 * ========================================================================
 *
 *                              Stream API
 *
 * ========================================================================
 */

static int start(struct bkr_stream_t *stream, bkr_direction_t direction)
{
	struct bkr_stream_t  *source = stream->source;
	int  result;

	if(direction == BKR_READING)
		bkr_stream_set_callback(source, (void (*)(void *)) read_callback, stream);
	else
		bkr_stream_set_callback(source, (void (*)(void *)) write_callback, stream);

	result = source->ops.start(source, direction);
	if(result >= 0)
		stream->direction = direction;

	return(result);
}


static int release(struct bkr_stream_t *stream)
{
	int  result;

	if(stream->direction == BKR_WRITING) {
		result = flush(stream);
		if(result < 0)
			return(result);
	}
	stream->direction = BKR_STOPPED;

	result = stream->source->ops.release(stream->source);
	if(result < 0)
		return(result);

	ring_free(stream->ring);
	free(stream->ring);
	free(stream->private);
	free(stream);
	return(0);
}


static struct bkr_stream_t *new(struct bkr_stream_t *, int, const bkr_format_info_t *);

static const struct bkr_stream_ops_t stream_ops = {
	.new = new,
	.start = start,
	.release = release,
	.read = bkr_simple_stream_read,
	.write = bkr_simple_stream_write,
};

static struct bkr_stream_t *new(struct bkr_stream_t *source, int mode, const bkr_format_info_t *fmt)
{
	int  capacity;
	struct bkr_stream_t  *stream;

	if(!source)
		goto no_source;

	capacity = source->capacity - fmt->modulation_pad;

	stream = malloc(sizeof(*stream));
	if(!stream)
		goto no_stream;
	stream->ring = malloc(sizeof(*stream->ring));
	if(!stream->ring)
		goto no_stream;

	if(!ring_alloc(stream->ring, GCR_BUFFER_SIZE - GCR_BUFFER_SIZE % capacity))
		goto no_ring_buffer;

	stream->fmt = *fmt;
	stream->source = source;
	bkr_stream_set_callback(stream, NULL, NULL);
	stream->mode = mode;
	stream->direction = BKR_STOPPED;
	stream->ops = stream_ops;
	stream->capacity = capacity;
	stream->timeout = GCR_TIMEOUT_MULT * source->timeout;
	stream->private = NULL;

	return(stream);

no_ring_buffer:
	free(stream->ring);
no_stream:
	free(stream);
no_source:
	return(NULL);
}


/*
 * ========================================================================
 *
 *                               CODEC API
 *
 * ========================================================================
 */

const struct bkr_stream_ops_t *bkr_gcr_codec_init(void)
{
	int  i;

	memset(gcr_decode, (signed char) -1, 512);
	for(i = 0; i < 256; i++)
		gcr_decode[gcr_encode[i]] = i;

	return(&stream_ops);
}
