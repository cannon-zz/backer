/*
 * bkrmode
 *
 * Command line utility for setting the operating mode of the Backer device
 * driver.
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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "backer.h"
#include "bkr_disp_mode.h"

int main(int argc, char *argv[])
{
	struct bkrconfig  oldconfig, newconfig;
	char  *fname = "/dev/backer";
	int  devfile;
	int  opt;

	newconfig.mode = -1;
	newconfig.timeout = -1;

	while((opt = getopt(argc, argv, "v:d:m:f:s:t:h")) != EOF)
		switch(opt)
			{
			case 'v':
			switch(tolower(optarg[0]))
				{
				case 'n':
				newconfig.mode &= ~BKR_VIDEOMODE(-1) | BKR_NTSC;
				break;
				case 'p':
				newconfig.mode &= ~BKR_VIDEOMODE(-1) | BKR_PAL;
				break;
				default:
				break;
				}
			break;

			case 'd':
			switch(tolower(optarg[0]))
				{
				case 'h':
				newconfig.mode &= ~BKR_DENSITY(-1) | BKR_HIGH;
				break;
				case 'l':
				newconfig.mode &= ~BKR_DENSITY(-1) | BKR_LOW;
				break;
				default:
				break;
				}
			break;

			case 'm':
			switch(tolower(optarg[0]))
				{
				case 'f':
				newconfig.mode &= ~BKR_FORMAT(-1) | BKR_FMT;
				break;
				case 'r':
				newconfig.mode &= ~BKR_FORMAT(-1) | BKR_RAW;
				break;
				default:
				break;
				}
			break;

			case 's':
			switch(tolower(optarg[0]))
				{
				case 's':
				newconfig.mode &= ~BKR_SPEED(-1) | BKR_SP;
				break;
				case 'e':
				newconfig.mode &= ~BKR_SPEED(-1) | BKR_EP;
				break;
				default:
				break;
				}
			break;

			case 'f':
			fname = optarg;
			break;

			case 't':
			newconfig.timeout = atoi(optarg);
			break;

			case 'h':
			default:
			printf(
	"Backer mode setting utility.\n" \
	"Usage: bkrmode [options]\n" \
	"The following options are recognized:\n" \
	"	-v <pal/ntsc>      Set the video mode to PAL or NTSC\n" \
	"	-d <high/low>      Set the data rate to high or low\n" \
	"	-m <formated/raw>  Set the driver to formated or raw mode\n" \
	"	-s <SP/EP>         Set the driver for a tape speed of SP or EP\n" \
	"	-t num             Set the timeout to num seconds\n" \
	"	-f dev             Use the device named dev\n" \
	"If no options are given then the current mode is displayed.\n");
			exit(-1);
			}

	if((devfile = open(fname, O_RDWR)) < 0)
		{
		printf("\nbkrmode: can't open %s\n\n", fname);
		exit(-1);
		}
	if(ioctl(devfile, BKRIOCGETMODE, &oldconfig) < 0)
		{
		printf("bkrmode: can't perform operation on device\n");
		exit(-1);
		}

	if((newconfig.mode == -1) && (newconfig.timeout == -1))
		{
		printf("\nCurrent mode of %s:\n", fname);
		bkr_display_mode(oldconfig.mode, oldconfig.timeout);
		printf("\n");
		exit(0);
		}

	if(BKR_VIDEOMODE(newconfig.mode) == BKR_VIDEOMODE(-1))
		newconfig.mode &= ~BKR_VIDEOMODE(-1) | BKR_VIDEOMODE(oldconfig.mode);
	if(BKR_DENSITY(newconfig.mode) == BKR_DENSITY(-1))
		newconfig.mode &= ~BKR_DENSITY(-1) | BKR_DENSITY(oldconfig.mode);
	if(BKR_FORMAT(newconfig.mode) == BKR_FORMAT(-1))
		newconfig.mode &= ~BKR_FORMAT(-1) | BKR_FORMAT(oldconfig.mode);
	if(BKR_SPEED(newconfig.mode) == BKR_SPEED(-1))
		newconfig.mode &= ~BKR_SPEED(-1) | BKR_SPEED(oldconfig.mode);
	if(newconfig.timeout == -1)
		newconfig.timeout = oldconfig.timeout;

	printf("\nSetting mode of %s to:\n", fname);
	bkr_display_mode(newconfig.mode, newconfig.timeout);
	printf("\n");

	if(ioctl(devfile, BKRIOCSETMODE, &newconfig) < 0)
		{
		printf("bkrmode: can't perform operation on device\n");
		exit(-1);
		}

	close(devfile);
	exit(0);
}
