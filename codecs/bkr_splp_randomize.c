/*
 * Driver for Danmere's Backer 16/32 video tape backup cards.
 *
 *                             Formating Layer
 *
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

#include <gst/gst.h>
#include <bkr_bytes.h>
#include <bkr_splp_randomize.h>


/*
 * (De-)Randomize a buffer using the supplied seed.  The count is rounded
 * up to the nearest multiple of 4 so make sure the buffer is sized
 * accordingly!  For each seed, this function is its own inverse.
 *
 * For the random number generator used see:  Knuth, D. E. 1981,
 * Semi-Numerical Algorithms, 2nd ed., vol 2 of The Art of Computer
 * Programing.
 */

void bkr_splp_sector_randomize(void *buff, gint count, guint32 seed)
{
	guint32 *location = buff;
	gint index;
	guint32 history[4];

	if(count <= 0)
		return;

	for(index = 3; index >= 0; index--)
		history[index] = (seed = 1664525 * seed + 1013904223);

	for(count = (count-1) >> 2; count >= 0; count--) {
		seed = 1664525 * seed + 1013904223;
		index = seed >> 30;
		location[count] ^= __cpu_to_le32(history[index]);
		history[index] = seed;
	}
}
