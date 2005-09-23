/*
 * bkr_display_mode()
 *
 * Common mode display function for command line utilities.
 *
 * The items to be displayed can be selected by passing invalid values for the
 * unwanted parameters.  For example, if the timeout passed to this function
 * exceeds BKR_MAX_TIMEOUT then it will not be displayed.
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

#include <stdio.h>

#include "backer.h"

void  bkr_display_mode(unsigned int mode, unsigned int timeout)
{
	if(timeout <= BKR_MAX_TIMEOUT)
		fprintf(stderr, "\tTimeout:     %u second(s)\n", timeout);
	switch(BKR_VIDEOMODE(mode))
		{
		case BKR_PAL:
		fputs("\tVideo mode:  [*] PAL   [ ] NTSC\n", stderr);
		break;
		case BKR_NTSC:
		fputs("\tVideo mode:  [ ] PAL   [*] NTSC\n", stderr);
		default:
		break;
		}
	switch(BKR_DENSITY(mode))
		{
		case BKR_LOW:
		fputs("\tData rate:   [*] LOW   [ ] HIGH\n", stderr);
		break;
		case BKR_HIGH:
		fputs("\tData rate:   [ ] LOW   [*] HIGH\n", stderr);
		default:
		break;
		}
	switch(BKR_SPEED(mode))
		{
		case BKR_EP:
		fputs("\tTape speed:  [*] EP    [ ] SP/LP\n", stderr);
		break;
		case BKR_SP:
		fputs("\tTape speed:  [ ] EP    [*] SP/LP\n", stderr);
		default:
		break;
		}
	switch(BKR_FORMAT(mode))
		{
		case BKR_RAW:
		fputs("\tData type:   [*] RAW   [ ] FORMATED\n", stderr);
		break;
		case BKR_FMT:
		fputs("\tData type:   [ ] RAW   [*] FORMATED\n", stderr);
		default:
		break;
		}
}
