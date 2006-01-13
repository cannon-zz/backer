/* 
 */

#ifndef __BKR_FRAME_H__
#define __BKR_FRAME_H__

#include <gst/gst.h>
#include <backer.h>

G_BEGIN_DECLS

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
};


#define BKR_FRAME_TYPE			(bkr_frame_get_type())
#define BKR_FRAME(obj)			(G_TYPE_CHECK_INSTANCE_CAST((obj), BKR_FRAME_TYPE, BkrFrame))
#define BKR_FRAME_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST((klass), BKR_FRAME_TYPE, BkrFrame))
#define GST_IS_BKR_FRAME(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), BKR_FRAME_TYPE))
#define GST_IS_BKR_FRAME_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), BKR_FRAME_TYPE))

typedef struct _BkrFrameClass BkrFrameClass;
typedef struct _BkrFrame BkrFrame;

struct _BkrFrameClass {
	GstElementClass parent_class;
};

struct _BkrFrame {
	GstElement element;

	GstPad *sinkpad, *srcpad;

	enum bkr_vidmode vidmode;
	enum bkr_density density;
	enum bkr_format fmt;
	struct bkr_frame_format format;
};

GType bkr_frame_get_type(void);

G_END_DECLS
#endif				/* __BKR_FRAME_H__ */
