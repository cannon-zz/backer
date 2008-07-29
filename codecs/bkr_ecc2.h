/*
 * Driver for Danmere's Backer 16/32 video tape backup cards.
 *
 *                   Sector Drop-Out Error Correction Codec
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


#ifndef _BKR_ECC2_H_
#define _BKR_ECC2_H_


#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <backer.h>
#include <rs.h>


G_BEGIN_DECLS


/*
 * Encoder
 */


#define BKR_ECC2ENC_TYPE		(bkr_ecc2enc_get_type())
#define BKR_ECC2ENC(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj), BKR_ECC2ENC_TYPE, BkrECC2Enc))
#define BKR_ECC2ENC_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass), BKR_ECC2ENC_TYPE, BkrECC2Enc))
#define GST_IS_BKR_ECC2ENC(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), BKR_ECC2ENC_TYPE))
#define GST_IS_BKR_ECC2ENC_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), BKR_ECC2ENC_TYPE))


typedef struct {
	GstElementClass parent_class;
} BkrECC2EncClass;


typedef struct {
	GstElement element;

	GstPad *srcpad;
	GstAdapter *adapter;

	enum bkr_videomode videomode;
	enum bkr_bitdensity bitdensity;
	enum bkr_sectorformat sectorformat;

	rs_format_t *rs_format;

	struct bkr_ecc2_format {
		int group_size;
		int data_size;
		int parity_size;
		int capacity;
		int interleave;
	} *format;
} BkrECC2Enc;


GType bkr_ecc2enc_get_type(void);


/*
 * Decoder
 */


#define BKR_ECC2DEC_TYPE		(bkr_ecc2dec_get_type())
#define BKR_ECC2DEC(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj), BKR_ECC2DEC_TYPE, BkrECC2Dec))
#define BKR_ECC2DEC_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass), BKR_ECC2DEC_TYPE, BkrECC2Dec))
#define GST_IS_BKR_ECC2DEC(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), BKR_ECC2DEC_TYPE))
#define GST_IS_BKR_ECC2DEC_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), BKR_ECC2DEC_TYPE))


typedef struct {
	GstElementClass parent_class;
} BkrECC2DecClass;


typedef struct {
	GstElement element;

	GstPad *srcpad;
	GstAdapter *adapter;

	enum bkr_videomode videomode;
	enum bkr_bitdensity bitdensity;
	enum bkr_sectorformat sectorformat;

	struct bkr_ecc2_format *format;

	rs_format_t *rs_format;

	/* erasure vector and length for use by Reed-Solomon decoder */
	gf *erasure;
	int  num_erasure;

	/* the number within the group of the next sector to be received */
	int sector_number;

	/* most sectors requiring correction in a sector group */
	int  worst_group;

	/* number of bad sectors not identified as such by the sector codec */
	unsigned long  extra_errors;
} BkrECC2Dec;


GType bkr_ecc2dec_get_type(void);


G_END_DECLS


#endif	/* __BKR_ECC2_H__ */
