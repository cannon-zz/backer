plugin_LTLIBRARIES = libtapefile.la

AM_CPPFLAGS = -I$(top_srcdir)/drivers

libtapefile_la_SOURCES = bkr_elements.h bkr_elements.c bkr_frame.h bkr_frame.c bkr_rll.h bkr_rll.c bkr_splp.h bkr_splp.c bkr_splp_randomize.h bkr_splp_randomize.c bkr_ecc2.h bkr_ecc2.c bkr_video_out.h bkr_video_out.c bkr_bytes.h rs.h rs.c
libtapefile_la_CFLAGS = $(AM_CFLAGS) $(gstreamer_CFLAGS)
libtapefile_la_LDFLAGS = $(gstreamer_LIBS) $(GST_PLUGIN_LDFLAGS)
