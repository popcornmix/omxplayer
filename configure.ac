AC_INIT([danu_omxplayer], [0.0.1], [support@rnd.danusys.com])
AM_INIT_AUTOMAKE([1.0 subdir-objects])

AC_CANONICAL_HOST

# Rid of default compiler options: -O2 -g
: ${CFLAGS=""}
: ${CXXFLAGS=""}

AC_LANG([C++])
AC_PROG_CXX

AC_ARG_ENABLE(debug,
AS_HELP_STRING([--enable-debug],
               [Enable debugging. Default: no]),
[case "${enableval}" in
             yes) debug=true ;;
             no)  debug=false ;;
             *)   AC_MSG_ERROR([bad value ${enableval} for --enable-debug]) ;;
esac],
[debug=false])
AM_CONDITIONAL(DEBUG, test x"$debug" = x"true")
AC_ARG_ENABLE(profile,
AS_HELP_STRING([--enable-profile],
               [Enable profile (and also debug). Default: no]),
[case "${enableval}" in
             yes) profile=true ;;
             no)  profile=false ;;
             *)   AC_MSG_ERROR([bad value ${enableval} for --enable-profile]) ;;
esac],
[profile=false])
AM_CONDITIONAL(PROFILING, test x"$profile" = x"true")
AM_COND_IF(PROFILING, [AM_CONDITIONAL(DEBUG, true)])

AC_ARG_VAR(OPT, [The path to '/opt' of Raspberry Pi firmware. This option is only for cross-compiling. The program will look into '/opt' runtime. Default: /opt])
if test -z "$OPT"; then
	OPT=/opt
fi

AC_CONFIG_FILES([Makefile doc/Makefile src/Makefile])

## Tests: commands
AC_CHECK_PROG([CMD_BASE64], [base64], [yes])
AC_CHECK_PROG([CMD_SED], [sed], [yes])
AC_CHECK_PROG([CMD_AWK], [awk], [yes])
if test x"${CMD_BASE64}" != x"yes" ; then
	AC_MSG_ERROR(['base64' command not found.])
fi
if test x"${CMD_SED}" != x"yes" ; then
	AC_MSG_ERROR(['sed' command not found.])
fi
if test x"${CMD_AWK}" != x"yes" ; then
	AC_MSG_ERROR(['awk' command not found.])
fi

## Tests: packages
# FFmpeg: n2.6
PKG_CHECK_MODULES([LIBFFMPEG],
	[libavcodec >= 56.26.100
		libavdevice >= 56.4.100
		libavfilter >= 5.11.102
		libavformat >= 56.25.101
		libavutil >= 54.20.100
		libswresample >= 1.1.100
		libswscale >= 3.1.101],
	[],
	[AC_MSG_ERROR(['FFmpeg-libs 2.6 or higher not found by pkg-config.
	Or you forgot to \"export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig\" ?'])]
	)
PKG_CHECK_MODULES([LIBPCRE], [libpcre >= 8.0])
PKG_CHECK_MODULES([DBUS], [dbus-1 >= 1.0.0])
PKG_CHECK_MODULES([FREETYPE2], [freetype2 >= 17.0.0])

AC_OUTPUT

AC_MSG_NOTICE([
* * *
This configure script does not check for RPI headers and libs.
Make sure '/opt' is installed or the OPT variable is properly set. Otherwise, the compilation will fail.
* * *])
