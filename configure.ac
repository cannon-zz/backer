# Preamble
AC_INIT([backer],[3.103],[kcannon@users.sf.net],[backer])
AC_COPYRIGHT([Copyright (C) 2002 Kipp C. Cannon])
AC_CONFIG_SRCDIR([drivers/backer.h])
AC_CONFIG_AUX_DIR([gnuscripts])
AM_INIT_AUTOMAKE([foreign])
AC_CONFIG_FILES([ \
	Makefile \
	backer.spec \
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
AC_PATH_PROG([GREP],[grep])
AC_PATH_PROG([GZIP],[gzip])
AC_PATH_PROG([READLINK],[readlink])

# Check for kernel source
AX_KERNEL_VERSION
AX_CHECK_KERNEL_SOURCE

# Check for GTK 2
AC_SUBST([HAVE_GTK=no])
AM_PATH_GTK_2_0([2.0.0],[HAVE_GTK=yes])
AM_CONDITIONAL([COND_GTK], [test "${HAVE_GTK}" == "yes"])

# Set driver configuration
AC_SUBST([BKR_MAJOR],[60])
AC_ARG_WITH([backer-major],AC_HELP_STRING([--with-backer-major=number],[Backer device major number (default=60)]),[BKR_MAJOR=${withval}])

AC_SUBST([DEFAULT_DEV],[/dev/tape])
AC_ARG_WITH([default-dev],AC_HELP_STRING([--with-default-dev=device],[Default device for utilities (default=/dev/tape)]),[DEFAULT_DEV=${withval}])
AC_DEFINE([DEFAULT_DEV], [DEFAULT_DEV], [Name of default device file])

# Output configure information
AC_PROG_MAKE_SET
AC_OUTPUT
