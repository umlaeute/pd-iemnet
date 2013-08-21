AC_INIT([iemnet],[0.0.1],[zmoelnig@iem.at],
                [iemnet],[http://ambisonics.iem.at/xchange/format])
AM_INIT_AUTOMAKE

AM_PROG_LIBTOOL

AC_CONFIG_SRCDIR([../../iemnet.h])

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_FILES([Makefile tests/Makefile])

AC_PROG_INSTALL

AC_LANG_C
AC_PROG_CC
AC_PROG_MAKE_SET
AC_HEADER_STDC

/* check for math */
AC_CHECK_LIB([m],[sqrt])
AX_PTHREAD

AC_OUTPUT
