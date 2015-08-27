DEBUG_FLAGS =
OPTI_FLAGS =
if PROFILING
DEBUG_FLAGS += -p
endif
if DEBUG
DEBUG_FLAGS += -g3
OPTI_FLAGS += -O0
else
DEBUG_FLAGS += -g
OPTI_FLAGS += -O3
endif

AM_CXXFLAGS = $(WARNINGCFLAGS) $(DEBUG_FLAGS) $(OPTI_FLAGS) -std=c++0x

bin_PROGRAMS = __danu_omxplayer
__danu_omxplayer_CXXFLAGS = \
	-D__STDC_CONSTANT_MACROS\
	-D__STDC_LIMIT_MACROS\
	-DTARGET_POSIX\
	-DTARGET_LINUX\
	-fPIC\
	-DPIC\
	-D_REENTRANT\
	-D_LARGEFILE64_SOURCE\
	-D_FILE_OFFSET_BITS=64\
	-DHAVE_CMAKE_CONFIG\
	-D__VIDEOCORE4__\
	-U_FORTIFY_SOURCE\
	-DHAVE_OMXLIB\
	-DUSE_EXTERNAL_FFMPEG\
	-DHAVE_LIBAVCODEC_AVCODEC_H\
	-DHAVE_LIBAVUTIL_OPT_H\
	-DHAVE_LIBAVUTIL_MEM_H\
	-DHAVE_LIBAVUTIL_AVUTIL_H\
	-DHAVE_LIBAVFORMAT_AVFORMAT_H\
	-DHAVE_LIBAVFILTER_AVFILTER_H\
	-DHAVE_LIBSWRESAMPLE_SWRESAMPLE_H\
	-DOMX\
	-DOMX_SKIP64BIT\
	-ftree-vectorize\
	-DUSE_EXTERNAL_OMX\
	-DTARGET_RASPBERRY_PI\
	-DUSE_EXTERNAL_LIBBCM_HOST\
	$(LIBFFMPEG_CFLAGS)\
	$(LIBPCRE_CFLAGS)\
	$(DBUS_CFLAGS)\
	$(FREETYPE2_CFLAGS)\
	-I$(OPT)/vc/include\
	-I$(OPT)/vc/include/interface/vcos/pthreads\
	-I$(OPT)/vc/include/interface/vmcs_host/linux

__danu_omxplayer_SOURCES =\
	linux/XMemUtils.cpp \
	utils/log.cpp \
	DynamicDll.cpp \
	utils/PCMRemap.cpp \
	utils/RegExp.cpp \
	OMXSubtitleTagSami.cpp \
	OMXOverlayCodecText.cpp \
	BitstreamConverter.cpp \
	linux/RBP.cpp \
	OMXThread.cpp \
	OMXReader.cpp \
	OMXStreamInfo.cpp \
	OMXAudioCodecOMX.cpp \
	OMXCore.cpp \
	OMXVideo.cpp \
	OMXAudio.cpp \
	OMXClock.cpp \
	File.cpp \
	OMXPlayerVideo.cpp \
	OMXPlayerAudio.cpp \
	OMXPlayerSubtitles.cpp \
	SubtitleRenderer.cpp \
	Unicode.cpp \
	Srt.cpp \
	KeyConfig.cpp \
	OMXControl.cpp \
	Keyboard.cpp \
	omxplayer.cpp \
	fonts/BASE64_FreeSans.h\
	fonts/BASE64_FreeSansOblique.h

__danu_omxplayer_LDADD =\
	-lWFC\
	-lGLESv2\
	-lEGL\
	-lbcm_host\
	-lopenmaxil\
	-lz\
	$(LIBFFMPEG_LIBS)\
	$(LIBPCRE_LIBS)\
	$(DBUS_LIBS)\
	$(FREETYPE2_LIBS)\
	-L$(OPT)/vc/lib

#fonts/BASE64_FreeSans.h: fonts/FreeSans.h
#	__gen_base64_fonts.sh FREESANS fonts/FreeSans.h

#fonts/BASE64_FreeSansOblique.h: fonts/FreeSansOblique.h
#	__gen_base64_fonts.sh FREESANSOBLIQUE fonts/FreeSansOblique.h
