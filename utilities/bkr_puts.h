/*
 * bkr_puts.h
 *
 * Header file for bkr_puts.c
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

#ifndef _BKR_PUTS_H
#define _BKR_PUTS_H

#include <backer.h>

#define  BKR_FONT_WIDTH  5	/* font width  */
#define  BKR_FONT_HEIGHT 14	/* font height */

struct bkr_puts_format {
	int leader;
	int data_size;
	int key_interval;
	int key_length;
	int bytes_per_line;
};

struct bkr_puts_format bkr_puts_get_format(enum bkr_videomode, enum bkr_bitdensity, enum bkr_sectorformat);
int bkr_puts(const char *, unsigned char *, int, int, const struct bkr_puts_format *);

#endif	/* _BKR_PUTS_H */
