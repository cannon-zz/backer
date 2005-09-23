/*
 * bkrlog
 *
 * Log file generator for use with the Backer tape device and tar.
 *
 * Copyright (C) 2000  Kipp C. Cannon
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
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/timeb.h>

#include "backer.h"
#include "bkr_aux_puts.h"

#define  PROGRAM_NAME     "bkrlog"
#define  LABEL_SIZE       6     /* characters */
#define  UPDATE_INTERVAL  250   /* milliseconds */


/*
 * Entry point
 */

int main(int argc, char *argv[])
{
	struct bkrformat  format;       /* tape format */
	int  tapefile;                  /* file handle for backer device */
	unsigned long  entry = 0;       /* entry number in log file */
	int  insize = 128;              /* size of input buffer */
	int  opt;                       /* for command line processing */
	char  *device = "/dev/backer";  /* device name */
	char  label[LABEL_SIZE+1];      /* archive label */
	char  *intext = NULL;           /* log file input buffer */
	char  *aux = NULL;              /* data for aux buffer */
	char  auxtext[17];              /* text for aux buffer */
	struct timeb last_time, this_time;

	/*
	 * Process command line options
	 */

	memset(auxtext, 0, 17);
	while((opt = getopt(argc, argv, "f:t:h")) != EOF)
		switch(opt)
			{
			case 'f':
			device = optarg;
			break;

			case 't':
			strncpy(auxtext, optarg, 16);
			break;

			case 'h':
			default:
			fputs(
	"Log file generator for Backer tape device.\n" \
	"Usage:  " PROGRAM_NAME " [options] -t text\n" \
	"        tar -v <other parameters> | " PROGRAM_NAME " [options] > logfile\n" \
	"The following options are recognized:\n" \
	"     -f devname     Use device devname (default /dev/backer)\n" \
	"     -h             Display usage\n" \
	"     -t text        Write text to auxiliary area and quit\n", stderr);
			exit(0);
			}

	/*
	 * Open backer device, retrieve format and create buffers.
	 */

	if((tapefile = open(device, O_RDWR)) < 0)
		{
		}
	else if(ioctl(tapefile, BKRIOCGETFORMAT, &format) < 0)
		{
		}
	else
		{
		intext = (char *) malloc(insize);
		aux = (char *) malloc(format.aux_length);
		if((intext == NULL) || (aux == NULL))
			errno = ENOMEM;
		}
	if(errno)
		{
		perror(PROGRAM_NAME);
		exit(-1);
		}

	/*
	 * If being used for one-shot message, print it and quit
	 */

	if(auxtext[0] != 0)
		{
		bkr_aux_puts(auxtext, aux, &format);
		ioctl(tapefile, BKRIOCSETAUX, aux);
		close(tapefile);
		exit(0);
		}

	/*
	 * Process output of tar line by line
	 */

	fgets(label, LABEL_SIZE, stdin);
	label[LABEL_SIZE] = 0;

	fprintf(stdout, "Log file for archive volume label: %s\n", label);

	ftime(&last_time);

	while(1)
		{
		/*
		 * Get current file name, expanding input buffer if needed
		 */

		fgets(intext, insize, stdin);
		if(feof(stdin))
			break;
		while(strchr(intext, '\n') == NULL)
			{
			intext = (char *) realloc(intext, insize << 1);
			if(intext == NULL)
				{
				errno = ENOMEM;
				perror(PROGRAM_NAME);
				exit(-1);
				}
			fgets(intext+insize, insize, stdin);
			insize <<= 1;
			}

		/*
		 * Write text to log file
		 */

		fprintf(stdout, "%08lu %s", entry, intext);

		/*
		 * Write text to tape and increment entry number
		 */

		switch(format.bytes_per_line)
			{
			case BYTES_PER_LINE_HIGH:
			sprintf(auxtext, "%6s: %08lu", label, entry);
			if(++entry > 99999999)
				entry = 0;
			break;

			case BYTES_PER_LINE_LOW:
			sprintf(auxtext, "%06lu", entry);
			if(++entry > 999999)
				entry = 0;
			break;
			}
		ftime(&this_time);
		if(1000*(this_time.time - last_time.time) +
		        (short) (this_time.millitm - last_time.millitm) >= UPDATE_INTERVAL)
			{
			bkr_aux_puts(auxtext, aux, &format);
			ioctl(tapefile, BKRIOCSETAUX, aux);
			last_time = this_time;
			}
		}

	close(tapefile);
	exit(0);
}
