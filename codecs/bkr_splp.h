/*
 * Driver for Danmere's Backer 16/32 video tape backup cards.
 *
 *                                Sector Codec
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


#ifndef __BKR_SPLP_H__
#define __BKR_SPLP_H__


#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <backer.h>
#include <rs.h>


G_BEGIN_DECLS


/*
 * Encoder
 */


#define BKR_SPLPENC_TYPE		(bkr_splpenc_get_type())
#define BKR_SPLPENC(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj), BKR_SPLPENC_TYPE, BkrSPLPEnc))
#define BKR_SPLPENC_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass), BKR_SPLPENC_TYPE, BkrSPLPEnc))
#define GST_IS_BKR_SPLPENC(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), BKR_SPLPENC_TYPE))
#define GST_IS_BKR_SPLPENC_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), BKR_SPLPENC_TYPE))


typedef struct {
	GstElementClass parent_class;
} BkrSPLPEncClass;


typedef struct {
	GstElement element;

	GstPad *srcpad;
	GstAdapter *adapter;

	enum bkr_videomode videomode;
	enum bkr_bitdensity bitdensity;
	enum bkr_sectorformat sectorformat;

	/*
	 * Format information.
	 *	capacity = data_size - sizeof(header)
	 */

	struct bkr_splp_format {
		gint data_size;
		gint parity_size;
		gint capacity;
		gint interleave;
	} *format;

	rs_format_t *rs_format;

	gint sector_number;
} BkrSPLPEnc;


GType bkr_splpenc_get_type(void);


/*
 * Decoder
 */


#define BKR_SPLPDEC_TYPE		(bkr_splpdec_get_type())
#define BKR_SPLPDEC(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj), BKR_SPLPDEC_TYPE, BkrSPLPDec))
#define BKR_SPLPDEC_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass), BKR_SPLPDEC_TYPE, BkrSPLPDec))
#define GST_IS_BKR_SPLPDEC(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), BKR_SPLPDEC_TYPE))
#define GST_IS_BKR_SPLPDEC_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), BKR_SPLPDEC_TYPE))


typedef struct {
	GstElementClass parent_class;
} BkrSPLPDecClass;


typedef struct {
	GstElement element;

	GstPad *srcpad;

	enum bkr_videomode videomode;
	enum bkr_bitdensity bitdensity;
	enum bkr_sectorformat sectorformat;
	struct bkr_splp_format *format;
	rs_format_t *rs_format;

	gint header_is_good;
	gint bytes_corrected;
	gint worst_block;
	gint recent_block;
	gint bad_sectors;
	gint lost_runs;
	gint duplicate_runs;
	gint decoded_number;
	gint not_underrunning;
	gint sector_number;
} BkrSPLPDec;


GType bkr_splpdec_get_type(void);


G_END_DECLS


#endif				/* __BKR_SPLP_H__ */
