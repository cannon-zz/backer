/*
 * A ring buffer implementation.
 *
 * This implementation is thread-safe when there is only one "producer"
 * thread and one "consumer" thread (i.e. if two treads try to write to the
 * buffer at once, there are problems).
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
 */

/*
 * Some functions for getting data into and out of ring buffers.
 *
 * Note that these functions assume that ring_data_t is a byte-sized type.
 * They also assume, for performance, that the number of elements being
 * manipulated is less than or equal to the ring size as only a single
 * wrap-around check is performed.  If this does not hold then buffer
 * overflows will occur!
 */

#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#include <bkr_ring_buffer.h>


/*
 * ring_new()
 *
 * Allocate a ring buffer.
 */

struct ring *ring_new(void *buffer, size_t size)
{
	struct ring *new = kmalloc(sizeof(*new), GFP_KERNEL);
	new->buffer = buffer;
	new->size = size;
	new->head = new->tail = 0;
	spin_lock_init(&new->lock);
	return new;
}


/*
 * ring_free()
 *
 * Free an allocated ring buffer.
 */

void ring_free(struct ring *ring)
{
	kfree(ring);
}


/*
 * memset_ring()
 *
 * Write n elements of val to a ring.  Returns the number of elements
 * written.
 */

size_t memset_ring(struct ring *dst, ring_data_t val, size_t n)
{
	size_t  remainder;

	remainder = dst->size - dst->head;
	if(remainder <= n) {
		memset(dst->buffer + dst->head, val, remainder);
		memset(dst->buffer, val, dst->head = n - remainder);
	} else {
		memset(dst->buffer + dst->head, val, n);
		dst->head += n;
	}

	return(n);
}


/*
 * memcpy_to_ring(), memcpy_from_ring()
 *
 * Copy data to a ring from a linear buffer or to a linear buffer from a
 * ring respectively.
 */

size_t memcpy_to_ring(struct ring *dst, void *src, size_t n)
{
	size_t  remainder;

	remainder = dst->size - dst->head;
	if(remainder <= n) {
		memcpy(dst->buffer + dst->head, src, remainder);
		memcpy(dst->buffer, src + remainder, dst->head = n - remainder);
	} else {
		memcpy(dst->buffer + dst->head, src, n);
		dst->head += n;
	}

	return(n);
}

size_t memcpy_from_ring(void *dst, struct ring *src, size_t n)
{
	size_t  remainder;

	remainder = src->size - src->tail;
	if(remainder <= n) {
		memcpy(dst, src->buffer + src->tail, remainder);
		memcpy(dst + remainder, src->buffer, src->tail = n - remainder);
	} else {
		memcpy(dst, src->buffer + src->tail, n);
		src->tail += n;
	}

	return(n);
}


/*
 * memcpy_to_ring_from_ring()
 *
 * Copy data from one ring to another.
 */

size_t memcpy_to_ring_from_ring(struct ring *dst, struct ring *src, size_t n)
{
	size_t  remainder;

	remainder = dst->size - dst->head;
	if(remainder <= n) {
		memcpy_from_ring(dst->buffer + dst->head, src, remainder);
		memcpy_from_ring(dst->buffer, src, dst->head = n - remainder);
	} else {
		memcpy_from_ring(dst->buffer + dst->head, src, n);
		dst->head += n;
	}

	return(n);
}


/*
 * Two additional functions for use in the kernel
 */


size_t copy_to_user_from_ring(char *dst, struct ring *src, size_t n)
{
	size_t  remainder;

	remainder = src->size - src->tail;
	if(remainder <= n) {
		copy_to_user(dst, src->buffer + src->tail, remainder);
		copy_to_user(dst + remainder, src->buffer, src->tail = n - remainder);
	} else {
		copy_to_user(dst, src->buffer + src->tail, n);
		src->tail += n;
	}

	return(n);
}


size_t copy_to_ring_from_user(struct ring *dst, const char *src, size_t n)
{
	size_t  remainder;

	remainder = dst->size - dst->head;
	if(remainder <= n) {
		copy_from_user(dst->buffer + dst->head, src, remainder);
		copy_from_user(dst->buffer, src + remainder, dst->head = n - remainder);
	} else {
		copy_from_user(dst->buffer + dst->head, src, n);
		dst->head += n;
	}

	return(n);
}
