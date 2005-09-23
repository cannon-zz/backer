/*
 * Implementation of a Backer device layer on stdin/stdout.
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
#include <malloc.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <backer.h>
#include <bkr_ring_buffer.h>
#include <bkr_stream.h>

#define  BKR_STDIO_RING_SIZE  16384

typedef struct {
	pthread_t  io_thread;
	pthread_cond_t  ring_activity;
	int  eof;
} bkr_stdio_private_t;


/*
 * ========================================================================
 *
 *                     Low-Level File I/O With Rings
 *
 * ========================================================================
 */

ssize_t read_ring(int fd, struct ring *ring, size_t count)
{
	size_t  result = ring->size - ring->head;

	count %= ring->size;

	if(result <= count) {
		ring->head += result = read(fd, ring->buffer + ring->head, result);
		if(ring->head == ring->size)
			result += ring->head = read(fd, ring->buffer, count -= result);
	} else
		ring->head += result = read(fd, ring->buffer + ring->head, count);
	return(result);
}


ssize_t write_ring(int fd, struct ring *ring, size_t count)
{
	size_t  result = ring->size - ring->tail;

	count %= ring->size;

	if(result <= count) {
		ring->tail += result = write(fd, ring->buffer + ring->tail, result);
		if(ring->tail == ring->size)
			result += ring->tail = write(fd, ring->buffer, count -= result);
	} else
		ring->tail += result = write(fd, ring->buffer + ring->tail, count);
	return(result);
}


/*
 * ========================================================================
 *
 *                   "High"-Level File I/O With Rings
 *
 * ========================================================================
 */

/*
 * Ring equivalents (almost) of fread() and fwrite().  These functions
 * attempt to read/write exactly nmemb chunks of exactly size bytes from/to
 * the file fd.  Unlike the standard library versions, these do not employ
 * any additional buffering beyond that of the ring itself (thus they do
 * not use a FILE * argument).
 */

size_t fread_ring(struct ring *ring, size_t size, size_t nmemb, int fd)
{
	size_t  memb, i;
	ssize_t  result;

	for(memb = 0; memb < nmemb; memb++)
		for(i = 0; i < size; i += result)
			if((result = read_ring(fd, ring, size - i)) <= 0) {
				if(result == 0) {
					/* EOF */
				} else {
					/* error */
				}
				return(memb);
			}

	return(memb);
}


size_t fwrite_ring(struct ring *ring, size_t size, size_t nmemb, int fd)
{
	size_t  memb, i;
	ssize_t  result;

	for(memb = 0; memb < nmemb; memb++)
		for(i = 0; i < size; i += result)
			if((result = write_ring(fd, ring, size - i)) < 0) {
				/* error */
				return(memb);
			}

	return(memb);
}


/*
 * ========================================================================
 *
 *                              I/O Thread
 *
 * ========================================================================
 */

static void io_thread_do_read(struct bkr_stream_t *stream, bkr_stdio_private_t *private)
{
	struct ring  *ring = stream->ring;
	int  count;

	ring_lock(ring);
	count = _space_in_ring(ring) / stream->capacity;
	if(!count && 0) {
		pthread_cond_wait(&private->ring_activity, &ring->lock);
	} else if(fread_ring(ring, stream->capacity, count, STDIN_FILENO) < count)
		private->eof = 1;	/* treat all read errors as EOF */
	ring_unlock(ring);
}


static void io_thread_do_write(struct bkr_stream_t *stream, bkr_stdio_private_t *private)
{
	struct ring  *ring = stream->ring;
	int  count;
 
	ring_lock(ring);
	count = _bytes_in_ring(ring) / stream->capacity;
	if(!count && 0)
		pthread_cond_wait(&private->ring_activity, &ring->lock);
	else
		fwrite_ring(ring, stream->capacity, count, STDOUT_FILENO);
	ring_unlock(ring);
}


static void *io_thread(void *data)
{
	struct bkr_stream_t  *stream = data;
	bkr_stdio_private_t  *private = (bkr_stdio_private_t *) stream->private;
	sigset_t  signals;

	sigemptyset(&signals);
	sigaddset(&signals, SIGINT);
	pthread_sigmask(SIG_BLOCK, &signals, NULL);

	while(1) {
		switch(stream->direction) {
			case BKR_READING:
			io_thread_do_read(stream, private);
			break;

			case BKR_WRITING:
			io_thread_do_write(stream, private);
			break;

			default:
			pthread_exit(NULL);
		}
		bkr_stream_do_callback(stream);
	}
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
	bkr_stdio_private_t  *private = (bkr_stdio_private_t *) stream->private;

	stream->direction = direction;
	pthread_create(&private->io_thread, NULL, io_thread, stream);

	return(0);
}


static int stream_read(struct bkr_stream_t *stream)
{
	bkr_stdio_private_t  *private = (bkr_stdio_private_t *) stream->private;
	int  bytes;

	ring_lock(stream->ring);
	bytes = _bytes_in_ring(stream->ring);
	pthread_cond_signal(&private->ring_activity);
	ring_unlock(stream->ring);

	return(bytes ? bytes : (private->eof ? 0 : -EAGAIN));
}


static int stream_write(struct bkr_stream_t *stream)
{
	bkr_stdio_private_t  *private = (bkr_stdio_private_t *) stream->private;
	int  frame_count;

	ring_lock(stream->ring);
	frame_count = _space_in_ring(stream->ring) / stream->capacity;
	pthread_cond_signal(&private->ring_activity);
	ring_unlock(stream->ring);

	return(frame_count ? frame_count * stream->capacity : -EAGAIN);
}


static int flush(struct bkr_stream_t *stream)
{
	struct ring  *ring = stream->ring;
	int  count;

	ring_lock(ring);
	count = ring->head % stream->capacity;
	ring_unlock(ring);
	if(count) {
		count = stream->capacity - count;
		if(count > stream_write(stream))
			return(-EAGAIN);
		ring_lock(ring);
		memset_ring(ring, 0, count);
		ring_unlock(ring);
	}
	return(stream_read(stream) > 0 ? -EAGAIN : 0);
}


static int release(struct bkr_stream_t *stream)
{
	bkr_stdio_private_t  *private = (bkr_stdio_private_t *) stream->private;
	bkr_direction_t  direction = stream->direction;

	if(direction != BKR_STOPPED) {
		if(direction == BKR_WRITING)
			while(flush(stream) == -EAGAIN);
		stream->direction = BKR_STOPPED;
		ring_lock(stream->ring);
		pthread_cond_signal(&private->ring_activity);
		ring_unlock(stream->ring);
		pthread_join(private->io_thread, NULL);
	}

	pthread_cond_destroy(&private->ring_activity);
	ring_free(stream->ring);
	free(stream->ring);
	free(private);
	free(stream);
	return(0);
}


static struct bkr_stream_t *new(struct bkr_stream_t *, int, const bkr_format_info_t *);

static struct bkr_stream_ops_t  stdio_ops = {
	.new = new,
	.start = start,
	.release = release,
	.read = stream_read,
	.write = stream_write,
};

static struct bkr_stream_t *new(struct bkr_stream_t *null, int mode, const bkr_format_info_t *fmt)
{
	struct bkr_stream_t  *stream = malloc(sizeof(*stream));
	bkr_stdio_private_t  *private = malloc(sizeof(*private));

	if(!stream || !private)
		goto no_stream;
	stream->ring = malloc(sizeof(*stream->ring));
	if(!stream->ring)
		goto no_stream;

	if(!ring_alloc(stream->ring, BKR_STDIO_RING_SIZE - BKR_STDIO_RING_SIZE % fmt->frame_size))
		goto no_ring_buffer;

	stream->fmt = *fmt;
	stream->source = NULL;
	bkr_stream_set_callback(stream, NULL, NULL);
	stream->mode = mode;
	stream->direction = BKR_STOPPED;
	stream->ops = stdio_ops;
	stream->private = private;
	stream->capacity = fmt->frame_size;
	stream->timeout = 0;	/* not used in user space */

	pthread_cond_init(&private->ring_activity, NULL);
	private->eof = 0;

	return(stream);

no_ring_buffer:
	free(stream->ring);
no_stream:
	free(stream);
	free(private);
	return(NULL);
}


/*
 * ========================================================================
 *
 *                         Stream Initialization
 *
 * ========================================================================
 */

struct bkr_stream_ops_t *bkr_stdio_dev_init(void)
{
	return(&stdio_ops);
}
