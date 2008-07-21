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


#ifndef __BKR_GCR_H__
#define __BKR_GCR_H__


#include <gst/gst.h>
#include <backer.h>


G_BEGIN_DECLS


/*
 * Format information.
 * 	capacity = corresponding frame codec capacity - modulation_pad
 */


struct bkr_gcr_format {
	gint capacity;
	gint modulation_pad;
};


/*
 * Encoder
 */


#define BKR_GCRENC_TYPE			(bkr_gcrenc_get_type())
#define BKR_GCRENC(obj)			(G_TYPE_CHECK_INSTANCE_CAST((obj), BKR_GCRENC_TYPE, BkrGCREnc))
#define BKR_GCRENC_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST((klass), BKR_GCRENC_TYPE, BkrGCREnc))
#define GST_IS_BKR_GCRENC(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), BKR_GCRENC_TYPE))
#define GST_IS_BKR_GCRENC_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), BKR_GCRENC_TYPE))


typedef struct _BkrGCREncClass BkrGCREncClass;
typedef struct _BkrGCREnc BkrGCREnc;


struct _BkrGCREncClass {
	GstElementClass parent_class;
};


struct _BkrGCREnc {
	GstElement element;

	GstPad *sinkpad, *srcpad;

	enum bkr_videomode videomode;
	enum bkr_bitdensity bitdensity;
	enum bkr_sectorformat sectorformat;
	struct bkr_gcr_format format;
};


GType bkr_gcrenc_get_type(void);


/*
 * Decoder
 */


#define BKR_GCRDEC_TYPE			(bkr_gcrdec_get_type())
#define BKR_GCRDEC(obj)			(G_TYPE_CHECK_INSTANCE_CAST((obj), BKR_GCRDEC_TYPE, BkrGCRDec))
#define BKR_GCRDEC_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST((klass), BKR_GCRDEC_TYPE, BkrGCRDec))
#define GST_IS_BKR_GCRDEC(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), BKR_GCRDEC_TYPE))
#define GST_IS_BKR_GCRDEC_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), BKR_GCRDEC_TYPE))


typedef struct {
	GstElementClass parent_class;
} BkrGCRDecClass;


typedef struct {
	GstElement element;

	GstPad *sinkpad, *srcpad;

	enum bkr_videomode videomode;
	enum bkr_bitdensity bitdensity;
	enum bkr_sectorformat sectorformat;
	struct bkr_gcr_format format;
} BkrGCRDec;


GType bkr_gcrdec_get_type(void);


G_END_DECLS


#endif	/* __BKR_GCR_H__ */
