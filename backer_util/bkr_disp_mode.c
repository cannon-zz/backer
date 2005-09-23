/*
 * bkr_display_mode()
 *
 * Common mode display function for command line utilities.
 *
 * The items to be displayed can be selected by passing invalid values for the
 * unwanted parameters.
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

#include <stdio.h>

#include "backer.h"

void  bkr_display_mode(FILE *file, unsigned int mode)
{
	switch(BKR_VIDEOMODE(mode))
		{
		case BKR_PAL:
		fputs("\tVideo mode:   [*] PAL   [ ] NTSC\n", file);
		break;
		case BKR_NTSC:
		fputs("\tVideo mode:   [ ] PAL   [*] NTSC\n", file);
		default:
		break;
		}
	switch(BKR_DENSITY(mode))
		{
		case BKR_LOW:
		fputs("\tData rate:    [*] LOW   [ ] HIGH\n", file);
		break;
		case BKR_HIGH:
		fputs("\tData rate:    [ ] LOW   [*] HIGH\n", file);
		default:
		break;
		}
	switch(BKR_FORMAT(mode))
		{
		case BKR_RAW:
		fputs("\tData format:  [*] RAW   [ ] SP/LP  [ ] EP\n", file);
		break;
		case BKR_SP:
		fputs("\tData format:  [ ] RAW   [*] SP/LP  [ ] EP\n", file);
		break;
		case BKR_EP:
		fputs("\tData format:  [ ] RAW   [ ] SP/LP  [*] EP\n", file);
		default:
		break;
		}
}
