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
#include <linux/slab.h>
static void *malloc(size_t size)  { return(kmalloc(size, GFP_KERNEL)); }
static void free(void *ptr) { kfree(ptr); }
#else
#include <errno.h>
#include <stdlib.h>
#endif

#include <backer.h>
#include <bkr_frame.h>
#include <bkr_ring_buffer.h>
#include <bkr_stream.h>


/*
 * ========================================================================
 *
 *                              PARAMETERS
 *
 * ========================================================================
 */

#define  FRAME_BUFFER_SIZE      (1<<14) /* FIXME: should be 512 kB */
#define  FRAME_TIMEOUT_MULT     1       /* device timeout multiplier */
#define  BKR_LEADER             0xe2    /* leader is filled with this */
#define  BKR_TRAILER            0x33    /* trailer is filled with this */
#define  FRAME_THRESHOLD_A      21
#define  FRAME_THRESHOLD_B      64


/*
 * ========================================================================
 *
 *                              Global Data
 *
 * ========================================================================
 */

static const unsigned char  sector_key[] = {
	0xd4, 0x7c, 0xb1, 0x93, 0x66, 0x65, 0x6a, 0xb5,
	0x63, 0xe4, 0x56, 0x59, 0x6c, 0xbe, 0xc5, 0xca,
	0xf4, 0x9c, 0xa3, 0xac, 0x6d, 0xb3, 0xd2, 0x7e,
	0x74, 0xa6, 0xe1, 0xa9, 0x5c, 0x9a, 0x4b, 0x5d
};


/*
 * ========================================================================
 *
 *                              CODEC Functions
 *
 * ========================================================================
 */

/*
 * Counts the number of bytes in the frame that match the key sequence.
 */

static unsigned int bkr_frame_correlate(struct ring ring, int key_interval, int key_length, const unsigned char *key)
{
	int  i;
	unsigned int  count = 0;

	for(i = key_length; i > 0; i--) {
		count += ring.buffer[ring.tail] == *(key++);
		ring_offset_inc(&ring, ring.tail, key_interval);
	}

	return(count);
}


/*
 * Uses bkr_frame_correlate() to scan the data stream until a sector key
 * sequence is found.  On success, the buffer tail is left pointing to the
 * first byte following the sector leader and the return value is
 * non-negative; otherwise the return value is non-positive (0 == EOF on
 * stream source).
 */

static int bkr_frame_find_field(struct bkr_stream_t *stream, const unsigned char *key)
{
	struct bkr_stream_t  *source = stream->source;
	bkr_frame_private_t  *private = stream->private;
	int  threshold = stream->fmt.key_length * FRAME_THRESHOLD_A / FRAME_THRESHOLD_B;
	int  result;

	while(1) {
		result = source->ops.read(source);
		if(result <= 0)
			return(result);
		if(result < stream->fmt.active_size)
			return(-EAGAIN);

		ring_lock(source->ring);
		result = bkr_frame_correlate(*source->ring, stream->fmt.key_interval, stream->fmt.key_length, key);
		if(result >= threshold) {
			ring_unlock(source->ring);
			break;
		}
		if(result > private->best_nonkey)
			private->best_nonkey = result;
		_ring_drain(source->ring, 1);
		ring_unlock(source->ring);
	}

	if(result < private->worst_key)
		private->worst_key = result;

	if(private->last_field_offset >= 0) {
		result = ring_offset_sub(source->ring, source->ring->tail, private->last_field_offset);
		if(result < private->smallest_field)
			private->smallest_field = result;
		else if(result > private->largest_field)
			private->largest_field = result;
		if(result*4 > stream->source->capacity*3)
			private->frame_warnings++;
	}
	private->last_field_offset = source->ring->tail;

	return(1);
}


/*
 * Strips the sector data out of a video field in the source ring buffer
 * and places it in the stream ring buffer.
 */

static void bkr_frame_decode_field(struct bkr_stream_t *stream)
{
	struct bkr_stream_t  *source = stream->source;
	int  i;

	ring_lock(source->ring);
	for(i = 1; i < stream->fmt.key_length; i++) {
		_ring_drain(source->ring, 1);
		memcpy_to_ring_from_ring(stream->ring, source->ring, stream->fmt.key_interval - 1);
	}
	_ring_drain(source->ring, 1);
	memcpy_to_ring_from_ring(stream->ring, source->ring, stream->fmt.active_size % stream->fmt.key_interval - 1);
	ring_unlock(source->ring);
}


/*
 * Moves one frame of encoded data from the codec ring to the source ring,
 * adding leader trailer and key bytes as required.
 */

static void bkr_frame_encode_field(struct bkr_stream_t *stream, struct bkr_stream_t *source, const unsigned char *key, int field_number)
{
	int  i;

	memset_ring(source->ring, BKR_LEADER, stream->fmt.leader);

	for(i = 1; i < stream->fmt.key_length; i++) {
		memset_ring(source->ring, *key++, 1);
		memcpy_to_ring_from_ring(source->ring, stream->ring, stream->fmt.key_interval - 1);
	}
	memset_ring(source->ring, *key++, 1);
	memcpy_to_ring_from_ring(source->ring, stream->ring, stream->fmt.active_size % stream->fmt.key_interval - 1);

	memset_ring(source->ring, BKR_TRAILER, stream->fmt.trailer + ((field_number & 1) ? stream->fmt.interlace : 0));
}

static int bkr_frame_encode(struct bkr_stream_t *stream, const unsigned char *key)
{
	struct bkr_stream_t  *source = stream->source;
	int  result = source->ops.write(source);

	if(result < source->capacity)
		return(result < 0 ? result : -EAGAIN);

	ring_lock(source->ring);
	bkr_frame_encode_field(stream, source, key, 1);	/* Odd field */
	bkr_frame_encode_field(stream, source, key, 2);	/* Even field */
	ring_unlock(source->ring);

	return(0);
}


/*
 * Ensure the ring has been filled to a video frame bounday, and check if it
 * has drained.
 */

static int flush(struct bkr_stream_t *stream)
{
	return(bkr_stream_fill_to(stream, 2 * stream->capacity, BKR_FILLER) < 0 ? -EAGAIN : bytes_in_ring(stream->ring) ? bkr_source_write_status(stream) : 0);
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
	struct ring  *ring = stream->ring;
	int  need_callback;

	ring_lock(ring);
	while(bkr_frame_find_field(stream, sector_key) > 0) {
		if(_space_in_ring(ring) < stream->capacity)
			break;
		bkr_frame_decode_field(stream);
	}
	need_callback = _bytes_in_ring(ring);
	ring_unlock(ring);

	if(need_callback)
		bkr_stream_do_callback(stream);
}


static void write_callback(struct bkr_stream_t *stream)
{
	struct ring  *ring = stream->ring;
	int  need_callback;

	ring_lock(ring);
	while(_bytes_in_ring(ring) >= 2*stream->capacity)
		if(bkr_frame_encode(stream, sector_key) < 0)
			break;
	need_callback = _space_in_ring(ring);
	ring_unlock(ring);

	if(need_callback)
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

static const struct bkr_stream_ops_t  stream_ops = {
	.new = new,
	.start = start,
	.release = release,
	.read = bkr_simple_stream_read,
	.write = bkr_simple_stream_write,
};

static struct bkr_stream_t *new(struct bkr_stream_t *source, int mode, const bkr_format_info_t *fmt)
{
	int  field_capacity = fmt->active_size - fmt->key_length;
	struct bkr_stream_t  *stream;
	bkr_frame_private_t  *private;

	if(!source)
		goto no_source;

	stream = malloc(sizeof(*stream));
	private = malloc(sizeof(*private));
	if(!stream || !private)
		goto no_stream;
	stream->ring = malloc(sizeof(*stream->ring));
	if(!stream->ring)
		goto no_stream;

	if(!ring_alloc(stream->ring, FRAME_BUFFER_SIZE - FRAME_BUFFER_SIZE % (2*field_capacity)))
		goto no_ring_buffer;

	stream->fmt = *fmt;
	stream->source = source;
	bkr_stream_set_callback(stream, NULL, NULL);
	stream->mode = mode;
	stream->direction = BKR_STOPPED;
	stream->ops = stream_ops;
	stream->capacity = field_capacity;
	stream->timeout = FRAME_TIMEOUT_MULT * source->timeout;
	stream->private = private;

	*private = (bkr_frame_private_t) {
		.frame_warnings = 0,
		.worst_key = stream->fmt.key_length,
		.best_nonkey = 0,
		.last_field_offset = -1,
		.smallest_field = ~0,
		.largest_field = 0,
	};
	
	return(stream);

no_ring_buffer:
	free(stream->ring);
no_stream:
	free(stream);
	free(private);
	source->ops.release(source);
no_source:
	return(NULL);
}


/*
 * ========================================================================
 *
 *                         CODEC Initialization
 *
 * ========================================================================
 */

const struct bkr_stream_ops_t *bkr_frame_codec_init(void)
{
	return(&stream_ops);
}
