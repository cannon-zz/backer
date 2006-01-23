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
#include <bkr_elements.h>
#include <bkr_bytes.h>
#include <bkr_splp.h>
#include <bkr_splp_randomize.h>
#include <rs.h>


/*
 * ========================================================================
 *
 *                              PARAMETERS
 *
 * ========================================================================
 */

/*
 * 28 bit sector number = about 1243 hours of NTSC, or 1491 hours of PAL.
 */

#define  SPLP_TIMEOUT_MULT       20
#define  BKR_SECTOR_NUMBER_BITS  28
#define  BKR_LOW_USED_BITS       4
#define  BKR_FILLER              0x33
#define  BOR_LENGTH              5		/* seconds */
#define  EOR_LENGTH              1		/* seconds */


/*
 * Format info.
 */

static struct bkr_splp_format format(enum bkr_videomode v, enum bkr_bitdensity d, enum bkr_sectorformat f)
{
	switch(d) {
	case BKR_LOW:
		switch(v) {
		case BKR_NTSC:
			switch(f) {
			case BKR_EP:
				return (struct bkr_splp_format) { 720,  96,  716, 12};
			case BKR_SP:
				return (struct bkr_splp_format) { 830, 100,  826, 10};
			}
		case BKR_PAL:
			switch(f) {
			case BKR_EP:
				return (struct bkr_splp_format) { 888,  96,  884, 12};
			case BKR_SP:
				return (struct bkr_splp_format) { 980, 140,  976, 14};
			}
		}
	case BKR_HIGH:
		switch(v) {
		case BKR_NTSC:
			switch(f) {
			case BKR_EP:
				return (struct bkr_splp_format) {1848, 224, 1844, 28};
			case BKR_SP:
				return (struct bkr_splp_format) {2160, 200, 2156, 20};
			}
		case BKR_PAL:
			switch(f) {
			case BKR_EP:
				return (struct bkr_splp_format) {2288, 208, 2284, 26};
			case BKR_SP:
				return (struct bkr_splp_format) {2618, 220, 2614, 22};
			}
		}
	}

	return (struct bkr_splp_format) {0,};
}


/*
 * ========================================================================
 *
 *                        Sector Header Handling
 *
 * ========================================================================
 */

#if __BKR_BYTE_ORDER == __LITTLE_ENDIAN

typedef struct {
	gint32  number : BKR_SECTOR_NUMBER_BITS;
	guint32  low_used : BKR_LOW_USED_BITS;
} bkr_sector_header_t;

#else

typedef struct {
	guint32  low_used : BKR_LOW_USED_BITS;
	gint32  number : BKR_SECTOR_NUMBER_BITS;
} bkr_sector_header_t;

#endif /* __BKR_BYTE_ORDER */

/* FIXME: the next two functions use aliasing, which is not guaranteed to
 * work on all platforms.  There are tricks to get it to work, but I don't
 * know what they are... */

static bkr_sector_header_t get_sector_header(guint8 *data, struct bkr_splp_format format)
{
	union {
		guint32 as_int;
		bkr_sector_header_t as_header;
	} header;

	header.as_int = get_unaligned((guint32 *) (data + format.capacity));
	header.as_int = __le32_to_cpu(header.as_int);

	return header.as_header;
}


static void put_sector_header(guint8 *data, struct bkr_splp_format format, gint sector_number, gint encoded_length)
{
	union {
		guint32 as_int;
		bkr_sector_header_t as_header;
	} header = {
		.as_header = {
			.number = sector_number,
			.low_used = encoded_length
		}
	};

	header.as_int = __cpu_to_le32(header.as_int);
	memcpy(data + format.capacity, &header.as_header, sizeof(header));
}


static guint8 *high_used(guint8 *data, struct bkr_splp_format format)
{
	return data + format.capacity - 1;
}


static guint encode_sector_length(guint length)
{
	return length + (length / 15) + 1;
}


static guint decode_sector_length(guint high, guint low)
{
	return (high * 15) + low - 1;
}


/*
 * ========================================================================
 *
 *                              SP/LP CODEC
 *
 * ========================================================================
 */

/*
 * Retrieves the next sector from the I/O buffer.  The algorithm is to loop
 * until we either find an acceptable sector (it's a non-BOR sector and is
 * in the correct order) or we encounter an error.  If we find a sector we
 * like, we set it up for a read out and return 0;  otherwise we return < 0
 * on error.  On error, a retry can be attempted by simply re-calling this
 * function.  EOF is indicated by a sector being returned with no data in
 * it.
 *
 * We tolerate junk sectors for the first little while in order to
 * transparently skip over the noise found at the beginning of recordings.
 * This is done by silently skipping over all errors (except -EAGAIN) until
 * we find a valid data sector after which full error reporting is
 * restored.  When reporting errors, -ENODATA is returned exactly once for
 * each irrecoverable sector.
 */

static gint correct_sector(BkrSPLPDec *filter, guint8 *data)
{
	guint8 *parity = data + filter->format.data_size;
	gint block, bytes_corrected;
	gint result = 0;

	/* FIXME: remove this to enable error correction */
	return 0;

	filter->header_is_good = 1;
	for(block = 0; block < filter->format.interleave; block++) {
		bytes_corrected = reed_solomon_decode(parity + block, data + block, 0, *filter->rs_format);
		/* block is uncorrectable? */
		if(bytes_corrected < 0) {
			result = -ENODATA;
			/* block contains header? */
			if(block >= filter->format.interleave - sizeof(bkr_sector_header_t))
				filter->header_is_good = 0;
			continue;
		}
		filter->bytes_corrected += bytes_corrected;
		if(bytes_corrected > filter->worst_block)
			filter->worst_block = bytes_corrected;
		if(bytes_corrected > filter->recent_block)
			filter->recent_block = bytes_corrected;
	}

	return result;
}


static void reset_statistics(BkrSPLPDec *filter)
{
	filter->bytes_corrected = 0;
	filter->worst_block = 0;
	filter->recent_block = 0;
	filter->bad_sectors = 0;
	filter->lost_runs = 0;	/* FIXME: not used? */
	filter->duplicate_runs = 0;
}


static GstBuffer* decode_sector(BkrSPLPDec *filter, GstBuffer *buffer)
{
	guint8 *data = GST_BUFFER_DATA(buffer);
	bkr_sector_header_t header;

	/*
	 * Perform error correction.  If this fails, move to the next
	 * sector.
	 */

	if(correct_sector(filter, data) < 0) {
		filter->bad_sectors++;
		return NULL;
	}

	/*
	 * Extract sector header.
	 */

	header = get_sector_header(data, filter->format);
	filter->decoded_number = header.number;

	/*
	 * If a BOR sector, reset error counters and move to next sector.
	 */

	if(header.number < 0) {
		reset_statistics(filter);
		return NULL;
	}

	/*
	 * If a duplicate, move to next sector.
	 */

	if(header.number <= filter->sector_number) {
		filter->duplicate_runs += filter->not_underrunning;
		filter->not_underrunning = 0;
		return NULL;
	}
	/* FIXME: at this point, we have decoded a sector numbered
	 * "decoded_number".  The last sector we successfully decoded
	 * (passed on to the next element in the chain) was numbered
	 * "sector_number".  If decoded_number = sector_number + 1, then
	 * this is the next sector in the sequence, and the one we want.
	 * If decoded_number <= sector_number then this is a duplicate
	 * sector and should be discarded (we handle this correctly above).
	 * If decoded_number > sector_number + 1, then we have skipped
	 * sectors.  The old codec would return exactly 1 -ENODATA for each
	 * skipped sector, which facilitated the implementation of the
	 * secondary error correction codec.  I think the gstreamer way to
	 * handle this is to define an event type for "skipped sector" and
	 * push a number of those events onto the src pad equal to the
	 * number of skipped sectors.  Anyway, the currect codec does not
	 * handle skipped sectors.
	 */
	filter->sector_number = filter->decoded_number;

	/*
	 * Sector is the one we want.
	 */

	filter->not_underrunning = 1;

	/*
	 * Determine the sector length and de-randomize it (it is safe for
	 * the randomizer to go past the end of the sector by a few bytes).
	 */

	if(header.low_used)
		GST_BUFFER_SIZE(buffer) = decode_sector_length(*high_used(data, filter->format), header.low_used);
	else
		GST_BUFFER_SIZE(buffer) = filter->format.capacity;
	bkr_splp_sector_randomize(data, GST_BUFFER_SIZE(buffer), header.number);

	return buffer;
}


/*
 * Encode one buffer by randomizing the data, inserting the sector header,
 * and computing parity bytes.
 */

static void encode_sector(BkrSPLPEnc *filter, GstBuffer *buffer)
{
	guint8 *data = GST_BUFFER_DATA(buffer);
	gint size = GST_BUFFER_SIZE(buffer);
	int  block;

	/*
	 * Randomize the data (it is safe for the randomizer to go past the
	 * end of the data area by a few bytes because there is space
	 * allocated for the parity bytes).
	 */

	bkr_splp_sector_randomize(data, size, filter->sector_number);

	/*
	 * Pad unused space, encode the data length, insert the header.
	 */

	if(size < filter->format.capacity) {
		memset(data + size, BKR_FILLER, filter->format.capacity - size - 1);
		size = encode_sector_length(size);
		*high_used(data, filter->format) = size >> BKR_LOW_USED_BITS;
		put_sector_header(data, filter->format, filter->sector_number, size);
	} else
		put_sector_header(data, filter->format, filter->sector_number, 0);
	GST_BUFFER_SIZE(buffer) = filter->format.data_size;

	/*
	 * Generate parity bytes.
	 */

	for(block = 0; block < filter->format.interleave; block++)
		reed_solomon_encode(data + filter->format.data_size + block, data + block, *filter->rs_format);
	GST_BUFFER_SIZE(buffer) += filter->format.parity_size;

	/*
	 * Reset for the next sector.
	 */
	
	filter->sector_number++;
}


/*
 * Generate EOR mark.
 */

static void write_empty_sectors(BkrSPLPEnc *filter, gint n)
{
	GstBuffer *buffer;

	while(n--) {
		buffer = gst_pad_alloc_buffer(filter->srcpad, 0, filter->format.data_size + filter->format.parity_size);
		GST_BUFFER_SIZE(buffer) = 0;
		encode_sector(filter, buffer);
		gst_pad_push(filter->srcpad, GST_DATA(buffer));
	}
}


static void write_eor(BkrSPLPEnc *filter)
{
	write_empty_sectors(filter, bkr_fields_per_second(filter->videomode) * EOR_LENGTH);
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
	ARG_SECTORFORMAT
};


static void enc_set_property(GObject *object, enum property id, const GValue *value, GParamSpec *pspec)
{
	BkrSPLPEnc *filter = BKR_SPLPENC(object);

	switch(id) {
	case ARG_VIDEOMODE:
		filter->videomode = g_value_get_enum(value);
		break;

	case ARG_BITDENSITY:
		filter->bitdensity = g_value_get_enum(value);
		break;

	case ARG_SECTORFORMAT:
		filter->sectorformat = g_value_get_enum(value);
		break;
	}

	filter->format = format(filter->videomode, filter->bitdensity, filter->sectorformat);
	filter->sector_number = -bkr_fields_per_second(filter->videomode) * BOR_LENGTH;

	reed_solomon_codec_free(filter->rs_format);
	filter->rs_format = NULL;
	if(filter->format.interleave) {
		filter->rs_format = reed_solomon_codec_new((filter->format.data_size + filter->format.parity_size) / filter->format.interleave, filter->format.data_size / filter->format.interleave, filter->format.interleave);
		if(!filter->rs_format) {
			/* FIXME */
		}
	}
}


static void enc_get_property(GObject *object, enum property id, GValue *value, GParamSpec *pspec)
{
	BkrSPLPEnc *filter = BKR_SPLPENC(object);

	switch(id) {
	case ARG_VIDEOMODE:
		g_value_set_enum(value, filter->videomode);
		break;

	case ARG_BITDENSITY:
		g_value_set_enum(value, filter->bitdensity);
		break;

	case ARG_SECTORFORMAT:
		g_value_set_enum(value, filter->sectorformat);
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
	BkrSPLPEnc *filter = BKR_SPLPENC(GST_OBJECT_PARENT(pad));
	GstBuffer *buffer = GST_BUFFER(data);

	/* check that element has been initialized */
	g_return_if_fail(filter->format.interleave != 0);

	/*
	 * Generate the BOR mark if this is the start of the record.
	 */

	if(filter->sector_number < 0)
		write_empty_sectors(filter, -filter->sector_number);

	/*
	 * Check for end of stream.
	 */

	if(GST_IS_EVENT(data)) {
		GstEvent *event = GST_EVENT(data);
		switch(GST_EVENT_TYPE(event)) {
		case GST_EVENT_EOS:
			write_eor(filter);
			break;
#if 0
		case GST_EVENT_NEWSEGMENT:
			break;
#endif
		default:
			break;
		}
		gst_pad_event_default(pad, event);
		return;
	}

	/*
	 * No-op if buffer is too big.
	 */

	if(GST_BUFFER_SIZE(buffer) > filter->format.capacity) {
		gst_data_unref(data);
		return;
	}

	/*
	 * Allocate new, larger, buffer if buffer is too small.
	 */

	if(GST_BUFFER_MAXSIZE(buffer) < filter->format.data_size + filter->format.parity_size) {
		GstBuffer *new = gst_pad_alloc_buffer(filter->srcpad, GST_BUFFER_OFFSET(buffer), filter->format.data_size + filter->format.parity_size);
		memcpy(GST_BUFFER_DATA(new), GST_BUFFER_DATA(buffer), GST_BUFFER_SIZE(buffer));
		GST_BUFFER_SIZE(new) = GST_BUFFER_SIZE(buffer);
		gst_buffer_unref(buffer);
		buffer = new;
	}

	/*
	 * Encode buffer.
	 */

	encode_sector(filter, buffer);

	gst_pad_push(filter->srcpad, GST_DATA(buffer));
}


/*
 * Buffer alloc function.  See
 *
 * file:///usr/share/doc/gstreamer0.8-doc/gstreamer-0.8/GstPad.html#GstPadBufferAllocFunction
 *
 * We make sure to return a buffer large enough to do an in-place encode of
 * the data.
 */

GstBuffer *enc_bufferalloc(GstPad *pad, guint64 offset, guint size)
{
	BkrSPLPEnc *filter = BKR_SPLPENC(GST_OBJECT_PARENT(pad));
	GstBuffer *buf;

	if(size > filter->format.capacity)
		return NULL;

	buf = gst_pad_alloc_buffer(filter->srcpad, offset, filter->format.data_size + filter->format.parity_size);
	GST_BUFFER_SIZE(buf) = size;

	return buf;
}


/*
 * Parent class.
 */

static GstElementClass *enc_parent_class = NULL;


/*
 * Instance finalize function.  See ???
 */

static void enc_finalize(GObject *object)
{
	BkrSPLPEnc *filter = BKR_SPLPENC(object);

	reed_solomon_codec_free(filter->rs_format);

	G_OBJECT_CLASS(enc_parent_class)->finalize(object);
}


/*
 * Class init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GClassInitFunc
 */

static void enc_class_init(BkrSPLPEncClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS(class);

	g_object_class_install_property(object_class, ARG_VIDEOMODE, g_param_spec_enum("videomode", "Video mode", "Video mode", BKR_TYPE_VIDEOMODE, DEFAULT_VIDEOMODE, G_PARAM_READWRITE));
	g_object_class_install_property(object_class, ARG_BITDENSITY, g_param_spec_enum("density", "Density", "Bit density", BKR_TYPE_BITDENSITY, DEFAULT_BITDENSITY, G_PARAM_READWRITE));
	g_object_class_install_property(object_class, ARG_SECTORFORMAT, g_param_spec_enum("format", "Format", "Sector format", BKR_TYPE_SECTORFORMAT, DEFAULT_SECTORFORMAT, G_PARAM_READWRITE));
	object_class->set_property = enc_set_property;
	object_class->get_property = enc_get_property;

	object_class->finalize = enc_finalize;

	enc_parent_class = g_type_class_ref(GST_TYPE_ELEMENT);
}


/*
 * Base init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GBaseInitFunc
 */

static void enc_base_init(BkrSPLPEncClass *class)
{
	GstElementClass *element_class = GST_ELEMENT_CLASS(class);
	static GstElementDetails plugin_details = {
		"Backer SP/LP Encoder",
		"Filter",
		"Backer SP/LP error correction and record encoder",
		"Kipp Cannon <kipp@gravity.phys.uwm.edu>"
	};

	gst_element_class_set_details(element_class, &plugin_details);
}


/*
 * Instance init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GInstanceInitFunc
 */

static void enc_instance_init(BkrSPLPEnc *filter)
{
	GstElementClass *class = GST_ELEMENT_GET_CLASS(filter);

	/* input, "sink", pad.  No link function because pad can accept
	 * anything as input */
	filter->sinkpad = gst_pad_new("sink", GST_PAD_SINK);
	gst_pad_set_chain_function(filter->sinkpad, enc_chain);
	gst_pad_set_bufferalloc_function(filter->sinkpad, enc_bufferalloc);
	gst_element_add_pad(GST_ELEMENT(filter), filter->sinkpad);

	/* output, "source", pad.  No link function because pad sends a
	 * typeless byte stream */
	filter->srcpad = gst_pad_new("src", GST_PAD_SRC);
	gst_element_add_pad(GST_ELEMENT(filter), filter->srcpad);

	/* internal state */
	filter->rs_format = NULL;
	filter->sector_number = 0;
}


/*
 * bkr_splpenc_get_type().
 */

GType bkr_splpenc_get_type(void)
{
	static GType type = 0;

	if (!type) {
		static const GTypeInfo info = {
			.class_size = sizeof(BkrSPLPEncClass),
			.class_init = (GClassInitFunc) enc_class_init,
			.base_init = (GBaseInitFunc) enc_base_init,
			.instance_size = sizeof(BkrSPLPEnc),
			.instance_init = (GInstanceInitFunc) enc_instance_init,
		};
		type = g_type_register_static(GST_TYPE_ELEMENT, "BkrSPLPEnc", &info, 0);
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
	BkrSPLPDec *filter = BKR_SPLPDEC(object);

	switch(id) {
	case ARG_VIDEOMODE:
		filter->videomode = g_value_get_enum(value);
		break;

	case ARG_BITDENSITY:
		filter->bitdensity = g_value_get_enum(value);
		break;

	case ARG_SECTORFORMAT:
		filter->sectorformat = g_value_get_enum(value);
		break;
	}

	filter->format = format(filter->videomode, filter->bitdensity, filter->sectorformat);
	reset_statistics(filter);

	reed_solomon_codec_free(filter->rs_format);
	filter->rs_format = NULL;
	if(filter->format.interleave) {
		filter->rs_format = reed_solomon_codec_new((filter->format.data_size + filter->format.parity_size) / filter->format.interleave, filter->format.data_size / filter->format.interleave, filter->format.interleave);
		if(!filter->rs_format) {
			/* FIXME */
		}
	}
}


static void dec_get_property(GObject *object, enum property id, GValue *value, GParamSpec *pspec)
{
	BkrSPLPDec *filter = BKR_SPLPDEC(object);

	switch(id) {
	case ARG_VIDEOMODE:
		g_value_set_enum(value, filter->videomode);
		break;

	case ARG_BITDENSITY:
		g_value_set_enum(value, filter->bitdensity);
		break;

	case ARG_SECTORFORMAT:
		g_value_set_enum(value, filter->sectorformat);
		break;
	}
}


/*
 * Chain function.  See
 *
 * file:///usr/share/doc/gstreamer0.8-doc/gstreamer-0.8/GstPad.html#GstPadChainFunction
 */

static void dec_chain(GstPad *pad, GstData *in)
{
	BkrSPLPDec *filter = BKR_SPLPDEC(GST_OBJECT_PARENT(pad));
	GstBuffer *outbuf;

	/* check that element has been initialized */
	g_return_if_fail(filter->format.interleave != 0);

	outbuf = decode_sector(filter, GST_BUFFER(in));
	if(outbuf)
		gst_pad_push(filter->srcpad, GST_DATA(outbuf));
	else
		gst_data_unref(in);
}


/*
 * Parent class.
 */

static GstElementClass *dec_parent_class = NULL;


/*
 * Instance finalize function.  See ???
 */

static void dec_finalize(GObject *object)
{
	BkrSPLPDec *filter = BKR_SPLPDEC(object);

	reed_solomon_codec_free(filter->rs_format);

	G_OBJECT_CLASS(dec_parent_class)->finalize(object);
}


/*
 * Class init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GClassInitFunc
 */

static void dec_class_init(BkrSPLPDecClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS(class);

	g_object_class_install_property(object_class, ARG_VIDEOMODE, g_param_spec_enum("videomode", "Video mode", "Video mode", BKR_TYPE_VIDEOMODE, DEFAULT_VIDEOMODE, G_PARAM_READWRITE));
	g_object_class_install_property(object_class, ARG_BITDENSITY, g_param_spec_enum("density", "Density", "Bit density", BKR_TYPE_BITDENSITY, DEFAULT_BITDENSITY, G_PARAM_READWRITE));
	g_object_class_install_property(object_class, ARG_SECTORFORMAT, g_param_spec_enum("format", "Format", "Sector format", BKR_TYPE_SECTORFORMAT, DEFAULT_SECTORFORMAT, G_PARAM_READWRITE));
	object_class->set_property = dec_set_property;
	object_class->get_property = dec_get_property;

	object_class->finalize = dec_finalize;

	dec_parent_class = g_type_class_ref(GST_TYPE_ELEMENT);
}


/*
 * Base init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GBaseInitFunc
 */

static void dec_base_init(BkrSPLPDecClass *class)
{
	GstElementClass *element_class = GST_ELEMENT_CLASS(class);
	static GstElementDetails plugin_details = {
		"Backer SP/LP Decoder",
		"Filter",
		"Backer SP/LP error correction and record decoder",
		"Kipp Cannon <kipp@gravity.phys.uwm.edu>"
	};

	gst_element_class_set_details(element_class, &plugin_details);
}


/*
 * Instance init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GInstanceInitFunc
 */

static void dec_instance_init(BkrSPLPDec *filter)
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

	/* internal state */
	filter->rs_format = NULL;
	filter->sector_number = -1;	/* first sector we want is 0 */
}


/*
 * bkr_splpdec_get_type().
 */

GType bkr_splpdec_get_type(void)
{
	static GType type = 0;

	if (!type) {
		static const GTypeInfo info = {
			.class_size = sizeof(BkrSPLPDecClass),
			.class_init = (GClassInitFunc) dec_class_init,
			.base_init = (GBaseInitFunc) dec_base_init,
			.instance_size = sizeof(BkrSPLPDec),
			.instance_init = (GInstanceInitFunc) dec_instance_init,
		};
		type = g_type_register_static(GST_TYPE_ELEMENT, "BkrSPLPDec", &info, 0);
	}
	return type;
}
