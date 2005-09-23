/*
 * A ring buffer implementation.
 *
 * Copyright (C) 2002  Kipp C. Cannon
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/*
 * The head is the next location to be written.
 * The tail is the next location to be read.
 *
 * ``empty'' and ``full'' rings are defined by:
 *
 *	tail == head     ---> bytes_in_ring() == 0
 *	head == tail - 1 ---> space_in_ring() == 0
 *
 * The following relationships are true:
 *
 *	0 <= space_in_ring(), bytes_in_ring() < size
 *	space_in_ring() + bytes_in_ring() == size - 1
 */

#ifndef _BKR_RING_BUFFER_H
#define _BKR_RING_BUFFER_H

#ifdef __KERNEL__
#include <linux/spinlock.h>
#include <linux/types.h>
#else
#include <sys/types.h>
#include <pthread.h>
#endif


typedef unsigned char  ring_data_t;
typedef ssize_t  ring_soffset_t;

struct ring {
	ring_data_t  *buffer;
	size_t  size;
	size_t  head;
	size_t  tail;
#ifdef __KERNEL__
	unsigned long  flags;
	spinlock_t  lock;
#else
	pthread_mutex_t  lock;
#endif
};


/*
 * Aquire and release ring locks
 */

#ifdef __KERNEL__

static void ring_lock(struct ring *ring)
{
	spin_lock_irqsave(&ring->lock, ring->flags);
}

static void ring_unlock(struct ring *ring)
{
	spin_unlock_irqrestore(&ring->lock, ring->flags);
}

#else

static void ring_lock(struct ring *ring)
{
	pthread_mutex_lock(&ring->lock);
}

static void ring_unlock(struct ring *ring)
{
	pthread_mutex_unlock(&ring->lock);
}

#endif


/*
 * Reset the head and tail offsets of a ring buffer to 0.
 */

static void ring_reset(struct ring *ring)
{
	ring->head = ring->tail = 0;
}


/*
 * Ring offset arithmetic.  The _inc and _dec macros are slightly more
 * efficient in the increment/decrement + assignment case.
 */

#if 1
static size_t ring_offset_add(struct ring *ring, size_t x, size_t y)
{
	const size_t z = x + y;
	return(z >= ring->size ? z - ring->size : z);
}
#else
#define ring_offset_add(ring,x,y) ({           \
	const size_t _z = (x) + (y);    \
	const struct ring *_r = ring;          \
	_z >= _r->size ? _z - _r->size : _z; })
#endif

#if 1
static size_t ring_offset_sub(struct ring *ring, size_t x, size_t y)
{
	const ring_soffset_t z = x - y;
	return(z < 0 ? z + ring->size : z);
}
#else
#define ring_offset_sub(ring,x,y) ({         \
	const ring_soffset_t _z = (x) - (y); \
	const struct ring *_r = ring;        \
	_z < 0 ? _z + _r->size : _z; })
#endif

#if 0
static void ring_offset_inc(struct ring *ring, size_t *offset, int n)
{
	*offset = ring_offset_add(ring, *offset, n);
}
#else
#define ring_offset_inc(ring,offset,n) \
	do { if(((offset) += (n)) >= (ring)->size) (offset) -= (ring)->size; } while(0)
#endif

#if 0
static void ring_offset_dec(struct ring *ring, size_t *offset, int n)
{
	*offset = ring_offset_sub(ring, *offset, n);
}
#else
#define ring_offset_dec(ring,offset,n) \
	do { if((ring_soffset_t)((offset) -= (n)) < 0) (offset) += (ring)->size; } while(0)
#endif

static void _ring_fill(struct ring *ring, size_t n)
{
	ring_offset_inc(ring, ring->head, n);
}

static void ring_fill(struct ring *ring, size_t n)
{
	ring_lock(ring);
	_ring_fill(ring, n);
	ring_unlock(ring);
}


static void _ring_drain(struct ring *ring, size_t n)
{
	ring_offset_inc(ring, ring->tail, n);
}

static void ring_drain(struct ring *ring, size_t n)
{
	ring_lock(ring);
	_ring_drain(ring, n);
	ring_unlock(ring);
}


/*
 * Determine the space and bytes available in a ring buffer.  The
 * definition of the ring_offset_sub() macro allows space_in_ring() to work
 * without an additional wrap-around check for the computation of tail - 1.
 */

static size_t _space_in_ring(struct ring *ring)
{
	return(ring_offset_sub(ring, ring->tail - 1, ring->head));
}

static size_t space_in_ring(struct ring *ring)
{
	size_t  result;

	ring_lock(ring);
	result = _space_in_ring(ring);
	ring_unlock(ring);

	return(result);
}


static size_t _bytes_in_ring(struct ring *ring)
{
	return(ring_offset_sub(ring, ring->head, ring->tail));
}

static size_t bytes_in_ring(struct ring *ring)
{
	size_t  result;

	ring_lock(ring);
	result = _bytes_in_ring(ring);
	ring_unlock(ring);

	return(result);
}


static int _ring_is_full(struct ring *ring)
{
	return(ring_offset_add(ring, ring->head, 1) == ring->tail);
}

static int ring_is_full(struct ring *ring)
{
	int  result;

	ring_lock(ring);
	result = _ring_is_full(ring);
	ring_unlock(ring);

	return(result);
}


static int _ring_is_empty(struct ring *ring)
{
	return(ring->head == ring->tail);
}

static int ring_is_empty(struct ring *ring)
{
	int  result;

	ring_lock(ring);
	result = _ring_is_empty(ring);
	ring_unlock(ring);

	return(result);
}


/*
 * Function prototypes
 */

void *ring_alloc(struct ring *, size_t);
void ring_free(struct ring *);
size_t memset_ring(struct ring *, ring_data_t, size_t);
size_t memcpy_to_ring(struct ring *, void *, size_t);
size_t memcpy_from_ring(void *, struct ring *, size_t);
size_t memcpy_to_ring_from_ring(struct ring *, struct ring *, size_t);
#ifdef __KERNEL__
size_t copy_to_user_from_ring(char *, struct ring *, size_t);
size_t copy_to_ring_from_user(struct ring *, const char *, size_t);
#endif /* __KERNEL__ */

#endif /* _BKR_RING_BUFFER_H */
