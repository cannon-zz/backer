dnl
dnl Find an installed kernel
dnl

AC_DEFUN([AX_KERNEL_VERSION],
[
AC_ARG_WITH([kernel-version], AC_HELP_STRING([--with-kernel-version=ver], [Linux kernel version (default=`uname -r`)]),
[
AC_SUBST([KERNEL_VERSION], [$withval])
AC_MSG_RESULT([using ${KERNEL_VERSION} for kernel version])
],
[
AC_CACHE_CHECK([for kernel version], [KERNEL_VERSION],
[
if test ! -z `uname -r` ; then
	AC_SUBST([KERNEL_VERSION], `uname -r`)
else
	AC_SUBST([KERNEL_VERSION], [])
	AC_MSG_RESULT([unknown])
fi
])
])

AC_ARG_WITH([modulesdir], AC_HELP_STRING([--with-modulesdir=dir], [location of kernel modules (default=/lib/modules/<kernel version>)]),
[
AC_SUBST([MODULES_DIR], [${withval}])
AC_MSG_RESULT([using ${MODULES_DIR} for kernel modules location])
],
[
AC_CACHE_CHECK([for kernel modules location], [MODULES_DIR],
[
if test -d "/lib/modules/${KERNEL_VERSION}" ; then
	AC_SUBST([MODULES_DIR], [/lib/modules/${KERNEL_VERSION}])
else
	AC_SUBST([MODULES_DIR], [])
	AC_MSG_RESULT([not found])
fi
])
])
])


dnl
dnl Find the corresponding kernel source tree
dnl

AC_DEFUN([AX_CHECK_KERNEL_SOURCE],
[
AC_REQUIRE([AX_KERNEL_VERSION])
AC_SUBST([KERNEL_SOURCE], [${MODULES_DIR}/build])
AC_ARG_WITH([kernel-source], AC_HELP_STRING([--with-kernel-source=dir], [path to Linux kernel source (default=<modulesdir>/build)]), [KERNEL_SOURCE=${withval}])

AC_MSG_CHECKING([for kernel ${KERNEL_VERSION} source])
if ${GREP} ${KERNEL_VERSION} ${KERNEL_SOURCE}/include/linux/version.h >/dev/null 2>/dev/null ; then
	AC_MSG_RESULT([${KERNEL_SOURCE}])
else
	AC_MSG_RESULT([no])
	AC_MSG_ERROR([Cannot find version string \"${KERNEL_VERSION}\" in linux/version.h residing in ${KERNEL_SOURCE}/include ... maybe your kernel headers are in a different directory or your Linux source tree hasn't been configured?])
fi
])
