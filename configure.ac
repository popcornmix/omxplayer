AC_INIT([danu_omxplayer], [0.0.1], [support@rnd.danusys.com])

# [en] Rid of default compiler options: -O2 -g 
# [ko] 기본 컴파일러 옵션 (-O2 -g) 제거
: ${CFLAGS=""}
: ${CXXFLAGS=""}

AM_INIT_AUTOMAKE([1.0 subdir-objects])

AC_LANG([C++])
AC_PROG_CXX

AC_ARG_ENABLE(debug,
AS_HELP_STRING([--enable-debug],
               [enable debugging, default: no]),
[case "${enableval}" in
             yes) debug=true ;;
             no)  debug=false ;;
             *)   AC_MSG_ERROR([bad value ${enableval} for --enable-debug]) ;;
esac],
[debug=false])
AM_CONDITIONAL(DEBUG, test x"$debug" = x"true")
AC_ARG_ENABLE(profile,
AS_HELP_STRING([--enable-profile],
               [enable profile (and also debug), default: no]),
[case "${enableval}" in
             yes) profile=true ;;
             no)  profile=false ;;
             *)   AC_MSG_ERROR([bad value ${enableval} for --enable-profile]) ;;
esac],
[profile=false])
AM_CONDITIONAL(PROFILING, test x"$profile" = x"true")
AM_COND_IF(PROFILING, [AM_CONDITIONAL(DEBUG, true)])

AC_ARG_VAR(OPT, [The path to '/opt' of Raspberry Pi firmware. Default: /opt])
if test -z "$OPT"; then
	OPT=/opt
fi


AC_CONFIG_FILES([Makefile doc/Makefile src/Makefile])

## Tests: commands
AC_CHECK_PROG([CMD_BASE64], [base64], [yes])
if test x"${CMD_BASE64}" != x"yes" ; then
	AC_MSG_ERROR(['base64' command not found.])
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
