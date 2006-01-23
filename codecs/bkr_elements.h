/*
 * Driver for Danmere's Backer 16/32 video tape backup cards.
 *
 *                         GStreamer Element Support
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

#ifndef __BKR_ELEMENTS_H__
#define __BKR_ELEMENTS_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define BKR_TYPE_VIDEOMODE	(bkr_videomode_get_type())
#define BKR_TYPE_DENSITY	(bkr_density_get_type())

GType bkr_videomode_get_type(void);
GType bkr_density_get_type(void);

#define DEFAULT_VIDMODE BKR_NTSC
#define DEFAULT_DENSITY BKR_HIGH

G_END_DECLS
#endif	/* __BKR_ELEMENTS_H__ */
