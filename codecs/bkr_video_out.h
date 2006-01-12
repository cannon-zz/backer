/* 
 */

#ifndef __BKR_VIDEO_OUT_H__
#define __BKR_VIDEO_OUT_H__

#include <gst/gst.h>
#include <backer.h>

G_BEGIN_DECLS

#define BKR_VIDEO_OUT_TYPE			(bkr_video_out_get_type())
#define BKR_VIDEO_OUT(obj)			(G_TYPE_CHECK_INSTANCE_CAST((obj), BKR_VIDEO_OUT_TYPE, BkrVideoOut))
#define BKR_VIDEO_OUT_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST((klass), BKR_VIDEO_OUT_TYPE, BkrVideoOut))
#define GST_IS_BKR_VIDEO_OUT(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), BKR_VIDEO_OUT_TYPE))
#define GST_IS_BKR_VIDEO_OUT_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), BKR_VIDEO_OUT_TYPE))

typedef struct _BkrVideoOutClass BkrVideoOutClass;
typedef struct _BkrVideoOut BkrVideoOut;

struct _BkrVideoOutClass {
	GstElementClass parent_class;
};

struct _BkrVideoOut {
	GstElement element;

	GstPad *sinkpad, *srcpad;

	enum bkr_vidmodes vidmode;
	enum bkr_densities density;
	gint width, height;
	guint32 *(*pixel_func)(guint32 *, guint32);
};

GType bkr_video_out_get_type(void);

G_END_DECLS
#endif				/* __BKR_VIDEO_OUT_H__ */
