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


int bkr_simple_stream_read(struct bkr_stream_t *stream)
{
	int bytes = bytes_in_ring(stream->ring);

	return bytes ? bytes : -EAGAIN;
}


int bkr_simple_stream_write(struct bkr_stream_t *stream)
{
	int space = space_in_ring(stream->ring);

	return space ? space : -EAGAIN;
}
