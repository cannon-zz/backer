/*
 * Driver for Danmere's Backer 16/32 video tape backup cards.
 *
 * Copyright (C) 2000,2001,2002,2008  Kipp C. Cannon
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


#ifndef __BKR_RLL_H__
#define __BKR_RLL_H__


#include <gst/gst.h>
#include <backer.h>


G_BEGIN_DECLS


/*
 * Encoder
 */


#define BKR_RLLENC_TYPE			(bkr_rllenc_get_type())
#define BKR_RLLENC(obj)			(G_TYPE_CHECK_INSTANCE_CAST((obj), BKR_RLLENC_TYPE, BkrRLLEnc))
#define BKR_RLLENC_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST((klass), BKR_RLLENC_TYPE, BkrRLLEnc))
#define GST_IS_BKR_RLLENC(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), BKR_RLLENC_TYPE))
#define GST_IS_BKR_RLLENC_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), BKR_RLLENC_TYPE))


typedef struct {
	GstElementClass parent_class;
} BkrRLLEncClass;


typedef struct {
	GstElement element;

	GstPad *srcpad;

	enum bkr_videomode videomode;
	enum bkr_bitdensity bitdensity;
	enum bkr_sectorformat sectorformat;

	/*
	 * Format information.
	 * 	capacity = corresponding frame codec capacity - modulation_pad
	 */

	struct bkr_rll_format {
		gint capacity;
		gint modulation_pad;
	} *format;
} BkrRLLEnc;


GType bkr_rllenc_get_type(void);


/*
 * Decoder
 */


#define BKR_RLLDEC_TYPE			(bkr_rlldec_get_type())
#define BKR_RLLDEC(obj)			(G_TYPE_CHECK_INSTANCE_CAST((obj), BKR_RLLDEC_TYPE, BkrRLLDec))
#define BKR_RLLDEC_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST((klass), BKR_RLLDEC_TYPE, BkrRLLDec))
#define GST_IS_BKR_RLLDEC(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), BKR_RLLDEC_TYPE))
#define GST_IS_BKR_RLLDEC_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), BKR_RLLDEC_TYPE))


typedef struct {
	GstElementClass parent_class;
} BkrRLLDecClass;


typedef struct {
	GstElement element;

	GstPad *srcpad;

	enum bkr_videomode videomode;
	enum bkr_bitdensity bitdensity;
	enum bkr_sectorformat sectorformat;
	struct bkr_rll_format *format;
} BkrRLLDec;


GType bkr_rlldec_get_type(void);


G_END_DECLS


#endif	/* __BKR_RLL_H__ */
