plugin_LTLIBRARIES = libtapefile.la

INCLUDES = -I$(top_srcdir)/drivers

libtapefile_la_SOURCES = bkr_elements.h bkr_elements.c bkr_frame.h bkr_frame.c bkr_rll.h bkr_rll.c bkr_splp.h bkr_splp.c bkr_splp_randomize.h bkr_splp_randomize.c bkr_video_out.h bkr_video_out.c rs.h rs.c
libtapefile_la_CFLAGS = $(AM_CFLAGS) $(GST_CFLAGS) $(GST_BASE_CFLAGS)
libtapefile_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS)
libtapefile_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = bkr_elements.h bkr_frame.h bkr_rll.h bkr_splp.h bkr_splp_randomize.h bkr_video_out.h rs.h
