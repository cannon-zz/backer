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

#ifndef _BKR_SPLP_H
#define _BKR_SPLP_H

#include <bkr_stream.h>
#include <bkr_ring_buffer.h>
#include <rs.h>


/*
 * ========================================================================
 *
 *                              PARAMETERS
 *
 * ========================================================================
 */

#define  BOR_LENGTH                5       /* second(s) */
#define  EOR_LENGTH                1       /* second(s) */
#define  BKR_SIZEOF_SECTOR_HEADER  4       /* bytes */


/*
 * ========================================================================
 *
 *                                 TYPES
 *
 * ========================================================================
 */


/*
 * Stream state structure.
 */

typedef struct {
	int  bytes_corrected;  /* total bad bytes corrected */
	int  worst_block;      /* in worst Reed-Solomon block */
	int  recent_block;     /* in worst recent R-S block */
	int  bad_sectors;      /* total uncorrectable sectors */
	int  lost_runs;        /* runs of lost sectors */
	int  duplicate_runs;   /* runs of duplicated sectors */
} bkr_errors_t;

#define  BKR_ERRORS_INITIALIZER  ((bkr_errors_t) {	\
	.bytes_corrected = 0,	\
	.worst_block = 0,	\
	.recent_block = 0,	\
	.bad_sectors = 0,	\
	.lost_runs = 0,		\
	.duplicate_runs = 0,	\
})

typedef struct {
	int  sector_number;             /* sector (video field) number */
	int  decoded_number;            /* sector number read from tape */
	rs_format_t  rs_format;         /* Reed-Solomon format information */
	size_t  decode_head;            /* top end of current sector */
	int  not_underrunning;          /* 0 == prev. sector was a duplicate */
	int  header_is_good;            /* 0 == header is corrupted */
	bkr_errors_t  errors;           /* error counts */
} bkr_splp_private_t;


/*
 * ========================================================================
 *
 *                               FUNCTIONS
 *
 * ========================================================================
 */

const struct bkr_stream_ops_t *bkr_splp_codec_init(void);


static int bkr_sector_capacity(bkr_format_info_t fmt)
{
	return(fmt.data_size - BKR_SIZEOF_SECTOR_HEADER);
}

#endif /* _BKR_SPLP_H */
