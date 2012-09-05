/*
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

#ifndef _BKR_PROC_IO_H
#define _BKR_PROC_IO_H

#include <backer.h>

/* FIXME: some of these no long match the data from the codecs */
struct bkr_proc_status_t
	{
	char  state[sizeof("SUSPENDED\0")];
	unsigned int  mode;
	int  sector_number;
	unsigned int  total_errors;
	unsigned int  worst_block;
	unsigned int  parity;
	unsigned int  recent_block;
	unsigned int  bad_sectors;
	unsigned int  bad_sector_runs;
	unsigned int  frame_errors;
	unsigned int  underrun_errors;
	unsigned int  worst_key;
	unsigned int  max_key_weight;
	unsigned int  best_nonkey;
	unsigned int  smallest_field;
	unsigned int  largest_field;
	unsigned int  bytes_in_buffer;
	unsigned int  buffer_size;
	};

int bkr_proc_read_status(FILE *, struct bkr_proc_status_t *);
int bkr_proc_write_status(FILE *, struct bkr_proc_status_t *);
int bkr_proc_read_format_table(FILE *, bkr_format_info_t *);
int bkr_proc_write_format_table(FILE *, bkr_format_info_t *);

#endif	/* _BKR_PROC_IO_H */
