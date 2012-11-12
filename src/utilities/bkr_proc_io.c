/*
 * Routines for parsing the /proc interface.
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

#include <stdio.h>
#include <sys/poll.h>
#include <backer.h>
#include <bkr_proc_io.h>

/*
 * seek_to_start()
 *
 * Seek to the start of a file.  Ignore errors so we can handle pipes.
 */

static void seek_to_start(FILE *file)
{
	fseek(file, 0, SEEK_SET);
}


/*
 * ready_for_read(), ready_for_write()
 *
 * Test the readability and writeability of a file.
 */

static int ready_for(FILE *file, int events)
{
	struct pollfd pollfd = {
		.fd = fileno(file),
		.events = events
	};

	return poll(&pollfd, 1, 0) > 0;
}

static int ready_for_read(FILE *file)
{
	return ready_for(file, POLLIN);
}

static int ready_for_write(FILE *file)
{
	return ready_for(file, POLLOUT);
}


/*
 * bkr_proc_read_status()
 *
 * Parse the contents of the /proc status file.  On success the return
 * value is 0, otherwise < 0 is returned and errno is set to indicate the
 * error.
 */

int bkr_proc_read_status(FILE *file, struct bkr_proc_status_t *status)
{
	int  result;

	do {
		seek_to_start(file);
		if(!ready_for_read(file))
			return -1;
		result = fscanf(file,
		                "Current State   : %s\n" \
		                "%*17c%u\n" \
		                "%*17c%u / %u\n" \
		                "%*17c%u\n" \
		                "%*17c%u\n" \
		                "%*17c%d\n" \
		                "%*17c%u\n" \
		                "%*17c%u / %u\n" \
		                "%*17c%u\n" \
		                "%*17c%u\n" \
		                "%*17c%u\n" \
		                "%*17c%u / %u\n" \
		                "%*17c%u\n" \
		                "%*17c%u\n" \
		                "%*17c%u\n",
		                status->state,
		                &status->mode,
		                &status->bytes_in_buffer, &status->buffer_size,
		                &status->sector_number,
		                &status->bad_sectors,
		                &status->bad_sector_runs,
		                &status->total_errors,
		                &status->worst_block, &status->parity,
		                &status->recent_block,
		                &status->frame_errors,
		                &status->underrun_errors,
		                &status->worst_key, &status->max_key_weight,
		                &status->best_nonkey,
		                &status->smallest_field,
		                &status->largest_field);
		if(result == EOF)
			return -1;
	} while(result < 18);

	return 0;
}


/*
 * bkr_proc_write_status()
 *
 * Using the information found in the stream and device structures,
 * write a status report to file in the same format as the /proc status
 * file.  On success the return value is 0, otherwise < 0 is returned and
 * errno is set to indicate the error.
 *
 * NOTE that software which reads the /proc status interface generally
 * expects the source of the status information to reset the recent_block
 * field to 0 after each read-out.  Remember to do that!
 */

int bkr_proc_write_status(FILE *file, struct bkr_proc_status_t *status)
{
	seek_to_start(file);

	fprintf(file,
	        "Current State   : %s\n" \
	        "Current Mode    : %u\n" \
	        "I/O Buffer      : %u / %u\n" \
	        "Sector Number   : %+010d\n" \
	        "Bad Sectors     : %u\n" \
	        "Bad Sector Runs : %u\n" \
	        "Byte Errors     : %u\n" \
	        "In Worst Block  : %u / %u\n" \
	        "Recently        : %u\n" \
	        "Framing Errors  : %u\n" \
	        "Underflows      : %u\n" \
	        "Worst Key       : %u / %u\n" \
	        "Closest Non-Key : %u\n" \
	        "Smallest Field  : %u\n" \
	        "Largest Field   : %u\n",
	        status->state,
	        status->mode,
	        status->bytes_in_buffer, status->buffer_size,
	        status->sector_number,
	        status->bad_sectors,
	        status->bad_sector_runs,
	        status->total_errors,
	        status->worst_block, status->parity,
	        status->recent_block,
	        status->frame_errors,
	        status->underrun_errors,
	        status->worst_key, status->max_key_weight,
	        status->best_nonkey,
	        status->smallest_field,
	        status->largest_field);
	return 0;
}


/*
 * bkr_proc_read_format_table()
 *
 * Parse the contents of a file in the format of the /proc format_table
 * file.  On success the return value is 0, otherwise < 0 is returned and
 * errno is set to indicate the error.
 */

int bkr_proc_read_format_table(FILE *file, bkr_format_info_t *format)
{
	int  i;
	int  *format_as_ints = (int *) format;

	seek_to_start(file);

	if(!ready_for_read(file))
		return -1;

	for(i = 0; i < BKR_NUM_FORMATS*BKR_NUM_FORMAT_PARMS; i++)
		if(fscanf(file, "%d", &format_as_ints[i]) != 1)
			return -1;
	return 0;
}


/*
 * bkr_proc_write_format_table()
 *
 * Write the contents of the parameter table format to file in a manner
 * compatible with the contents of the /proc format_table file.  On success
 * the return value is 0, otherwise < 0 is returned and errno is set to
 * indicate the error.
 */

int bkr_proc_write_format_table(FILE *file, bkr_format_info_t *format)
{
	int  i;
	int  *format_as_ints = (int *) format;

	seek_to_start(file);

	for(i = 0; i < BKR_NUM_FORMATS*BKR_NUM_FORMAT_PARMS - 1; i++)
		if(fprintf(file, "%d\t", format_as_ints[i]) < 0)
			return -1;
	if(fprintf(file, "%d", format_as_ints[i]) < 0)
		return -1;
	return 0;
}
