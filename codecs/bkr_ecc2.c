/*
 * Driver for Danmere's Backer 16/32 video tape backup cards.
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


#include <errno.h>
#include <stdlib.h>
#include <string.h>


#include <backer.h>
#include <bkr_bytes.h>
#include <bkr_ecc2.h>
#include <bkr_ring_buffer.h>
#include <bkr_stream.h>
#include <rs.h>


#ifndef min
#define min(x,y) ({ \
	const typeof(x) _x = (x); \
	const typeof(y) _y = (y); \
	(void) (&_x == &_y); \
	_x < _y ? _x : _y ; \
})
#endif


/*
 * ========================================================================
 *
 *                              Parameters
 *
 * ========================================================================
 */


#define  ECC2_TIMEOUT_MULT  1
#define  BLOCK_SIZE         255
#define  PARITY             20
#define  ECC2_FILLER        0x33


/*
 * ========================================================================
 *
 *                              Global Data
 *
 * ========================================================================
 */


typedef u_int32_t data_length_t;


typedef struct {
	rs_format_t  rs_format;
	gf  *erasure;
	int  num_erasure;
	int  worst_group;
	unsigned long  extra_errors;
	int  group_size;
	int  data_size;
	int  parity_size;
	size_t  decode_head;
	int  eof;
	int  sector;
} bkr_ecc2_private_t;


/*
 * ========================================================================
 *
 *              Multi-Sector Forward Error Correction CODEC
 *
 * ========================================================================
 */


static data_length_t get_data_length(const struct bkr_stream_t *stream)
{
	struct ring ring = *stream->ring;
	data_length_t  data_length;

	_ring_drain(&ring, stream->capacity);
	memcpy_from_ring(&data_length, &ring, sizeof(data_length));

	return(__le32_to_cpu(data_length));
}


static void put_data_length(struct bkr_stream_t *stream, data_length_t data_length)
{
	data_length = __cpu_to_le32(data_length);
	memcpy_to_ring(stream->ring, &data_length, sizeof(data_length));
}


static int get_group(struct bkr_stream_t *stream)
{
	bkr_ecc2_private_t  *private = stream->private;
	struct bkr_stream_t  *source = stream->source;
	struct ring  *ring = stream->ring;
	struct ring  *source_ring = source->ring;
	int  result;

	for(; private->sector < BLOCK_SIZE; private->sector++) {
		if(space_in_ring(ring) < source->capacity)
			return(-EAGAIN);
		result = source->ops.read(source);
		if(result > 0) {
			ring_lock(ring);
			ring_lock(source_ring);
			memcpy_to_ring_from_ring(ring, source_ring, source->capacity);
			ring_unlock(source_ring);
			ring_unlock(ring);
			continue;
		} else if(result == -ENODATA) {
			ring_lock(ring);
			memset_ring(ring, ECC2_FILLER, source->capacity);
			ring_unlock(ring);
			if(private->num_erasure < PARITY)
				private->erasure[private->num_erasure++] = private->sector;
			continue;
		}
		if(result == 0)
			private->eof = 1;
		return(-EAGAIN);
	}

	private->sector = 0;
	return(0);
}


static void correct_group(struct bkr_stream_t *stream)
{
	bkr_ecc2_private_t  *private = stream->private;
	struct ring  *ring = stream->ring;
	int  num_blocks = stream->source->capacity;
	int  block;
	int corrections;
	unsigned char  *data, *parity;

	ring_lock(ring);
	data = ring->buffer + ring_offset_sub(ring, ring->head, private->group_size);
	parity = data + private->data_size;
	ring_unlock(ring);

	for(block = 0; block < num_blocks; block++) {
		memcpy(private->rs_format.erasure, private->erasure, private->num_erasure * sizeof(gf));
		corrections = reed_solomon_decode(parity++, data++, private->num_erasure, private->rs_format);
		if(corrections < 0) {
			/* uncorrectable block */
			continue;
		}
		if(corrections > private->worst_group)
			private->worst_group = corrections;
		if(corrections <= private->num_erasure)
			continue;
		private->extra_errors += corrections - private->num_erasure;
		memcpy(private->erasure, private->rs_format.erasure, corrections * sizeof(gf));
		private->num_erasure = corrections;
	}
	private->num_erasure = 0;
}


static int finish_group(struct bkr_stream_t *stream)
{
	bkr_ecc2_private_t  *private = stream->private;
	struct ring  *ring = stream->ring;
	int  num_blocks = stream->source->capacity;
	int  block;
	unsigned char  *data, *parity;
	data_length_t  data_length;

	ring_lock(ring);

	data_length = ring->head % private->group_size;
	data = ring->buffer + ring_offset_sub(ring, ring->head, data_length);
	parity = data + private->data_size;

	if(_space_in_ring(ring) < private->group_size - data_length) {
		ring_unlock(ring);
		return(-EAGAIN);
	}

	memset_ring(stream->ring, ECC2_FILLER, stream->capacity - data_length);
	put_data_length(stream, data_length);

	ring_unlock(ring);

	for(block = 0; block < num_blocks; block++)
		reed_solomon_encode(parity++, data++, private->rs_format);

	ring_fill(ring, private->parity_size);
	
	return(stream->capacity);
}


static int flush(struct bkr_stream_t *stream)
{
	bkr_ecc2_private_t  *private = stream->private;
	struct ring  *ring = stream->ring;
	int  head;

	ring_lock(ring);
	head = ring->head;
	ring_unlock(ring);

	if(head % private->group_size)
		if(finish_group(stream) == -EAGAIN)
			return(-EAGAIN);

	return(bytes_in_ring(ring) ? bkr_source_write_status(stream) : 0);
}


/*
 * ========================================================================
 *
 *                        I/O Activity Callbacks
 *
 * ========================================================================
 */


static void read_callback(struct bkr_stream_t *stream)
{
	while(get_group(stream) >= 0)
		correct_group(stream);

	bkr_stream_do_callback(stream);
}


static void write_callback(struct bkr_stream_t *stream)
{
	struct bkr_stream_t  *source = stream->source;
	struct ring  *ring = stream->ring;
	struct ring  *source_ring = source->ring;
	int  count;

	while(bytes_in_ring(ring)) {
		count = source->ops.write(source);
		if(count < 1)
			break;
		ring_lock(ring);
		ring_lock(source_ring);
		memcpy_to_ring_from_ring(source_ring, ring, min((unsigned) count, _bytes_in_ring(ring)));
		ring_unlock(source_ring);
		ring_unlock(ring);
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
	bkr_ecc2_private_t  *private = stream->private;
	struct bkr_stream_t  *source = stream->source;
	int  result;

	if(direction == BKR_READING) {
		bkr_stream_set_callback(source, (void (*)(void *)) read_callback, stream);
		ring_offset_dec(stream->ring, stream->ring->tail, 1);
		ring_offset_dec(stream->ring, private->decode_head, 1);
	} else
		bkr_stream_set_callback(source, (void (*)(void *)) write_callback, stream);

	result = source->ops.start(source, direction);
	if(result >= 0)
		stream->direction = direction;

	return(result);
}


static int read(struct bkr_stream_t *stream)
{
	bkr_ecc2_private_t  *private = stream->private;
	struct ring  *ring = stream->ring;
	int  count;

	ring_lock(ring);

	count = ring_offset_sub(ring, private->decode_head, ring->tail);
	if(!count) {
		count = private->group_size - ring->tail % private->group_size;
		if(_bytes_in_ring(ring) < count + private->group_size) {
			ring_unlock(ring);
			if(private->eof)
				return(0);
			return(-EAGAIN);
		}
		_ring_drain(ring, count);
		private->decode_head = ring_offset_add(ring, ring->tail, get_data_length(stream));
	}

	ring_unlock(ring);
	return(count);
}


static int write(struct bkr_stream_t *stream)
{
	bkr_ecc2_private_t  *private = stream->private;
	struct ring  *ring = stream->ring;
	int  result;
	
	ring_lock(ring);
	result = stream->capacity - ring->head % private->group_size;
	ring_unlock(ring);
	if(!result) {
		result = finish_group(stream);
		if(result < 0)
			result = bkr_source_write_status(stream);
	}

	return(space_in_ring(ring) < result ? -EAGAIN : result);
}


static int release(struct bkr_stream_t *stream)
{
	bkr_ecc2_private_t  *private = stream->private;
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

	reed_solomon_codec_free(&private->rs_format);
	ring_free(stream->ring);
	free(private->erasure);
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
	.read = read,
	.write = write,
};


static struct bkr_stream_t *new(struct bkr_stream_t *source, int mode, const bkr_format_info_t *fmt)
{
	struct bkr_stream_t  *stream;
	bkr_ecc2_private_t  *private;

	if(!source)
		goto no_source;

	stream = malloc(sizeof(*stream));
	private = malloc(sizeof(*private));
	if(!stream || !private)
		goto no_stream;
	stream->ring = malloc(sizeof(*stream->ring));
	private->erasure = malloc(PARITY * sizeof(*private->erasure));
	if(!stream->ring || !private->erasure)
		goto no_buffers;

	private->num_erasure = 0;
	private->worst_group = 0;
	private->extra_errors = 0;
	private->group_size = BLOCK_SIZE * source->capacity;
	private->parity_size = PARITY * source->capacity;
	private->data_size = private->group_size - private->parity_size;
	private->decode_head = 0;
	private->eof = 0;
	private->sector = 0;

	if(!ring_alloc(stream->ring, 3 * private->group_size))
		goto no_buffers;

	if(reed_solomon_codec_new(BLOCK_SIZE, BLOCK_SIZE - PARITY, source->capacity, &private->rs_format) < 0)
		goto no_rs_codec;

	stream->fmt = *fmt;
	stream->source = source;
	bkr_stream_set_callback(stream, NULL, NULL);
	stream->mode = mode;
	stream->direction = BKR_STOPPED;
	stream->ops = stream_ops;
	stream->capacity = private->data_size - sizeof(data_length_t);
	stream->timeout = ECC2_TIMEOUT_MULT * source->timeout;
	stream->private = private;

	return(stream);

no_rs_codec:
	ring_free(stream->ring);
no_buffers:
	free(stream->ring);
	free(private->erasure);
no_stream:
	free(private);
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


const struct bkr_stream_ops_t *bkr_ecc2_codec_init(void)
{
	galois_field_init(GF00256);

	return(&stream_ops);
}
