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

#ifdef __KERNEL__
#include <linux/errno.h>
#else
#include <errno.h>
#endif

#include <bkr_stream.h>

/*
 * ========================================================================
 *
 *                           Helper Functions
 *
 * ========================================================================
 */

int bkr_stream_bytes(struct bkr_stream_t *stream)
{
	struct ring *ring = NULL;
	int  bytes;

	for(bytes = 0; stream; stream = stream->source)
		if(ring != stream->ring)
			bytes += bytes_in_ring(ring = stream->ring);

	return(bytes);
}


int bkr_stream_size(struct bkr_stream_t *stream)
{
	struct ring *ring = NULL;
	int  size;

	for(size = 0; stream; stream = stream->source)
		if(ring != stream->ring)
			size += (ring = stream->ring)->size;

	return(size);
}


int bkr_source_read_status(struct bkr_stream_t *stream)
{
	int  result = stream->source ? stream->source->ops.read(stream->source) : -EAGAIN;

	return(result <= 0 ? result : -EAGAIN);
}


int bkr_source_write_status(struct bkr_stream_t *stream)
{
	int  result = stream->source ? stream->source->ops.write(stream->source) : -EAGAIN;

	return(result < 0 ? result : -EAGAIN);
}


int bkr_simple_stream_read(struct bkr_stream_t *stream)
{
	int  bytes = bytes_in_ring(stream->ring);

	return(bytes ? bytes : bkr_source_read_status(stream));
}


int bkr_simple_stream_write(struct bkr_stream_t *stream)
{
	int  space = space_in_ring(stream->ring);

	return(space ? space : bkr_source_write_status(stream));
}


static int _ring_fill_to(struct ring *ring, int interval, unsigned char data)
{
	int  count = ring->head % interval;

	if(count) {
		count = interval - count;
		if(_space_in_ring(ring) >= count) {
			memset_ring(ring, data, count);
			count = 0;
		}
	}

	return(count);
}


static int ring_fill_to(struct ring *ring, int interval, unsigned char data)
{
	int  result;

	ring_lock(ring);
	result = _ring_fill_to(ring, interval, data);
	ring_unlock(ring);
	return(result);
}


int bkr_stream_fill_to(struct bkr_stream_t *stream, int interval, unsigned char data)
{
	return(ring_fill_to(stream->ring, interval, data) ? -EAGAIN : 0);
}
