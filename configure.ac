AC_INIT([stereo-cube], 0.1)

AC_CONFIG_AUX_DIR([build])
AC_CONFIG_MACRO_DIR([build/autotools])
AC_CONFIG_SRCDIR([src/stereo-cube.c])
AM_CONFIG_HEADER([config.h])
AC_GNU_SOURCE

AM_INIT_AUTOMAKE([1.9 foreign no-dist-gzip dist-xz tar-ustar])

LT_LIB_M
AC_SUBST(LIBM)

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

dnl     ============================================================
dnl     Enable strict compiler flags
dnl     ============================================================

# use strict compiler flags only when building from git; the rules for
# distcheck will take care of turning this on when making a release
m4_define([maintainer_default], [yes])
AC_ARG_ENABLE(
  [maintainer-flags],
  [AC_HELP_STRING([--enable-maintainer-flags=@<:@no/yes/error@:>@],
                  [Use strict compiler flags @<:@default=]maintainer_default[@:>@])],
  [],
  enable_maintainer_flags=maintainer_default
)

MAINTAINER_COMPILER_FLAGS="-Wall -Wcast-align -Wuninitialized
                           -Wno-strict-aliasing -Wempty-body -Wformat
                           -Wformat-security -Winit-self -Wundef
                           -Wpointer-arith -Wmissing-declarations"

AS_CASE(
  [$enable_maintainer_flags],
  [yes],
  [
    AS_COMPILER_FLAGS([MAINTAINER_CFLAGS], [$MAINTAINER_COMPILER_FLAGS])
  ],
  [no],
  [
  ],
  [error],
  [
    MAINTAINER_COMPILER_FLAGS="$MAINTAINER_COMPILER_FLAGS -Werror"
    AS_COMPILER_FLAGS([MAINTAINER_CFLAGS], [$MAINTAINER_COMPILER_FLAGS])
  ],
  [*],
  [AC_MSG_ERROR([Invalid option for --enable-maintainer-flags])]
)

# strip leading spaces
STEREO_CUBE_EXTRA_CFLAGS="$STEREO_CUBE_EXTRA_CFLAGS ${MAINTAINER_CFLAGS#*  }"

AC_PROG_CC

PKG_CHECK_MODULES(GDK_PIXBUF, [gdk-pixbuf-2.0])
PKG_CHECK_MODULES(GBM, [gbm])
PKG_CHECK_MODULES(DRM, [libdrm])
PKG_CHECK_MODULES(GL, [egl glesv2])
PKG_CHECK_MODULES(WAYLAND, [wayland-client wayland-egl])

AC_SUBST(STEREO_CUBE_EXTRA_CFLAGS)

AC_CONFIG_FILES([
        Makefile
        src/Makefile
])

AC_OUTPUT
