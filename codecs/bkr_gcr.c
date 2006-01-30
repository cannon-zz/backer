/*
 * Driver for Danmere's Backer 16/32 video tape backup cards.
 *
 *                             Formating Layer
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

#include <string.h>

#include <gst/gst.h>
#include <backer.h>
#include <bkr_bytes.h>
#include <bkr_elements.h>
#include <bkr_gcr.h>


/*
 * ========================================================================
 *
 *                              PARAMETERS
 *
 * ========================================================================
 */

/*
 * Format info.
 */

static struct bkr_gcr_format format(enum bkr_videomode v, enum bkr_bitdensity d)
{
	switch(d) {
	case BKR_LOW:
		switch(v) {
		case BKR_NTSC:
			return (struct bkr_gcr_format) { 816, 102};
		case BKR_PAL:
			return (struct bkr_gcr_format) { 984, 123};
		}
	case BKR_HIGH:
		switch(v) {
		case BKR_NTSC:
			return (struct bkr_gcr_format) {2072, 259};
		case BKR_PAL:
			return (struct bkr_gcr_format) {2496, 312};
		}
	}

	return (struct bkr_gcr_format) {0,};
}


/*
 * ========================================================================
 *
 *                              Global Data
 *
 * ========================================================================
 */

static const guint16 gcr_encode[] = {
	0x089, 0x08a, 0x08b, 0x08c, 0x08d, 0x08e, 0x091, 0x092,
	0x093, 0x094, 0x095, 0x096, 0x099, 0x09a, 0x09b, 0x09c,
	0x09d, 0x09e, 0x0a2, 0x0a3, 0x0a4, 0x0a5, 0x0a6, 0x0a9,
	0x0aa, 0x0ab, 0x0ac, 0x0ad, 0x0ae, 0x0b1, 0x0b2, 0x0b3,
	0x0b4, 0x0b5, 0x0b6, 0x0b9, 0x0ba, 0x0bb, 0x0bc, 0x0bd,
	0x0be, 0x0c2, 0x0c3, 0x0c4, 0x0c5, 0x0c6, 0x0c9, 0x0ca,
	0x0cb, 0x0cc, 0x0cd, 0x0ce, 0x0d1, 0x0d2, 0x0d3, 0x0d4,
	0x0d5, 0x0d6, 0x0d9, 0x0da, 0x0db, 0x0dc, 0x0dd, 0x0de,
	0x0e1, 0x0e2, 0x0e3, 0x0e4, 0x0e5, 0x0e6, 0x0e9, 0x0ea,
	0x0eb, 0x0ec, 0x0ed, 0x0ee, 0x0f1, 0x0f2, 0x0f3, 0x0f4,
	0x0f5, 0x0f6, 0x0f9, 0x0fa, 0x0fb, 0x0fc, 0x0fd, 0x109,
	0x10a, 0x10b, 0x10c, 0x10d, 0x10e, 0x111, 0x112, 0x113,
	0x114, 0x115, 0x116, 0x119, 0x11a, 0x11b, 0x11c, 0x11d,
	0x11e, 0x121, 0x122, 0x123, 0x124, 0x125, 0x126, 0x129,
	0x12a, 0x12b, 0x12c, 0x12d, 0x12e, 0x131, 0x132, 0x133,
	0x134, 0x135, 0x136, 0x139, 0x13a, 0x13b, 0x13c, 0x13d,
	0x13e, 0x142, 0x143, 0x144, 0x145, 0x146, 0x149, 0x14a,
	0x14b, 0x14c, 0x14d, 0x14e, 0x151, 0x152, 0x153, 0x154,
	0x155, 0x156, 0x159, 0x15a, 0x15b, 0x15c, 0x15d, 0x15e,
	0x161, 0x162, 0x163, 0x164, 0x165, 0x166, 0x169, 0x16a,
	0x16b, 0x16c, 0x16d, 0x16e, 0x171, 0x172, 0x173, 0x174,
	0x175, 0x176, 0x179, 0x17a, 0x17b, 0x17c, 0x17d, 0x17e,
	0x184, 0x185, 0x186, 0x189, 0x18a, 0x18b, 0x18c, 0x18d,
	0x18e, 0x191, 0x192, 0x193, 0x194, 0x195, 0x196, 0x199,
	0x19a, 0x19b, 0x19c, 0x19d, 0x19e, 0x1a1, 0x1a2, 0x1a3,
	0x1a4, 0x1a5, 0x1a6, 0x1a9, 0x1aa, 0x1ab, 0x1ac, 0x1ad,
	0x1ae, 0x1b1, 0x1b2, 0x1b3, 0x1b4, 0x1b5, 0x1b6, 0x1b9,
	0x1ba, 0x1bb, 0x1bc, 0x1bd, 0x1be, 0x1c2, 0x1c3, 0x1c4,
	0x1c5, 0x1c6, 0x1c9, 0x1ca, 0x1cb, 0x1cc, 0x1cd, 0x1ce,
	0x1d1, 0x1d2, 0x1d3, 0x1d4, 0x1d5, 0x1d6, 0x1d9, 0x1da,
	0x1db, 0x1dc, 0x1dd, 0x1de, 0x1e1, 0x1e2, 0x1e3, 0x1e4,
	0x1e5, 0x1e6, 0x1e9, 0x1ea, 0x1eb, 0x1ec, 0x1ed, 0x1ee
};

static guint8 gcr_decode[512];

#define  GCR_MASK  ((guint16) 0x01ff)


/*
 * ========================================================================
 *
 *                              CODEC Functions
 *
 * ========================================================================
 */

/* use inline assembly if possible */
#if 1
#define  rolw(x, n)  asm("rolw %2, %0" : "=r" (x) : "0" (x), "c" (n))
#else
#define  rolw(x, n)  do { x = bswap_16(x) >> (8 - (n)) } while(0)
#endif


static void gcr_demodulate(guint8 *src, guint8 *dst, gint n)
{
	guint16 state = 0, rgstr;
	gint8 shift = 1;

	n >>= 3;

	while(1) {
		rgstr = __le16_to_cpu(get_unaligned((guint16 *) src++));
		rolw(rgstr, shift);
		if(state & 1)
			rgstr = ~rgstr;
		state ^= rgstr;
		*dst++ = gcr_decode[rgstr & GCR_MASK];
		if(++shift > 8) {
			if(!--n)
				break;
			src++;
			shift = 1;
		}
	}
}


static void gcr_modulate(guint8 *src, guint8 *dst, gint n)
{
	guint16 state = 0, rgstr = 0;
	gint8 shift = 7;

	n >>= 3;

	while(1) {
		if(state &= 1)
			state = GCR_MASK;
		state ^= gcr_encode[*src++];
		rgstr |= state << shift;
		if(--shift >= 0) {
			rgstr = bswap_16(rgstr);
			*dst++ = rgstr; /* write low byte */
			rgstr &= (guint16) 0xff00;
		} else {
			put_unaligned(__cpu_to_be16(rgstr), (guint16 *) dst);
			if(!--n)
				break;
			dst += sizeof(guint16);
			rgstr = 0;
			shift = 7;
		}
	}
}


/*
 * ============================================================================
 *
 *                       GStreamer Encoder Support Code
 *
 * ============================================================================
 */

/*
 * Properties
 */

enum property {
	ARG_VIDEOMODE = 1,
	ARG_BITDENSITY,
};


static void enc_set_property(GObject *object, enum property id, const GValue *value, GParamSpec *pspec)
{
	BkrGCREnc *filter = BKR_GCRENC(object);

	switch(id) {
	case ARG_VIDEOMODE:
		filter->videomode = g_value_get_enum(value);
		break;

	case ARG_BITDENSITY:
		filter->bitdensity = g_value_get_enum(value);
		break;
	}

	filter->format = format(filter->videomode, filter->bitdensity);
}


static void enc_get_property(GObject *object, enum property id, GValue *value, GParamSpec *pspec)
{
	BkrGCREnc *filter = BKR_GCRENC(object);

	switch(id) {
	case ARG_VIDEOMODE:
		g_value_set_enum(value, filter->videomode);
		break;

	case ARG_BITDENSITY:
		g_value_set_enum(value, filter->bitdensity);
		break;
	}
}


/*
 * Chain function.  See
 *
 * file:///usr/share/doc/gstreamer0.8-doc/gstreamer-0.8/GstPad.html#GstPadChainFunction
 */

static void enc_chain(GstPad *pad, GstData *data)
{
	BkrGCREnc *filter = BKR_GCRENC(GST_OBJECT_PARENT(pad));
	GstBuffer *inbuf = GST_BUFFER(data);
	GstBuffer *outbuf;

	g_return_if_fail(inbuf != NULL);

	/* check that element properties are set */
	g_return_if_fail(filter->format.capacity != 0);

	/* modulate data and move it along */
	if(GST_BUFFER_SIZE(inbuf) >= filter->format.capacity) {
		outbuf = gst_pad_alloc_buffer(filter->srcpad, 0, filter->format.capacity + filter->format.modulation_pad);
		gcr_modulate(GST_BUFFER_DATA(inbuf), GST_BUFFER_DATA(outbuf), filter->format.capacity);
		gst_pad_push(filter->srcpad, GST_DATA(outbuf));
	}

	gst_data_unref(data);
}


/*
 * Class init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GClassInitFunc
 */

static GstElementClass *enc_parent_class = NULL;

static void enc_class_init(BkrGCREncClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS(class);

	g_object_class_install_property(object_class, ARG_VIDEOMODE, g_param_spec_enum("videomode", "Video mode", "Video mode", BKR_TYPE_VIDEOMODE, DEFAULT_VIDEOMODE, G_PARAM_READWRITE));
	g_object_class_install_property(object_class, ARG_BITDENSITY, g_param_spec_enum("bitdensity", "Bit density", "Bit density", BKR_TYPE_BITDENSITY, DEFAULT_BITDENSITY, G_PARAM_READWRITE));
	object_class->set_property = enc_set_property;
	object_class->get_property = enc_get_property;

	enc_parent_class = g_type_class_ref(GST_TYPE_ELEMENT);
}


/*
 * Base init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GBaseInitFunc
 */

static void enc_base_init(BkrGCREncClass *class)
{
	GstElementClass *element_class = GST_ELEMENT_CLASS(class);
	static GstElementDetails plugin_details = {
		"Backer GCR Encoder",
		"Filter",
		"Backer run-length limiting bit modulation encoder",
		"Kipp Cannon <kipp@gravity.phys.uwm.edu>"
	};

	gst_element_class_set_details(element_class, &plugin_details);
}


/*
 * Instance init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GInstanceInitFunc
 */

static void enc_instance_init(BkrGCREnc *filter)
{
	GstElementClass *class = GST_ELEMENT_GET_CLASS(filter);

	/* input, "sink", pad.  No link function because pad can accept
	 * anything as input */
	filter->sinkpad = gst_pad_new("sink", GST_PAD_SINK);
	gst_pad_set_chain_function(filter->sinkpad, enc_chain);
	gst_element_add_pad(GST_ELEMENT(filter), filter->sinkpad);

	/* output, "source", pad.  No link function because pad sends a
	 * typeless byte stream */
	filter->srcpad = gst_pad_new("src", GST_PAD_SRC);
	gst_element_add_pad(GST_ELEMENT(filter), filter->srcpad);
}


/*
 * bkr_gcrenc_get_type().
 */

GType bkr_gcrenc_get_type(void)
{
	static GType type = 0;

	if (!type) {
		static const GTypeInfo info = {
			.class_size = sizeof(BkrGCREncClass),
			.class_init = (GClassInitFunc) enc_class_init,
			.base_init = (GBaseInitFunc) enc_base_init,
			.instance_size = sizeof(BkrGCREnc),
			.instance_init = (GInstanceInitFunc) enc_instance_init,
		};
		int  i;
		memset(gcr_decode, (gint8) -1, 512);
		for(i = 0; i < 256; i++)
			gcr_decode[gcr_encode[i]] = i;
		type = g_type_register_static(GST_TYPE_ELEMENT, "BkrGCREnc", &info, 0);
	}
	return type;
}


/*
 * ============================================================================
 *
 *                       GStreamer Decoder Support Code
 *
 * ============================================================================
 */

/*
 * Properties
 */

static void dec_set_property(GObject *object, enum property id, const GValue *value, GParamSpec *pspec)
{
	BkrGCRDec *filter = BKR_GCRDEC(object);

	switch(id) {
	case ARG_VIDEOMODE:
		filter->videomode = g_value_get_enum(value);
		break;

	case ARG_BITDENSITY:
		filter->bitdensity = g_value_get_enum(value);
		break;
	}

	filter->format = format(filter->videomode, filter->bitdensity);
}


static void dec_get_property(GObject *object, enum property id, GValue *value, GParamSpec *pspec)
{
	BkrGCRDec *filter = BKR_GCRDEC(object);

	switch(id) {
	case ARG_VIDEOMODE:
		g_value_set_enum(value, filter->videomode);
		break;

	case ARG_BITDENSITY:
		g_value_set_enum(value, filter->bitdensity);
		break;
	}
}


/*
 * Chain function.  See
 *
 * file:///usr/share/doc/gstreamer0.8-doc/gstreamer-0.8/GstPad.html#GstPadChainFunction
 */

static void dec_chain(GstPad *pad, GstData *data)
{
	BkrGCRDec *filter = BKR_GCRDEC(GST_OBJECT_PARENT(pad));
	GstBuffer *inbuf = GST_BUFFER(data);
	GstBuffer *outbuf;

	g_return_if_fail(inbuf != NULL);

	/* check that element properties are set */
	g_return_if_fail(filter->format.capacity != 0);

	/* demodulate data and move it along */
	if(GST_BUFFER_SIZE(inbuf) >= filter->format.capacity + filter->format.modulation_pad) {
		outbuf = gst_pad_alloc_buffer(filter->srcpad, 0, filter->format.capacity);
		gcr_demodulate(GST_BUFFER_DATA(inbuf), GST_BUFFER_DATA(outbuf), filter->format.capacity);
		gst_pad_push(filter->srcpad, GST_DATA(outbuf));
	}

	gst_data_unref(data);
}


/*
 * Parent class.
 */

static GstElementClass *dec_parent_class = NULL;


/*
 * Class init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GClassInitFunc
 */

static void dec_class_init(BkrGCRDecClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS(class);

	g_object_class_install_property(object_class, ARG_VIDEOMODE, g_param_spec_enum("videomode", "Video mode", "Video mode", BKR_TYPE_VIDEOMODE, DEFAULT_VIDEOMODE, G_PARAM_READWRITE));
	g_object_class_install_property(object_class, ARG_BITDENSITY, g_param_spec_enum("bitdensity", "Bit density", "Bit density", BKR_TYPE_BITDENSITY, DEFAULT_BITDENSITY, G_PARAM_READWRITE));
	object_class->set_property = dec_set_property;
	object_class->get_property = dec_get_property;

	dec_parent_class = g_type_class_ref(GST_TYPE_ELEMENT);
}


/*
 * Base init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GBaseInitFunc
 */

static void dec_base_init(BkrGCRDecClass *class)
{
	GstElementClass *element_class = GST_ELEMENT_CLASS(class);
	static GstElementDetails plugin_details = {
		"Backer GCR Decoder",
		"Filter",
		"Backer run-length limiting bit modulation decoder",
		"Kipp Cannon <kipp@gravity.phys.uwm.edu>"
	};

	gst_element_class_set_details(element_class, &plugin_details);
}


/*
 * Instance init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GInstanceInitFunc
 */

static void dec_instance_init(BkrGCRDec *filter)
{
	GstElementClass *class = GST_ELEMENT_GET_CLASS(filter);

	/* input, "sink", pad.  No link function because pad can accept
	 * anything as input */
	filter->sinkpad = gst_pad_new("sink", GST_PAD_SINK);
	gst_pad_set_chain_function(filter->sinkpad, dec_chain);
	gst_element_add_pad(GST_ELEMENT(filter), filter->sinkpad);

	/* output, "source", pad.  No link function because pad sends a
	 * typeless byte stream */
	filter->srcpad = gst_pad_new("src", GST_PAD_SRC);
	gst_element_add_pad(GST_ELEMENT(filter), filter->srcpad);
}


/*
 * bkr_gcrdec_get_type().
 */

GType bkr_gcrdec_get_type(void)
{
	static GType type = 0;

	if (!type) {
		static const GTypeInfo info = {
			.class_size = sizeof(BkrGCRDecClass),
			.class_init = (GClassInitFunc) dec_class_init,
			.base_init = (GBaseInitFunc) dec_base_init,
			.instance_size = sizeof(BkrGCRDec),
			.instance_init = (GInstanceInitFunc) dec_instance_init,
		};
		type = g_type_register_static(GST_TYPE_ELEMENT, "BkrGCRDec", &info, 0);
	}
	return type;
}
