/*
 * Driver for Danmere's Backer 16/32 video tape backup cards.
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


#ifndef __BKR_FRAME_H__
#define __BKR_FRAME_H__


#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <backer.h>


G_BEGIN_DECLS


/*
 * Encoder
 */


#define BKR_FRAMEENC_TYPE			(bkr_frameenc_get_type())
#define BKR_FRAMEENC(obj)			(G_TYPE_CHECK_INSTANCE_CAST((obj), BKR_FRAMEENC_TYPE, BkrFrameEnc))
#define BKR_FRAMEENC_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST((klass), BKR_FRAMEENC_TYPE, BkrFrameEnc))
#define GST_IS_BKR_FRAMEENC(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), BKR_FRAMEENC_TYPE))
#define GST_IS_BKR_FRAMEENC_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), BKR_FRAMEENC_TYPE))


typedef struct {
	GstElementClass parent_class;
} BkrFrameEncClass;


typedef struct {
	GstElement element;

	GstPad *srcpad;

	enum bkr_videomode videomode;
	enum bkr_bitdensity bitdensity;
	enum bkr_sectorformat sectorformat;

	gint odd_field;

	/*
	 * Format information.
	 * 	interlace = number of extra bytes in an odd field
	 */

	struct bkr_frame_format {
		gint field_size;
		gint interlace;
		gint leader;
		gint trailer;
		gint active_size;
		gint key_interval;
		gint key_length;
	} *format;

	/*
	 * Flags
	 */

	gboolean inject_noise;
} BkrFrameEnc;


GType bkr_frameenc_get_type(void);


/*
 * Decoder
 */


#define BKR_FRAMEDEC_TYPE			(bkr_framedec_get_type())
#define BKR_FRAMEDEC(obj)			(G_TYPE_CHECK_INSTANCE_CAST((obj), BKR_FRAMEDEC_TYPE, BkrFrameDec))
#define BKR_FRAMEDEC_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST((klass), BKR_FRAMEDEC_TYPE, BkrFrameDec))
#define GST_IS_BKR_FRAMEDEC(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), BKR_FRAMEDEC_TYPE))
#define GST_IS_BKR_FRAMEDEC_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), BKR_FRAMEDEC_TYPE))


typedef struct {
	GstElementClass parent_class;
} BkrFrameDecClass;


typedef struct {
	GstElement element;

	GstPad *srcpad;
	GstAdapter *adapter;

	enum bkr_videomode videomode;
	enum bkr_bitdensity bitdensity;
	enum bkr_sectorformat sectorformat;
	struct bkr_frame_format *format;

	gint worst_key;
	gint best_nonkey;
	gint frame_warnings;
	gint last_field_offset;
	guint smallest_field;
	gint largest_field;
} BkrFrameDec;


GType bkr_framedec_get_type(void);


G_END_DECLS


#endif	/* __BKR_FRAME_H__ */
