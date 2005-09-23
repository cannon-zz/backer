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

#ifndef _BKR_FRAME_H
#define _BKR_FRAME_H

#include <bkr_stream.h>
#include <bkr_ring_buffer.h>


/*
 * ========================================================================
 *
 *                                PRIVATE DATA
 *
 * ========================================================================
 */

typedef struct {
	unsigned int  frame_warnings;	/* excessive inter-field gaps */
	unsigned int  worst_key;
	unsigned int  best_nonkey;
	size_t  last_field_offset;	/* last field's ring offset */
	unsigned int  smallest_field;	/* smallest video field so far */
	unsigned int  largest_field;	/* largest video field so far */
} bkr_frame_private_t;


/*
 * ========================================================================
 *
 *                                 FUNCTIONS 
 *
 * ========================================================================
 */

const struct bkr_stream_ops_t *bkr_frame_codec_init(void);


#endif /* _BKR_FRAME_H */
