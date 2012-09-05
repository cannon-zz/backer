/*
 * bkr_disp_mode.h
 *
 * Header file for bkr_disp_mode.c
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

#ifndef _BKR_DISP_MODE_H
#define _BKR_DISP_MODE_H

#include <stdio.h>
#include <backer.h>

void bkr_display_mode(FILE *, enum bkr_videomode, enum bkr_bitdensity, enum bkr_sectorformat);

#endif /* _BKR_DISP_MODE_H */
