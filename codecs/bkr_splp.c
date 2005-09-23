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
#include <linux/module.h>
#include <linux/slab.h>
static void *malloc(size_t size)  { return(kmalloc(size, GFP_KERNEL)); }
static void free(void *ptr) { kfree(ptr); }
#else
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#endif

#include <backer.h>
#include <bkr_bytes.h>
#include <bkr_stream.h>
#include <bkr_splp.h>
#include <bkr_splp_randomize.h>


/*
 * ========================================================================
 *
 *                              PARAMETERS
 *
 * ========================================================================
 */

/*
 * 28 bit sector number = about 1243 hours of NTSC, or 1491 hours of PAL.
 */

#define  SPLP_TIMEOUT_MULT       20
#define  BKR_SECTOR_NUMBER_BITS  28
#define  BKR_LOW_USED_BITS       4
#if BKR_SECTOR_NUMBER_BITS + BKR_LOW_USED_BITS != 32
#error Sector header size not equal to 32 bits.
#endif
#if (BKR_SECTOR_NUMBER_BITS + BKR_LOW_USED_BITS)/8 != BKR_SIZEOF_SECTOR_HEADER
#error BKR_SIZEOF_SECTOR_HEADER is wrong.
#endif


/*
 * ========================================================================
 *
 *                        Sector Header Handling
 *
 * ========================================================================
 */

#if __BKR_BYTE_ORDER == __LITTLE_ENDIAN

typedef struct {
	int32_t  number : BKR_SECTOR_NUMBER_BITS;
	u_int32_t  low_used : BKR_LOW_USED_BITS;
} bkr_sector_header_t;

#else

typedef struct {
	u_int32_t  low_used : BKR_LOW_USED_BITS;
	int32_t  number : BKR_SECTOR_NUMBER_BITS;
} bkr_sector_header_t;

#endif /* __BKR_BYTE_ORDER */

/* FIXME: the next two functions use aliasing, which is not guaranteed to
 * work on all platforms.  There are tricks to get it to work, but I don't
 * know what they are... */

static bkr_sector_header_t bkr_splp_get_sector_header(const struct bkr_stream_t *stream)
{
	union {
		u_int32_t  as_int;
		bkr_sector_header_t  as_header;
	} header;

	header.as_int = get_unaligned((u_int32_t *) (stream->ring->buffer + stream->ring->tail + stream->capacity));
	header.as_int = __le32_to_cpu(header.as_int);

	return(header.as_header);
}


static void bkr_splp_put_sector_header(const struct bkr_stream_t *stream, int length)
{
	bkr_splp_private_t  *private = stream->private;
	union {
		u_int32_t  as_int;
		bkr_sector_header_t  as_header;
	} header = {
		.as_header = {
			.number = private->sector_number,
			.low_used = length,
		},
	};

	header.as_int = __cpu_to_le32(header.as_int);
	memcpy_to_ring(stream->ring, &header.as_header, sizeof(header));
}


static unsigned int bkr_splp_high_used(const struct bkr_stream_t *stream)
{
	return(*(stream->ring->buffer + stream->ring->tail + stream->capacity - 1));
}


static unsigned int bkr_splp_encode_sector_length(unsigned int length)
{
	return(length + (length / 15) + 1);
}


static unsigned int bkr_splp_decode_sector_length(unsigned int high, unsigned int low)
{
	return((high * 15) + low - 1);
}


/*
 * ========================================================================
 *
 *                              SP/LP CODEC
 *
 * ========================================================================
 */

/*
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
 * each irrecoverable sector.
 */

static int correct_sector(struct bkr_stream_t *stream)
{
	bkr_splp_private_t  *private = stream->private;
	struct ring  *ring = stream->ring;
	unsigned char *data = ring->buffer + ring->tail;
	unsigned char *parity = data + stream->fmt.data_size;
	int  block, bytes_corrected;
	int  result = 0;

	/* FIXME: remove this to enable error correction */
	return(0);

	private->header_is_good = 1;
	for(block = 0; block < stream->fmt.interleave; block++) {
		bytes_corrected = reed_solomon_decode(parity + block, data + block, 0, private->rs_format);
		/* block is uncorrectable? */
		if(bytes_corrected < 0) {
			result = -ENODATA;
			/* block contains header? */
			if(block >= stream->fmt.interleave - BKR_SIZEOF_SECTOR_HEADER)
				private->header_is_good = 0;
			continue;
		}
		private->errors.bytes_corrected += bytes_corrected;
		if(bytes_corrected > private->errors.worst_block)
			private->errors.worst_block = bytes_corrected;
		if(bytes_corrected > private->errors.recent_block)
			private->errors.recent_block = bytes_corrected;
	}

	return(result);
}


static int get_next_good_sector(struct bkr_stream_t *stream)
{
	bkr_splp_private_t  *private = stream->private;
	struct ring  *ring = stream->ring;
	bkr_sector_header_t  header;
	int  count;

	while(1) {
		/*
		 * Advance to next sector boundary.
		 */

		count = stream->source->capacity;
		count -= ring->tail % count;
		if(_bytes_in_ring(ring) < count + stream->source->capacity)
			return(-EAGAIN);
		_ring_drain(ring, count);
		private->decode_head = ring->tail;

		/*
		 * Perform error correction.  If this fails, move to the
		 * next sector.
		 */

		if(correct_sector(stream) < 0) {
			private->errors.bad_sectors++;
			continue;
		}

		/*
		 * Extract sector header.
		 */

		header = bkr_splp_get_sector_header(stream);
		private->decoded_number = header.number;

		/*
		 * If a BOR sector, reset error counters and move to next
		 * sector.
		 */

		if(header.number < 0) {
			private->errors = BKR_ERRORS_INITIALIZER;
			continue;
		}

		/*
		 * If a duplicate, move to next sector.
		 */

		if(header.number <= private->sector_number) {
			private->errors.duplicate_runs += private->not_underrunning;
			private->not_underrunning = 0;
			continue;
		}

		/*
		 * Sector is the one we want.
		 */

		private->not_underrunning = 1;
		break;
	}

	/*
	 * Determine the sector length and de-randomize it (it is safe for
	 * the randomizer to go past the end of the sector by a few bytes).
	 */

	if(header.low_used)
		count = bkr_splp_decode_sector_length(bkr_splp_high_used(stream), header.low_used);
	else
		count = stream->capacity;
	bkr_splp_sector_randomize(ring->buffer + ring->tail, count, header.number);
	private->decode_head = ring_offset_add(ring, ring->tail, count);

	return(count);
}


/*
 * Writes the current sector to the I/O buffer and resets for the next one.
 * Returns 0 on success, < 0 on error in which case a retry can be
 * attempted by simply re-calling this function.
 */

static int commit_sector(struct bkr_stream_t *stream)
{
	bkr_splp_private_t  *private = stream->private;
	struct ring  *ring = stream->ring;
	size_t  data_start;
	int  block, data_length;

	ring_lock(ring);

	data_length = ring->head % stream->source->capacity;
	data_start = ring->head - data_length;

	if(_space_in_ring(ring) < stream->source->capacity - data_length) {
		ring_unlock(ring);
		return(-EAGAIN);
	}

	/*
	 * Randomize the data (it is safe for the randomizer to go past the
	 * end of the data area by a few bytes).
	 */

	bkr_splp_sector_randomize(ring->buffer + data_start, data_length, private->sector_number);

	/*
	 * Pad unused space, encode the data length, insert the header.
	 */

	if(data_length < stream->capacity) {
		memset_ring(ring, BKR_FILLER, stream->capacity - data_length - 1);
		data_length = bkr_splp_encode_sector_length(data_length);
		memset_ring(ring, data_length >> BKR_LOW_USED_BITS, 1);
		bkr_splp_put_sector_header(stream, data_length);
	} else
		bkr_splp_put_sector_header(stream, 0);

	/*
	 * Generate parity bytes.
	 */

	for(block = 0; block < stream->fmt.interleave; block++)
		reed_solomon_encode(ring->buffer + ring->head + block, ring->buffer + data_start + block, private->rs_format);
	_ring_fill(ring, stream->fmt.parity_size);

	/*
	 * Reset for the next sector.
	 */
	
	private->sector_number++;

	ring_unlock(ring);
	return(stream->capacity);
}


/*
 * Generate EOR mark.
 * FIXME: don't busy wait.
 */

static void write_eor(struct bkr_stream_t *stream)
{
	struct ring  *ring = stream->ring;
	int  sectors;

	/* number of sectors in the EOR mark */
	sectors = bkr_fields_per_second(stream->mode) * EOR_LENGTH;

	/* add 1 if the last data sector hasn't been written yet */
	ring_lock(ring);
	sectors += (ring->head % stream->source->capacity) > 0;
	ring_unlock(ring);

	for(; sectors > 0; sectors--)
		while((commit_sector(stream) == -EAGAIN) && (bkr_source_write_status(stream) != -EAGAIN));
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
	bkr_splp_private_t  *private = stream->private;
	struct bkr_stream_t  *source = stream->source;
	int  result;

	bkr_stream_set_callback(source, stream->callback, stream->callback_data);
	if(direction == BKR_READING)
		private->sector_number = -1;

	result = source->ops.start(source, direction);
	if(result >= 0)
		stream->direction = direction;

	return(result);
}


static int read(struct bkr_stream_t *stream)
{
	bkr_splp_private_t  *private = stream->private;
	struct ring  *ring = stream->ring;
	int  result;

	if(private->decoded_number > private->sector_number + 1) {
		private->sector_number++;
		return(-ENODATA);
	}

	ring_lock(ring);
	result = ring_offset_sub(ring, private->decode_head, ring->tail);
	if(result)
		private->sector_number = private->decoded_number;
	else {
		result = get_next_good_sector(stream);
		if(result >= 0) {
			private->sector_number++;
			if(private->decoded_number > private->sector_number) { 
				private->errors.lost_runs++;
				result = -ENODATA;
			}
		}
	}
	ring_unlock(ring);

	if(result == -EAGAIN)
		result = bkr_source_read_status(stream);

	return(result);
}


static int write(struct bkr_stream_t *stream)
{
	struct ring  *ring = stream->ring;
	int  result;

	ring_lock(ring);
	result = stream->capacity - ring->head % stream->source->capacity;
	ring_unlock(ring);
	if(!result) {
		result = commit_sector(stream);
		if(result == -EAGAIN)
			result = bkr_source_write_status(stream);
	}

	return(result);
}


static int write_bor(struct bkr_stream_t *stream)
{
	bkr_splp_private_t  *private = stream->private;

	while(private->sector_number < 0)
		if(commit_sector(stream) == -EAGAIN)
			return(bkr_source_write_status(stream));

	stream->ops.write = write;
	return(write(stream));
}


static int release(struct bkr_stream_t *stream)
{
	bkr_splp_private_t  *private = stream->private;
	struct bkr_stream_t  *source = stream->source;
	int  result;

	if(stream->direction == BKR_WRITING)
		write_eor(stream);
	stream->direction = BKR_STOPPED;

	result = source->ops.release(source);
	if(result < 0)
		return(result);
	reed_solomon_codec_free(&private->rs_format);
	free(private);
	free(stream);
	return(0);
}


static struct bkr_stream_t *new(struct bkr_stream_t *, int, const bkr_format_info_t *);

static const struct bkr_stream_ops_t  stream_ops = {
	.new = new,
	.start = start,
	.release = release,
	.read = read,
	.write = write_bor,
};

static struct bkr_stream_t *new(struct bkr_stream_t *source, int mode, const bkr_format_info_t *fmt)
{
	struct bkr_stream_t  *stream;
	bkr_splp_private_t  *private;

	if(!source)
		goto no_source;

	stream = malloc(sizeof(*stream));
	private = malloc(sizeof(*private));
	if(!stream || !private)
		goto no_stream;

	if(reed_solomon_codec_new((fmt->data_size + fmt->parity_size) / fmt->interleave, fmt->data_size / fmt->interleave, fmt->interleave, &private->rs_format) < 0)
		goto no_rs_codec;

	stream->source = source;
	stream->ring = source->ring;
	stream->fmt = *fmt;
	stream->ops = stream_ops;
	stream->mode = mode;
	stream->direction = BKR_STOPPED;
	bkr_stream_set_callback(stream, NULL, NULL);
	stream->capacity = bkr_sector_capacity(stream->fmt);
	stream->timeout = SPLP_TIMEOUT_MULT * stream->source->timeout;
	stream->private = private;

	private->sector_number = -bkr_fields_per_second(mode) * BOR_LENGTH;
	private->decoded_number = -1;
	private->not_underrunning = 1;
	private->errors = BKR_ERRORS_INITIALIZER;
	private->decode_head = stream->ring->tail;

	return(stream);

no_rs_codec:
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
 *                               CODEC API
 *
 * ========================================================================
 */

const struct bkr_stream_ops_t *bkr_splp_codec_init(void)
{
	galois_field_init(GF00256);

	return(&stream_ops);
}
