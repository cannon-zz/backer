/* 
 */


#ifndef __BKR_VIDEO_OUT_H__
#define __BKR_VIDEO_OUT_H__


#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <backer.h>


G_BEGIN_DECLS


#define BKR_VIDEO_OUT_TYPE			(bkr_video_out_get_type())
#define BKR_VIDEO_OUT(obj)			(G_TYPE_CHECK_INSTANCE_CAST((obj), BKR_VIDEO_OUT_TYPE, BkrVideoOut))
#define BKR_VIDEO_OUT_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST((klass), BKR_VIDEO_OUT_TYPE, BkrVideoOutClass))
#define GST_IS_BKR_VIDEO_OUT(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), BKR_VIDEO_OUT_TYPE))
#define GST_IS_BKR_VIDEO_OUT_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), BKR_VIDEO_OUT_TYPE))


typedef struct {
	GstElementClass parent_class;
} BkrVideoOutClass;


typedef struct {
	GstElement element;

	GstPad *srcpad;
	GstAdapter *adapter;

	enum bkr_videomode videomode;
	enum bkr_bitdensity bitdensity;

	/*
	 * next field number (first field is number 1, an odd field)
	 */

	gint field_number;

	/*
	 * Format information.
	 * 	width = count of pixels across each line
	 * 	height = count of lines in even field
	 * 	interlace = count of lines to add for odd field
	 */

	struct bkr_video_out_format {
		gint bytes_per_line;
		gint interlace;
		gint width;
		gint height;
		guint32 *(*pixel_func)(guint32 *, guint32);
	} format;
} BkrVideoOut;


GType bkr_video_out_get_type(void);


G_END_DECLS


#endif	/* __BKR_VIDEO_OUT_H__ */
