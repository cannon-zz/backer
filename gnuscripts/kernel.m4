dnl
dnl Find an installed linux kernel
dnl

AC_DEFUN([AX_LINUX_KERNEL_VERSION],
	[AC_ARG_WITH([linux-version],
		AC_HELP_STRING(
			[--with-linux-version=ver],
			[Linux kernel version (default=`uname -r`)]
		),
		[LINUX_KERNEL_VERSION="$withval"
		AC_MSG_RESULT([using ${LINUX_KERNEL_VERSION} for kernel version])],
		[AC_CHECKING([for linux kernel version])
		if test ! -z `uname -r` ; then
			LINUX_KERNEL_VERSION=`uname -r`
			AC_MSG_RESULT([$LINUX_KERNEL_VERSION])
		else
			LINUX_KERNEL_VERSION=
			AC_MSG_ERROR([not found])
		fi
		]
	)

	AC_ARG_WITH([modulesdir],
		AC_HELP_STRING(
			[--with-modulesdir=dir],
			[location of kernel modules (default=/lib/modules/<kernel version>)]
		),
		[MODULES_DIR="${withval}"
		AC_MSG_RESULT([using ${MODULES_DIR} for kernel modules location])
		],
		[AC_CHECKING([for linux kernel ${LINUX_KERNEL_VERSION} modules location])
		if test -d "/lib/modules/${LINUX_KERNEL_VERSION}" ; then
			MODULES_DIR="/lib/modules/${LINUX_KERNEL_VERSION}"
			AC_MSG_RESULT([$MODULES_DIR])
		else
			MODULES_DIR=
			AC_MSG_ERROR([not found])
		fi
		]
	)
	AC_SUBST([LINUX_KERNEL_VERSION])
	AC_SUBST([MODULES_DIR])]
)


dnl
dnl Find the corresponding kernel source tree
dnl

AC_DEFUN([AX_CHECK_LINUX_KERNEL_SOURCE],
	[AC_REQUIRE([AX_LINUX_KERNEL_VERSION])
	LINUX_KERNEL_SOURCE="${MODULES_DIR}/build"
	AC_ARG_WITH(
		[kernel-source],
		AC_HELP_STRING([--with-kernel-source=dir], [path to Linux kernel source (default=<modulesdir>/build)]),
		[LINUX_KERNEL_SOURCE=${withval}]
	)
	AC_MSG_CHECKING([for linux/version.h in ${LINUX_KERNEL_SOURCE}/include])
	if test -f "${LINUX_KERNEL_SOURCE}/include/linux/version.h" ; then
		AC_MSG_RESULT([yes])
	else
		AC_MSG_RESULT([no])
		AC_MSG_ERROR([Maybe your kernel headers are in a different directory or your Linux source tree hasn't been configured?])
	fi
	AC_SUBST([LINUX_KERNEL_SOURCE])]
)
