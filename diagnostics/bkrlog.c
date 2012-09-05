/*
 * bkrlog
 *
 * Log file generator for use with the Backer tape device and GNU tar.
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

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include "backer.h"
#include "bkr_aux_puts.h"

#define  PROGRAM_NAME     "bkrlog"
#define  LABEL_SIZE       6     /* characters */
#define  UPDATE_INTERVAL  100   /* milliseconds */

/*
 * Function prototypes
 */

void commit_aux_data(int);


/*
 * Global Data
 */

struct bkrformat  format;       /* tape format */
int  tapefile;                  /* file handle for backer device */
int  need_update;               /* flag for output "thread" */
char  *aux = NULL;              /* data for aux buffer */
char  *auxtext = NULL;          /* text for aux buffer */


/*
 * Entry point
 */

int main(int argc, char *argv[])
{
	struct itimerval update_interval = {{0, UPDATE_INTERVAL*1000}, {0, UPDATE_INTERVAL*1000}};
	unsigned long  entry = 0;       /* entry number in log file */
	int  insize = 128;              /* size of input buffer */
	int  opt;                       /* for command line processing */
	char  *device = "/dev/backer";  /* device name */
	char  label[LABEL_SIZE+1];      /* archive label */
	char  *intext = NULL;           /* log file input buffer */

	/*
	 * Process command line options
	 */

	while((opt = getopt(argc, argv, "f:t:h")) != EOF)
		switch(opt)
			{
			case 'f':
			device = optarg;
			break;

			case 't':
			auxtext = optarg;
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
	 * If we are just writing a one-shot message, send it and quit.
	 */

	if(auxtext != NULL)
		{
		bkr_aux_puts(auxtext, aux, &format);
		ioctl(tapefile, BKRIOCSETAUX, aux);
		close(tapefile);
		exit(0);
		}
	auxtext = (char *) calloc(17, sizeof(char));

	/*
	 * Set up the output "thread".
	 */

	need_update = 0;
	signal(SIGALRM, commit_aux_data);
	setitimer(ITIMER_REAL, &update_interval, NULL);

	/*
	 * Process output of tar line by line
	 */

	fscanf(stdin, "%s\n", intext);
	memcpy(label, intext, LABEL_SIZE+1);
	label[LABEL_SIZE] = 0;

	fprintf(stdout, "Log file for archive volume label: %s\n", label);

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
		need_update = 1;
		}

	close(tapefile);
	exit(0);
}


/*
 * commit_aux_data()
 *
 * Periodically commits auxtext to the auxiliary data region
 */

void commit_aux_data(int signal)
{
	if(need_update)
		{
		bkr_aux_puts(auxtext, aux, &format);
		ioctl(tapefile, BKRIOCSETAUX, aux);
		need_update = 0;
		}
}
