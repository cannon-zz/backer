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


static struct bkr_splp_format *compute_format(enum bkr_videomode v, enum bkr_bitdensity d, enum bkr_sectorformat f)
{
	struct bkr_splp_format initializer;
	struct bkr_splp_format *format;

	switch(d) {
	case BKR_LOW:
		switch(v) {
		case BKR_NTSC:
			switch(f) {
			case BKR_EP:
				initializer = (struct bkr_splp_format) { 720,  96,  716, 12};
				break;
			case BKR_SP:
				initializer = (struct bkr_splp_format) { 830, 100,  826, 10};
				break;
			default:
				GST_DEBUG("unrecognized sectorformat");
				return NULL;
			}
			break;
		case BKR_PAL:
			switch(f) {
			case BKR_EP:
				initializer = (struct bkr_splp_format) { 888,  96,  884, 12};
				break;
			case BKR_SP:
				initializer = (struct bkr_splp_format) { 980, 140,  976, 14};
				break;
			default:
				GST_DEBUG("unrecognized sectorformat");
				return NULL;
			}
			break;
		default:
			GST_DEBUG("unrecognized videomode");
			return NULL;
		}
		break;
	case BKR_HIGH:
		switch(v) {
		case BKR_NTSC:
			switch(f) {
			case BKR_EP:
				initializer = (struct bkr_splp_format) {1848, 224, 1844, 28};
				break;
			case BKR_SP:
				initializer = (struct bkr_splp_format) {2160, 200, 2156, 20};
				break;
			default:
				GST_DEBUG("unrecognized sectorformat");
				return NULL;
			}
			break;
		case BKR_PAL:
			switch(f) {
			case BKR_EP:
				initializer = (struct bkr_splp_format) {2288, 208, 2284, 26};
				break;
			case BKR_SP:
				initializer = (struct bkr_splp_format) {2618, 220, 2614, 22};
				break;
			default:
				GST_DEBUG("unrecognized sectorformat");
				return NULL;
			}
			break;
		default:
			GST_DEBUG("unrecognized videomode");
			return NULL;
		}
		break;
	default:
		GST_DEBUG("unrecognized bitdensity");
		return NULL;
	}

	format = malloc(sizeof(*format));
	if(!format) {
		GST_DEBUG("memory allocation failure");
		return NULL;
	}

	*format = initializer;

	return format;
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
	gint32  sector_number : BKR_SECTOR_NUMBER_BITS;
	guint32  low_used : BKR_LOW_USED_BITS;
} bkr_sector_header_t;


#else


typedef struct {
	guint32  low_used : BKR_LOW_USED_BITS;
	gint32  sector_number : BKR_SECTOR_NUMBER_BITS;
} bkr_sector_header_t;


#endif /* __BKR_BYTE_ORDER */


/* FIXME: the next two functions use aliasing, which is not guaranteed to
 * work on all platforms.  There are tricks to get it to work, but I don't
 * know what they are... */


static bkr_sector_header_t get_sector_header(const guint8 *data, const struct bkr_splp_format *format)
{
	union {
		guint32 as_int;
		bkr_sector_header_t as_header;
	} header;

	header.as_int = get_unaligned((guint32 *) (data + format->capacity));
	header.as_int = __le32_to_cpu(header.as_int);

	return header.as_header;
}


static void put_sector_header(guint8 *data, const struct bkr_splp_format *format, int sector_number, int encoded_length)
{
	union {
		guint32 as_int;
		bkr_sector_header_t as_header;
	} header = {
		.as_header = {
			.sector_number = sector_number,
			.low_used = encoded_length
		}
	};

	header.as_int = __cpu_to_le32(header.as_int);
	memcpy(data + format->capacity, &header.as_header, sizeof(header));
}


static guint8 *high_used(guint8 *data, const struct bkr_splp_format *format)
{
	return data + format->capacity - 1;
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
 * Container to hold the results of the sector decode process.
 */


struct sector_decode_status {
	int sector_is_valid;	/* sector is error free */
	int header_is_valid;	/* sector header is error free */
	int sector_is_bor;	/* sector is part of BOR mark */
	int sector_is_eor;	/* sector is part of EOR mark */
	int sector_is_duplicate;	/* sector a repeat (ignore) */
	int sectors_skipped;	/* this many sectors have been skipped */
};


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


static struct sector_decode_status correct_sector(BkrSPLPDec *filter, guint8 *data)
{
	guint8 *parity = data + filter->format->data_size;
	int block, bytes_corrected;
	struct sector_decode_status status = {
		.sector_is_valid = 1,
		.header_is_valid = 1,
		.sector_is_bor = 0,
		.sector_is_eor = 0,
		.sector_is_duplicate = 0,
		.sectors_skipped = 0
	};

	/* This pre-processor conditional disables error correction.
	 * Useful for confirming that the decoding pipeline is, infact, the
	 * inverse of the encoding pipeline (the error corrector could be
	 * hiding off-by-one problems by just fixing the data). */
#if 0
	return status;
#endif

	for(block = 0; block < filter->format->interleave; block++) {
		bytes_corrected = reed_solomon_decode(parity + block, data + block, 0, *filter->rs_format);
		/* block is uncorrectable? */
		if(bytes_corrected < 0) {
			status.sector_is_valid = 0;
			/* block contains header? */
			if(block >= filter->format->interleave - sizeof(bkr_sector_header_t))
				status.header_is_valid = 0;
			continue;
		}
		filter->bytes_corrected += bytes_corrected;
		if(bytes_corrected > filter->worst_block)
			filter->worst_block = bytes_corrected;
		if(bytes_corrected > filter->recent_block)
			filter->recent_block = bytes_corrected;
	}

	return status;
}


static void reset_statistics(BkrSPLPDec *filter)
{
	/* NOTE:  keep synchronized with defaults in dec_class_init() */
	filter->bytes_corrected = 0;
	filter->worst_block = 0;
	filter->recent_block = 0;
	filter->bad_sectors = 0;
	filter->lost_runs = 0;	/* FIXME: not used? */
	filter->duplicate_runs = 0;
}


static struct sector_decode_status decode_sector(BkrSPLPDec *filter, GstBuffer *buffer)
{
	guint8 *data = GST_BUFFER_DATA(buffer);
	bkr_sector_header_t header;
	struct sector_decode_status status;

	/*
	 * Incase something goes wrong.
	 */

	GST_BUFFER_OFFSET(buffer) = GST_BUFFER_OFFSET_END(buffer) = GST_BUFFER_OFFSET_NONE;

	/*
	 * Perform error correction.
	 */

	status = correct_sector(filter, data);
	if(!status.sector_is_valid)
		filter->bad_sectors++;
	if(!status.header_is_valid)
		/* can't do anything without a valid header */
		return status;

	/*
	 * Extract sector header.
	 */

	header = get_sector_header(data, filter->format);

	/*
	 * If a BOR sector, reset error counters and move to next sector.
	 */

	if(header.sector_number < 0) {
		status.sector_is_bor = 1;
		reset_statistics(filter);
		return status;
	}

	/*
	 * Set media-specific offset to the sector number.  There is
	 * nothing meaningful to set the "end" offset to.
	 */

	GST_BUFFER_OFFSET(buffer) = header.sector_number;

	/*
	 * If sector is a duplicate, then we're done.
	 */

	if(header.sector_number <= filter->sector_number) {
		filter->duplicate_runs += filter->not_underrunning;
		filter->not_underrunning = 0;
		status.sector_is_duplicate = 1;
		return status;
	}

	/* 
	 * At this point, we have decoded a sector whose number is recorded
	 * in the buffer's offset.  The last sector we decoded was numbered
	 * sector_number.  If offset = sector_number + 1, then this is the
	 * next sector in the sequence, and the one we want.  If offset <=
	 * sector_number then this is a duplicate (we have just handled
	 * this above).  If offset > sector_number + 1, then we have
	 * skipped some sectors
	 */

	status.sectors_skipped = header.sector_number - filter->sector_number - 1;
	filter->sector_number = header.sector_number;

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
		GST_BUFFER_SIZE(buffer) = filter->format->capacity;
	bkr_splp_sector_randomize(data, GST_BUFFER_SIZE(buffer), header.sector_number);

	/*
	 * Sector length = 0 --> EOR mark
	 */

	if(GST_BUFFER_SIZE(buffer) == 0)
		status.sector_is_eor = 1;

	return status;
}


/*
 * Encode one buffer by randomizing the data, inserting the sector header,
 * and computing parity bytes.
 */


static void encode_sector(BkrSPLPEnc *filter, GstBuffer *buffer, int sector_number)
{
	guint8 *data = GST_BUFFER_DATA(buffer);
	int size = GST_BUFFER_SIZE(buffer);
	int block;

	/*
	 * Randomize the data (it is safe for the randomizer to go past the
	 * end of the data area by a few bytes because there is space
	 * allocated for the parity bytes).
	 */

	bkr_splp_sector_randomize(data, size, sector_number);

	/*
	 * Pad unused space, encode the data length, insert the header.
	 */

	if(size < filter->format->capacity) {
		memset(data + size, BKR_FILLER, filter->format->capacity - size - 1);
		size = encode_sector_length(size);
		*high_used(data, filter->format) = size >> BKR_LOW_USED_BITS;
		put_sector_header(data, filter->format, sector_number, size);
	} else
		put_sector_header(data, filter->format, sector_number, 0);
	GST_BUFFER_SIZE(buffer) = filter->format->data_size;

	/*
	 * Generate parity bytes.
	 */

	for(block = 0; block < filter->format->interleave; block++)
		reed_solomon_encode(data + filter->format->data_size + block, data + block, *filter->rs_format);
	GST_BUFFER_SIZE(buffer) += filter->format->parity_size;
}


/*
 * Generate beginning-of-record and end-of-record marks.
 *
 * NOTE:  write_empty_sectors() requires the caps argument to be the caps
 * from which the filter's format structure was computed.  This function
 * will screw up if caps corresponds to a different format.  This is
 * enforced in enc_chain(), where write_empty_sectors() gets called (via
 * write_bor()) after a check for caps consistency.
 */


static GstFlowReturn write_empty_sectors(BkrSPLPEnc *filter, GstCaps *caps, int n)
{
	size_t buffer_size;
	GstFlowReturn result = GST_FLOW_OK;

	if(!filter->format) {
		GST_DEBUG("format == NULL");
		result = GST_FLOW_NOT_NEGOTIATED;
		goto done;
	}

	buffer_size = filter->format->data_size + filter->format->parity_size;

	while(n--) {
		GstBuffer *buf;

		result = gst_pad_alloc_buffer(filter->srcpad, GST_BUFFER_OFFSET_NONE, buffer_size, caps, &buf);
		if(result != GST_FLOW_OK) {
			GST_DEBUG("gst_pad_alloc_buffer() failed");
			goto done;
		}

		GST_BUFFER_SIZE(buf) = 0;
		/* note that sector_number is not incremented:  all BOR
		 * sectors have the same sector number (-1), and all EOR
		 * sectors have the same sector number (whatever it had
		 * gotten upto when the recording ended */
		encode_sector(filter, buf, filter->sector_number);

		result = gst_pad_push(filter->srcpad, buf);
		if(result != GST_FLOW_OK) {
			GST_DEBUG("gst_pad_alloc_buffer() failed");
			goto done;
		}
	}

done:
	return result;
}


static GstFlowReturn write_bor(BkrSPLPEnc *filter, GstCaps *caps)
{
	GstFlowReturn result;

	filter->sector_number = -1;
	result = write_empty_sectors(filter, caps, BOR_LENGTH * bkr_fields_per_second(filter->videomode));
	filter->sector_number = 0;

	return result;
}


static GstFlowReturn write_eor(BkrSPLPEnc *filter, GstCaps *caps)
{
	return write_empty_sectors(filter, caps, EOR_LENGTH * bkr_fields_per_second(filter->videomode));
}


/*
 * Write a regular (possibly short) data sector.  Takes as much data as
 * will fit into a sector from the filter's adapter, encodes it, and pushes
 * it out the srcpad.  If there isn't enough data in the adapter to fill a
 * sector then a short sector is encoded.  This is a waste of tape if this
 * is not the end of the input stream.  Note, also, that if the adapter is
 * empty the result will be a sector containing 0 bytes, which will be
 * interpreted as the end-of-record marker on decode.  You must check that
 * there is infact data (and enough data) in the adapter before calling
 * this function.
 */


#ifndef min
#define min(x,y) ({ \
	const typeof(x) _x = (x); \
	const typeof(y) _y = (y); \
	(void) (&_x == &_y); \
	_x < _y ? _x : _y ; \
})
#endif


static GstFlowReturn write_sector(BkrSPLPEnc *filter, GstCaps *caps)
{
	size_t size = min(gst_adapter_available(filter->adapter), (unsigned) filter->format->capacity);
	GstBuffer *srcbuf;
	const guint8 *data;
	GstFlowReturn result;

	data = gst_adapter_peek(filter->adapter, size);

	/* buffer is allocated to be large enough to hold the encoded
	 * sector, then its size is reduced to tell encode_sector() how
	 * much data is really there */
	result = gst_pad_alloc_buffer(filter->srcpad, GST_BUFFER_OFFSET_NONE, filter->format->data_size + filter->format->parity_size, caps, &srcbuf);
	if(result != GST_FLOW_OK) {
		GST_DEBUG("gst_pad_alloc_buffer() failed");
		return result;
	}
	GST_BUFFER_SIZE(srcbuf) = size;

	memcpy(GST_BUFFER_DATA(srcbuf), data, size);
	encode_sector(filter, srcbuf, filter->sector_number++);

	gst_adapter_flush(filter->adapter, size);

	result = gst_pad_push(filter->srcpad, srcbuf);
	if(result != GST_FLOW_OK) {
		GST_DEBUG("gst_pad_push() failed");
		return result;
	}

	return GST_FLOW_OK;
}


/*
 * ============================================================================
 *
 *                       GStreamer Encoder Support Code
 *
 * ============================================================================
 */


/*
 * Sink pad setcaps function.  See
 *
 * file:///usr/share/doc/gstreamer0.10-doc/gstreamer-0.10/GstPad.html#GstPadBufferAllocFunction
 */


static struct bkr_splp_format *caps_to_format(GstCaps *caps)
{
	enum bkr_videomode videomode;
	enum bkr_bitdensity bitdensity;
	enum bkr_sectorformat sectorformat;

	if(!bkr_parse_caps(caps, &videomode, &bitdensity, &sectorformat)) {
		GST_DEBUG("failure parsing caps");
		return NULL;
	}

	return compute_format(videomode, bitdensity, sectorformat);
}


static gboolean enc_setcaps(GstPad *pad, GstCaps *caps)
{
	BkrSPLPEnc *filter = BKR_SPLPENC(gst_pad_get_parent(pad));
	gboolean result;

	free(filter->format);
	filter->format = caps_to_format(caps);

	reed_solomon_codec_free(filter->rs_format);
	if(filter->format) {
		filter->rs_format = reed_solomon_codec_new((filter->format->data_size + filter->format->parity_size) / filter->format->interleave, filter->format->data_size / filter->format->interleave, filter->format->interleave);
		if(!filter->rs_format) {
			GST_DEBUG("reed_solomon_codec_new() failed");
			free(filter->format);
			filter->format = NULL;
		}
	} else
		filter->rs_format = NULL;

	result = filter->format ? TRUE : FALSE;

	gst_object_unref(filter);

	return result;
}



/*
 * Buffer alloc function.  See
 *
 * file:///usr/share/doc/gstreamer0.8-doc/gstreamer-0.8/GstPad.html#GstPadBufferAllocFunction
 */


static GstFlowReturn enc_bufferalloc(GstPad *pad, guint64 offset, guint size, GstCaps *caps, GstBuffer **buf)
{
	size_t buffer_size;
	GstFlowReturn result;

	/* incase something goes wrong */
	*buf = NULL;

	/* avoid computing the format if we already know what it is */
	if(caps == GST_PAD_CAPS(pad)) {
		BkrSPLPEnc *filter = BKR_SPLPENC(gst_pad_get_parent(pad));
		buffer_size = filter->format->data_size + filter->format->parity_size;
		gst_object_unref(filter);
	} else {
		struct bkr_splp_format *format = caps_to_format(caps);
		if(!format) {
			/* FIXME:  is this enough error handling? */
			result = GST_FLOW_ERROR;
			goto done;
		}
		buffer_size = format->data_size + format->parity_size;
		free(format);
	}

	*buf = gst_buffer_new_and_alloc(buffer_size);
	if(!*buf) {
		result = GST_FLOW_ERROR;
		goto done;
	}

	gst_buffer_set_caps(*buf, caps);
	result = GST_FLOW_OK;

done:
	return result;
}


/*
 * Event function.  See
 *
 * file:///usr/share/doc/gstreamer0.10-doc/gstreamer-0.10/GstPad.html#GstPadEventFunction
 */


static GstFlowReturn enc_flush(BkrSPLPEnc *filter, GstCaps *caps)
{
	/*
	 * write any remaining sectors (including a final, short, sector if
	 * needed)
	 */

	while(gst_adapter_available(filter->adapter)) {
		GstFlowReturn result = write_sector(filter, caps);
		if(result != GST_FLOW_OK) {
			GST_DEBUG("write_sector() failed");
			return result;
		}
	}

	return GST_FLOW_OK;
}


static gboolean enc_event(GstPad *pad, GstEvent *event)
{
	BkrSPLPEnc *filter = BKR_SPLPENC(gst_pad_get_parent(pad));
	GstCaps *caps = GST_PAD_CAPS(pad);
	gboolean result;

	switch(GST_EVENT_TYPE(event)) {
	case GST_EVENT_NEWSEGMENT:
		/*
		 * FIXME:  what to do if a recording is in progress?
		 */

		/*
		 * forward the new segment event
		 */

		result = gst_pad_push_event(filter->srcpad, event);
		if(!result)
			break;

		/*
		 * initialize the sector number so as to induce a BOR mark
		 * to be written
		 */

		filter->sector_number = -1;
		break;

	case GST_EVENT_EOS:
		/*
		 * flush the adapter.
		 */

		if(enc_flush(filter, caps) != GST_FLOW_OK) {
			GST_DEBUG("enc_flush() failed");
			gst_event_unref(event);
			result = FALSE;
			break;
		}

		/*
		 * write the end-of-record mark.  note that pushing an
		 * event on a pad cannot change its caps, so we don't have
		 * to check that filter's format still matches the pad's
		 * caps.
		 */

		if(write_eor(filter, caps) != GST_FLOW_OK) {
			GST_DEBUG("failure writing EOR mark");
			gst_event_unref(event);
			result = FALSE;
			break;
		}

		/*
		 * forward the end-of-stream event.
		 */

		result = gst_pad_push_event(filter->srcpad, event);
		break;

	default:
		result = gst_pad_event_default(pad, event);
		break;
	}

	gst_object_unref(filter);
	return result;
}


/*
 * Chain function.  See
 *
 * file:///usr/share/doc/gstreamer0.8-doc/gstreamer-0.8/GstPad.html#GstPadChainFunction
 */


static GstFlowReturn enc_chain(GstPad *pad, GstBuffer *sinkbuf)
{
	BkrSPLPEnc *filter = BKR_SPLPENC(gst_pad_get_parent(pad));
	GstCaps *caps = gst_buffer_get_caps(sinkbuf);
	GstFlowReturn result;

	if(!caps || (caps != GST_PAD_CAPS(pad))) {
		if(!caps)
			GST_DEBUG("caps not set on buffer");
		else if(caps != GST_PAD_CAPS(pad))
			GST_DEBUG("buffer's caps don't match pad's caps");
		result = GST_FLOW_NOT_NEGOTIATED;
		goto done;
	}

	gst_adapter_push(filter->adapter, sinkbuf);

	if(filter->sector_number < 0) {
		result = write_bor(filter, caps);
		if(result != GST_FLOW_OK) {
			GST_DEBUG("failure writing BOR mark");
			goto done;
		}
	}

	while(gst_adapter_available(filter->adapter) >= filter->format->capacity) {
		result = write_sector(filter, caps);
		if(result != GST_FLOW_OK) {
			GST_DEBUG("write_sector() failed");
			goto done;
		}
	}

	result = GST_FLOW_OK;

done:
	gst_caps_unref(caps);
	gst_object_unref(filter);
	return result;
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

	g_object_unref(filter->adapter);
	filter->adapter = NULL;
	gst_object_unref(filter->srcpad);
	filter->srcpad = NULL;
	reed_solomon_codec_free(filter->rs_format);
	filter->rs_format = NULL;
	free(filter->format);
	filter->format = NULL;

	G_OBJECT_CLASS(enc_parent_class)->finalize(object);
}


/*
 * Base init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GBaseInitFunc
 */


static void enc_base_init(gpointer class)
{
	static GstElementDetails plugin_details = {
		"Backer Sector Encoder",
		"Filter",
		"Backer sector and record encoder",
		"Kipp Cannon <kcannon@ligo.caltech.edu>"
	};
	GObjectClass *object_class = G_OBJECT_CLASS(class);
	GstElementClass *element_class = GST_ELEMENT_CLASS(class);
	GstPadTemplate *sinkpad_template = gst_pad_template_new(
		"sink",
		GST_PAD_SINK,
		GST_PAD_ALWAYS,
		bkr_get_template_caps()
	);
	GstPadTemplate *srcpad_template = gst_pad_template_new(
		"src",
		GST_PAD_SRC,
		GST_PAD_ALWAYS,
		bkr_get_template_caps()
	);

	gst_element_class_set_details(element_class, &plugin_details);

	object_class->finalize = enc_finalize;

	gst_element_class_add_pad_template(element_class, sinkpad_template);
	gst_element_class_add_pad_template(element_class, srcpad_template);
}


/*
 * Class init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GClassInitFunc
 */


static void enc_class_init(gpointer class, gpointer class_data)
{
	enc_parent_class = g_type_class_ref(GST_TYPE_ELEMENT);
}


/*
 * Instance init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GInstanceInitFunc
 */


static void enc_instance_init(GTypeInstance *object, gpointer class)
{
	GstElement *element = GST_ELEMENT(object);
	BkrSPLPEnc *filter = BKR_SPLPENC(object);
	GstPad *pad;

	gst_element_create_all_pads(element);

	/* configure sink pad */
	pad = gst_element_get_static_pad(element, "sink");
	gst_pad_set_setcaps_function(pad, enc_setcaps);
	gst_pad_set_bufferalloc_function(pad, enc_bufferalloc);
	gst_pad_set_event_function(pad, enc_event);
	gst_pad_set_chain_function(pad, enc_chain);
	gst_object_unref(pad);

	/* configure src pad */
	pad = gst_element_get_static_pad(element, "src");

	/* consider this to consume the reference */
	filter->srcpad = pad;

	/* internal state */
	filter->adapter = gst_adapter_new();
	filter->rs_format = NULL;
	filter->format = NULL;
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
			.class_init = enc_class_init,
			.base_init = enc_base_init,
			.instance_size = sizeof(BkrSPLPEnc),
			.instance_init = enc_instance_init,
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


enum property {
	ARG_DEC_BYTES_CORRECTED = 1,
	ARG_DEC_WORST_BLOCK,
	ARG_DEC_RECENT_BLOCK,
	ARG_DEC_BAD_SECTORS,
	ARG_DEC_LOST_RUNS,
	ARG_DEC_DUPLICATE_RUNS
};


static void dec_set_property(GObject *object, enum property id, const GValue *value, GParamSpec *pspec)
{
	BkrSPLPDec *filter = BKR_SPLPDEC(object);

	switch(id) {
	case ARG_DEC_BYTES_CORRECTED:
		filter->bytes_corrected = g_value_get_int(value);
		break;

	case ARG_DEC_WORST_BLOCK:
		filter->worst_block = g_value_get_int(value);
		break;

	case ARG_DEC_RECENT_BLOCK:
		filter->recent_block = g_value_get_int(value);
		break;

	case ARG_DEC_BAD_SECTORS:
		filter->bad_sectors = g_value_get_int(value);
		break;

	case ARG_DEC_LOST_RUNS:
		filter->lost_runs = g_value_get_int(value);
		break;

	case ARG_DEC_DUPLICATE_RUNS:
		filter->duplicate_runs = g_value_get_int(value);
		break;
	}
}


static void dec_get_property(GObject *object, enum property id, GValue *value, GParamSpec *pspec)
{
	BkrSPLPDec *filter = BKR_SPLPDEC(object);

	switch(id) {
	case ARG_DEC_BYTES_CORRECTED:
		g_value_set_int(value, filter->bytes_corrected);
		break;

	case ARG_DEC_WORST_BLOCK:
		g_value_set_int(value, filter->worst_block);
		break;

	case ARG_DEC_RECENT_BLOCK:
		g_value_set_int(value, filter->recent_block);
		break;

	case ARG_DEC_BAD_SECTORS:
		g_value_set_int(value, filter->bad_sectors);
		break;

	case ARG_DEC_LOST_RUNS:
		g_value_set_int(value, filter->lost_runs);
		break;

	case ARG_DEC_DUPLICATE_RUNS:
		g_value_set_int(value, filter->duplicate_runs);
		break;
	}
}


/*
 * Sink pad setcaps function.  See
 *
 * file:///usr/share/doc/gstreamer0.10-doc/gstreamer-0.10/GstPad.html#GstPadBufferAllocFunction
 */


static gboolean dec_setcaps(GstPad *pad, GstCaps *caps)
{
	BkrSPLPDec *filter = BKR_SPLPDEC(gst_pad_get_parent(pad));
	gboolean result;

	free(filter->format);
	filter->format = caps_to_format(caps);

	reed_solomon_codec_free(filter->rs_format);
	if(filter->format) {
		reset_statistics(filter);
		filter->rs_format = reed_solomon_codec_new((filter->format->data_size + filter->format->parity_size) / filter->format->interleave, filter->format->data_size / filter->format->interleave, filter->format->interleave);
		if(!filter->rs_format) {
			GST_DEBUG("reed_solomon_codec_new() failed");
			free(filter->format);
			filter->format = NULL;
		}
	} else
		filter->rs_format = NULL;

	result = filter->format ? TRUE : FALSE;

	gst_object_unref(filter);

	return result;
}


/*
 * Chain function.  See
 *
 * file:///usr/share/doc/gstreamer0.8-doc/gstreamer-0.8/GstPad.html#GstPadChainFunction
 */


static GstFlowReturn dec_chain(GstPad *pad, GstBuffer *sinkbuf)
{
	BkrSPLPDec *filter = BKR_SPLPDEC(gst_pad_get_parent(pad));
	GstCaps *caps = gst_buffer_get_caps(sinkbuf);
	GstPad *srcpad = filter->srcpad;
	struct sector_decode_status status;
	GstFlowReturn result;

	if(!caps || (caps != GST_PAD_CAPS(pad))) {
		if(!caps)
			GST_DEBUG("caps not set on buffer");
		else if(caps != GST_PAD_CAPS(pad))
			GST_DEBUG("buffer's caps don't match pad's caps");
		result = GST_FLOW_NOT_NEGOTIATED;
		goto done;
	}

	/*
	 * perform an in-place decode of the sector
	 */

	status = decode_sector(filter, sinkbuf);

	/*
	 * ignore beginning-of-record and duplicate sectors.  can't
	 * determine anything about a sector without a valid header, so
	 * ignore those too.  NOTE:  it's normal to get some completely
	 * hosed sectors at the start of a recording as the VCR seeks
	 * around to lock onto the signal.
	 */

	if(!status.header_is_valid || status.sector_is_bor || status.sector_is_duplicate) {
		gst_buffer_unref(sinkbuf);
		result = GST_FLOW_OK;
		goto done;
	}

	/*
	 * generate "skipped sector" events if needed.
	 */

	for(; status.sectors_skipped; status.sectors_skipped--) {
		result = gst_pad_push_event(srcpad, bkr_event_new_skipped_sector());
		if(result != GST_FLOW_OK) {
			gst_buffer_unref(sinkbuf);
			GST_DEBUG("gst_pad_push_event() failed");
			goto done;
		}
	}

	/*
	 * if sector is an end-of-record marker, generate an EOS event, and
	 * discard sector.  NOTE:  because all end-of-record sectors have
	 * the same sector number, we'll only see one of them:  the rest
	 * will be treated as duplicates and discarded.
	 */

	if(status.sector_is_eor) {
		gst_buffer_unref(sinkbuf);
		result = gst_pad_push_event(srcpad, gst_event_new_eos());
		goto done;
	}

	/*
	 * if sector did not decode properly, generate a "next sector
	 * invalid" event.
	 */

	if(!status.sector_is_valid) {
		result = gst_pad_push_event(srcpad, bkr_event_new_next_sector_invalid());
		if(result != GST_FLOW_OK) {
			gst_buffer_unref(sinkbuf);
			GST_DEBUG("gst_pad_push_event() failed");
			goto done;
		}
	}

	/*
	 * transmit data
	 */

	result = gst_pad_push(srcpad, sinkbuf);
	if(result != GST_FLOW_OK) {
		GST_DEBUG("gst_pad_push() failed");
		goto done;
	}

done:
	gst_caps_unref(caps);
	gst_object_unref(filter);
	return result;
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

	gst_object_unref(filter->srcpad);
	filter->srcpad = NULL;
	reed_solomon_codec_free(filter->rs_format);
	filter->rs_format = NULL;
	free(filter->format);
	filter->format = NULL;

	G_OBJECT_CLASS(dec_parent_class)->finalize(object);
}


/*
 * Base init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GBaseInitFunc
 */


static void dec_base_init(gpointer class)
{
	static GstElementDetails plugin_details = {
		"Backer Sector Decoder",
		"Filter",
		"Backer sector and record decoder",
		"Kipp Cannon <kcannon@ligo.caltech.edu>"
	};
	GObjectClass *object_class = G_OBJECT_CLASS(class);
	GstElementClass *element_class = GST_ELEMENT_CLASS(class);
	GstPadTemplate *sinkpad_template = gst_pad_template_new(
		"sink",
		GST_PAD_SINK,
		GST_PAD_ALWAYS,
		bkr_get_template_caps()
	);
	GstPadTemplate *srcpad_template = gst_pad_template_new(
		"src",
		GST_PAD_SRC,
		GST_PAD_ALWAYS,
		bkr_get_template_caps()
	);

	gst_element_class_set_details(element_class, &plugin_details);

	object_class->set_property = dec_set_property;
	object_class->get_property = dec_get_property;
	object_class->finalize = dec_finalize;

	gst_element_class_add_pad_template(element_class, sinkpad_template);
	gst_element_class_add_pad_template(element_class, srcpad_template);
}


/*
 * Class init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GClassInitFunc
 */


static void dec_class_init(gpointer class, gpointer class_data)
{
	GObjectClass *object_class = G_OBJECT_CLASS(class);

	g_object_class_install_property(object_class, ARG_DEC_BYTES_CORRECTED, g_param_spec_int("bytes_corrected", "Bytes corrected", "Bytes corrected", 0, INT_MAX, 0, G_PARAM_READWRITE));
	g_object_class_install_property(object_class, ARG_DEC_WORST_BLOCK, g_param_spec_int("worst_block", "Worst block", "Worst block", 0, INT_MAX, 0, G_PARAM_READWRITE));
	g_object_class_install_property(object_class, ARG_DEC_RECENT_BLOCK, g_param_spec_int("recent_block", "Recent block", "Recent block", 0, INT_MAX, 0, G_PARAM_READWRITE));
	g_object_class_install_property(object_class, ARG_DEC_BAD_SECTORS, g_param_spec_int("bad_sectors", "Bad sectors", "Bad Sectors", 0, INT_MAX, 0, G_PARAM_READWRITE));
	g_object_class_install_property(object_class, ARG_DEC_LOST_RUNS, g_param_spec_int("lost_runs", "Lost runs", "Lost runs", 0, INT_MAX, 0, G_PARAM_READWRITE));
	g_object_class_install_property(object_class, ARG_DEC_DUPLICATE_RUNS, g_param_spec_int("duplicate_runs", "Duplicate runs", "Duplicate runs", 0, INT_MAX, 0, G_PARAM_READWRITE));

	dec_parent_class = g_type_class_ref(GST_TYPE_ELEMENT);
}


/*
 * Instance init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GInstanceInitFunc
 */


static void dec_instance_init(GTypeInstance *object, gpointer class)
{
	GstElement *element = GST_ELEMENT(object);
	BkrSPLPDec *filter = BKR_SPLPDEC(object);
	GstPad *pad;

	gst_element_create_all_pads(element);

	/* configure sink pad */
	pad = gst_element_get_static_pad(element, "sink");
	gst_pad_set_setcaps_function(pad, dec_setcaps);
	gst_pad_set_chain_function(pad, dec_chain);
	gst_object_unref(pad);

	/* configure src pad */
	pad = gst_element_get_static_pad(element, "src");

	/* consider this to consume the reference */
	filter->srcpad = pad;

	/* internal state */
	filter->rs_format = NULL;
	filter->format = NULL;
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
			.class_init = dec_class_init,
			.base_init = dec_base_init,
			.instance_size = sizeof(BkrSPLPDec),
			.instance_init = dec_instance_init,
		};
		type = g_type_register_static(GST_TYPE_ELEMENT, "BkrSPLPDec", &info, 0);
	}
	return type;
}
