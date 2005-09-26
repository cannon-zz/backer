# Preamble
AC_INIT([backer],[3.103],[kcannon@users.sf.net],[backer])
AC_COPYRIGHT([Copyright (C) 2002 Kipp C. Cannon])
AC_CONFIG_SRCDIR([drivers/backer.h])
AC_CONFIG_AUX_DIR([gnuscripts])
AM_INIT_AUTOMAKE([foreign])
AC_CONFIG_FILES([ \
	Makefile \
	backer.spec \
	codecs/Makefile \
	docs/bkrcheck.8 \
	docs/bkrencode.8 \
	docs/bkrmonitor.8 \
	docs/Makefile \
	utilities/Makefile \
])

# Documentation boilerplate
AC_SUBST([pkgdocdir], [${docdir}/${PACKAGE_NAME}-${PACKAGE_VERSION}])
LATEXRULES=$srcdir/docs/LaTeX.Rules
AC_SUBST_FILE([LATEXRULES])

# Check for programs
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LIBTOOL
PKG_PROG_PKG_CONFIG()

# Check for kernel source
AX_KERNEL_VERSION
AX_CHECK_KERNEL_SOURCE

# Check for GTK+ 2
AC_SUBST([HAVE_GTK], [no])
AM_PATH_GTK_2_0([2.0.0],[HAVE_GTK=yes])
AM_CONDITIONAL([COND_GTK], [test "${HAVE_GTK}" == "yes"])

# Check for GStreamer 0.8
AC_SUBST([GST_VERSION], [0.8])
PKG_CHECK_MODULES([GST], [gstreamer-${GST_VERSION}], [HAVE_GST=yes], [HAVE_GST=no])
AC_SUBST([GST_CFLAGS])
AC_SUBST([GST_LIBS])
AC_SUBST([plugindir], [${libdir}/gstreamer-${GST_VERSION}])
AC_SUBST([GST_PLUGIN_LDFLAGS], ["-module -avoid-version -export-symbols-regex [_]*\(gst_\|Gst\|GST_\).*"])


# Set driver configuration
AC_SUBST([BKR_MAJOR],[60])
AC_ARG_WITH([backer-major],AC_HELP_STRING([--with-backer-major=number],[Backer device major number (default=60)]),[BKR_MAJOR=${withval}])

AC_SUBST([DEFAULT_DEV],[/dev/tape])
AC_ARG_WITH([default-dev],AC_HELP_STRING([--with-default-dev=device],[Default device for utilities (default=/dev/tape)]),[DEFAULT_DEV=${withval}])
AC_DEFINE([DEFAULT_DEV], [DEFAULT_DEV], [Name of default device file])

# Output configure information
AC_PROG_MAKE_SET
AC_OUTPUT
