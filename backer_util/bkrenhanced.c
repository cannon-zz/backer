/*
 * bkrenhanced
 *
 * Enhanced redundancy filter for the Backer tape device.
 *
 * Copyright (C) 2000,2001  Kipp C. Cannon
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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/mtio.h>

#include "backer.h"
#include "backer_fmt.h"
#include "bkr_disp_mode.h"
#include "rs.h"

#define  PROGRAM_NAME    "bkrenhanced"
#define  BLOCK_SIZE      255
#define  PARITY          20


/*
 * Types
 */

typedef enum
	{
	READING = 0,
	WRITING
	} direction_t;

typedef struct
	{
	int  num_in;
	pthread_mutex_t  gate_mutex;
	pthread_mutex_t  all_out_mutex;
	pthread_cond_t  entered;
	pthread_cond_t  all_in;
	pthread_cond_t  all_out;
	} sync_gate_t;

#define  SYNC_GATE_INITIALIZER ((sync_gate_t) \
	{ 0, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, \
	  PTHREAD_COND_INITIALIZER, PTHREAD_COND_INITIALIZER, \
	  PTHREAD_COND_INITIALIZER })


/*
 * Function prototypes
 */

void sigint_handler(int);
void *encoding_write(void *);
void *decoding_write(void *);
void sync_gate(sync_gate_t *, int);
void sync_gate_wait(sync_gate_t *, int);


/*
 * Global data
 */

bkr_format_info_t  format_info[] = BKR_FORMAT_INFO_INITIALIZER;
int  got_sigint = 0;
sync_gate_t  io_gate = SYNC_GATE_INITIALIZER;
unsigned char  *io_buffer[2];
gf  erasure[PARITY][2];
int  num_erase[2] = { 0, 0 };
int sector_capacity;

int  group_buffer_size;
int  group_data_size, group_parity_size;
unsigned char  *data, *parity;
rs_format_t  rs_format;
unsigned char  *group_buffer;
int  *group_length;
int  worst_group = 0;


/*
 * Entry point
 */

int main(int argc, char *argv[])
{
	int  i;
	char  *devname = NULL;
	struct mtget  mtget = { mt_dsreg : -1 };
	direction_t  direction = WRITING;
	int  current_buffer = 0;
	pthread_t  write_thread;
	unsigned int  total_sectors = 0, lost_sectors = 0;

	/*
	 * Some setup stuff
	 */

	reed_solomon_init(BLOCK_SIZE, BLOCK_SIZE - PARITY, &rs_format);


	/*
	 * Process command line options
	 */

	while((i = getopt(argc, argv, "D:F:f::huV:")) != EOF)
		switch(i)
			{
			case 'f':
			mtget.mt_dsreg = -1;
			if(optarg != NULL)
				devname = optarg;
			else
				devname = DEFAULT_DEVICE;
			break;

			case 'u':
			direction = READING;
			break;

			case 'D':
			if(mtget.mt_dsreg == -1)
				break;
			mtget.mt_dsreg &= ~BKR_DENSITY(-1);
			switch(tolower(optarg[0]))
				{
				case 'h':
				mtget.mt_dsreg |= BKR_HIGH;
				break;
				case 'l':
				mtget.mt_dsreg |= BKR_LOW;
				break;
				default:
				mtget.mt_dsreg |= BKR_DENSITY(DEFAULT_MODE);
				break;
				}
			break;

			case 'F':
			if(mtget.mt_dsreg == -1)
				break;
			mtget.mt_dsreg &= ~BKR_FORMAT(-1);
			switch(tolower(optarg[0]))
				{
				case 's':
				mtget.mt_dsreg |= BKR_SP;
				break;
				case 'e':
				mtget.mt_dsreg |= BKR_EP;
				break;
				default:
				mtget.mt_dsreg |= BKR_FORMAT(DEFAULT_MODE);
				break;
				}
			break;

			case 'V':
			if(mtget.mt_dsreg == -1)
				break;
			mtget.mt_dsreg &= ~BKR_VIDEOMODE(-1);
			switch(tolower(optarg[0]))
				{
				case 'n':
				mtget.mt_dsreg |= BKR_NTSC;
				break;
				case 'p':
				mtget.mt_dsreg |= BKR_PAL;
				break;
				default:
				mtget.mt_dsreg |= BKR_VIDEOMODE(DEFAULT_MODE);
				break;
				}
			break;

			case 'h':
			default:
			fputs(
	"Backer enhance redundancy filter.\n" \
	"Usage: " PROGRAM_NAME " [options...]\n" \
	"The following options are recognized:\n" \
	"	-D <h/l>  Set the data rate to high or low\n" \
	"	-F <s/e>  Set the data format to SP or EP\n" \
	"	-V <p/n>  Set the video mode to PAL or NTSC\n" \
	"	-f [dev]  Get the format to use from the Backer device dev\n" \
	"                  (default " DEFAULT_DEVICE ")\n" \
	"	-h        Display usage message\n" \
	"	-u        Unencode (default is to encode)\n", stderr);
			exit(-1);
			}

	/*
	 * Get sector capacity and allocate buffers.
	 */

	if(mtget.mt_dsreg == -1)
		{
		if(devname != NULL)
			{
			if((i = open(devname, O_RDONLY)) < 0)
				{
				perror(PROGRAM_NAME);
				exit(1);
				}
			}
		else
			{
			if(direction == READING)
				i = STDIN_FILENO;
			else
				i = STDOUT_FILENO;
			}
		if(ioctl(i, MTIOCGET, &mtget) < 0)
			{
			perror(PROGRAM_NAME);
			exit(1);
			}
		if(devname != NULL)
			close(i);
		}
	sector_capacity = bkr_sector_capacity(&format_info[bkr_mode_to_format(mtget.mt_dsreg)]);

	group_data_size = sector_capacity * (BLOCK_SIZE - PARITY);
	group_parity_size = sector_capacity * PARITY;
	group_buffer_size = group_data_size + group_parity_size;

	io_buffer[0] = (unsigned char *) malloc(group_buffer_size);
	io_buffer[1] = (unsigned char *) malloc(group_buffer_size);
	group_buffer = (unsigned char *) malloc(group_buffer_size);

	if((io_buffer[0] == NULL) || (io_buffer[1] == NULL) || (group_buffer == NULL))
		{
		errno = ENOMEM;
		perror(PROGRAM_NAME);
		exit(1);
		}

	fprintf(stderr, PROGRAM_NAME ": %s at %4.1f%% efficiency\n",
	        (direction == READING) ? "DECODING" : "ENCODING",
	        100.0*(group_data_size - sizeof(int))/group_buffer_size);

	group_length = (int *) (group_buffer + group_buffer_size - sizeof(int));


	/*
	 * Do data transfer
	 */

	if(direction == READING)
		{
		pthread_create(&write_thread, NULL, decoding_write, NULL);
		for(i = 0; 1; )
			{
			i += fread(io_buffer[current_buffer] + i * sector_capacity, sector_capacity, BLOCK_SIZE - i, stdin);

			if(i == BLOCK_SIZE)
				{
				total_sectors += BLOCK_SIZE;
				sync_gate(&io_gate, 2);
				current_buffer ^= 1;
				num_erase[current_buffer] = 0;
				i = 0;
				continue;
				}

			if(feof(stdin) || (errno != ENODATA))
				break;

			lost_sectors++;

			if(num_erase[current_buffer] < PARITY)
				{
				erasure[num_erase[current_buffer]][current_buffer] = i;
				num_erase[current_buffer]++;
				}
			memset(io_buffer[current_buffer] + i++ * sector_capacity, 0, sector_capacity);
			clearerr(stdin);
			}
		}
	else
		{
		signal(SIGINT, sigint_handler);
		pthread_create(&write_thread, NULL, encoding_write, NULL);
		while(!feof(stdin) && !got_sigint)
			{
			/*
			 * Read data for sector group
			 */

			*group_length = fread(group_buffer + group_parity_size, 1, group_data_size - sizeof(int), stdin);
			memset(group_buffer + group_parity_size + *group_length, 0, group_data_size - sizeof(int) - *group_length);

			/*
			 * Compute parity symbols
			 */

			parity = group_buffer + group_parity_size - PARITY;
			data = group_buffer + group_buffer_size - (BLOCK_SIZE - PARITY);
			while(parity >= group_buffer)
				{
				reed_solomon_encode(parity, data, &rs_format);
				parity -= PARITY;
				data -= BLOCK_SIZE - PARITY;
				}

			/*
			 * Interleave the data
			 */

			data = io_buffer[current_buffer];
			for(i = 0; i < PARITY; i += 1 - group_parity_size)
				for(; i < group_parity_size; i += PARITY)
					*(data++) = group_buffer[i];
			for(i = group_parity_size; i < group_parity_size + BLOCK_SIZE - PARITY; i += 1 - group_data_size)
				for(; i < group_buffer_size; i += BLOCK_SIZE - PARITY)
					*(data++) = group_buffer[i];

			total_sectors += BLOCK_SIZE;
			sync_gate(&io_gate, 2);
			current_buffer ^= 1;
			}
		}

	/*
	 * At EOF:  wait for output thread to re-enter the gate then cancel
	 * it.
	 */

	sync_gate_wait(&io_gate, 1);
	pthread_cancel(write_thread);
	pthread_join(write_thread, NULL);

	/*
	 * Display final statistics and quit.
	 */

	if(direction == READING)
		{
		fprintf(stderr, PROGRAM_NAME ": Number of lost sectors detected:  %u of %u\n" \
		                PROGRAM_NAME ": Number of errors in worst block:  %d (%u allowed)\n", lost_sectors, total_sectors, worst_group, PARITY);
		}

	exit(0);
}


/*
 * sigint_handler()
 *
 * SIGINT handler for cleanly terminating a recording.
 */

void sigint_handler(int num)
{
	got_sigint = 1;
	fputs(PROGRAM_NAME ": got SIGINT, writing last group.  Please wait...\n", stderr);
	signal(SIGINT, SIG_IGN);
}


/*
 * I/O threads
 */

void *decoding_write(void *arg)
{
	int  i;
	int  current_buffer = 0;

	while(1)
		{
		sync_gate(&io_gate, 2);

		/*
		 * De-interleave the sector group
		 */

		data = io_buffer[current_buffer];
		for(i = 0; i < PARITY; i += 1 - group_parity_size)
			for(; i < group_parity_size; i += PARITY)
				group_buffer[i] = *(data++);
		for(i = group_parity_size; i < group_parity_size + BLOCK_SIZE - PARITY; i += 1 - group_data_size)
			for(; i < group_buffer_size; i += BLOCK_SIZE - PARITY)
				group_buffer[i] = *(data++);

		/*
		 * Perform error & erasure correction
		 */

		parity = group_buffer + group_parity_size - PARITY;
		data = group_buffer + group_buffer_size - (BLOCK_SIZE - PARITY);
		while(parity >= group_buffer)
			{
			i = reed_solomon_decode(parity, data, erasure[current_buffer], num_erase[current_buffer], &rs_format);
			if(i > worst_group)
				worst_group = i;
			parity -= PARITY;
			data -= BLOCK_SIZE - PARITY;
			}

		fwrite(group_buffer + group_parity_size, 1, *group_length, stdout);
		current_buffer ^= 1;
		}
}

void *encoding_write(void *arg)
{
	int current_buffer = 0;

	while(1)
		{
		sync_gate(&io_gate, 2);

		fwrite(io_buffer[current_buffer], sector_capacity, BLOCK_SIZE, stdout);
		current_buffer ^= 1;
		}
}


/*
 * sync_gate()
 *
 * Waits for num_to_wait_for threads to enter.  This function is a
 * cancellation point.
 */

void sync_gate(sync_gate_t *gate, int num_to_wait_for)
{
	if(num_to_wait_for <= 1)
		{
		pthread_testcancel();
		return;
		}

	pthread_mutex_lock(&gate->all_out_mutex);
	pthread_mutex_lock(&gate->gate_mutex);

	pthread_cond_broadcast(&gate->entered);

	if(++gate->num_in == num_to_wait_for)
		{
		pthread_cond_broadcast(&gate->all_in);
		pthread_cond_wait(&gate->all_out, &gate->gate_mutex);
		--gate->num_in;
		pthread_mutex_unlock(&gate->all_out_mutex);
		}
	else
		{
		pthread_mutex_unlock(&gate->all_out_mutex);
		pthread_cond_wait(&gate->all_in, &gate->gate_mutex);
		if(--gate->num_in == 1)
			pthread_cond_broadcast(&gate->all_out);
		}

	pthread_mutex_unlock(&gate->gate_mutex);
}

void sync_gate_wait(sync_gate_t *gate, int num_to_wait_for)
{
	pthread_mutex_lock(&gate->all_out_mutex);
	pthread_mutex_lock(&gate->gate_mutex);

	pthread_mutex_unlock(&gate->all_out_mutex);
	while(gate->num_in != num_to_wait_for)
		pthread_cond_wait(&gate->entered, &gate->gate_mutex);

	pthread_mutex_unlock(&gate->gate_mutex);
}
