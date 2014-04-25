bin_PROGRAMS = omxplayer.bin

AM_CXXFLAGS = -std=c++0x -I./ -Ilinux $(DVDNAV_CFLAGS) $(FREETYPE2_CFLAGS) $(PCRE_CFLAGS) $(AVCODEC_CFLAGS) $(AVUTIL_CFLAGS) $(AVFORMAT_CFLAGS) $(SWSCALE_CFLAGS) $(SWRESAMPLE_CFLAGS) $(VC_CFLAGS)   $(DBUS_CFLAGS) -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -DTARGET_POSIX -DTARGET_LINUX -fPIC -DPIC -D_REENTRANT -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -DHAVE_CMAKE_CONFIG -D__VIDEOCORE4__ -U_FORTIFY_SOURCE -Wall -DHAVE_OMXLIB -DUSE_EXTERNAL_FFMPEG  -DHAVE_LIBAVCODEC_AVCODEC_H -DHAVE_LIBAVUTIL_OPT_H -DHAVE_LIBAVUTIL_MEM_H -DHAVE_LIBAVUTIL_AVUTIL_H -DHAVE_LIBAVFORMAT_AVFORMAT_H -DHAVE_LIBAVFILTER_AVFILTER_H -DHAVE_LIBSWRESAMPLE_SWRESAMPLE_H -DOMX -DOMX_SKIP64BIT -ftree-vectorize -DUSE_EXTERNAL_OMX -DTARGET_RASPBERRY_PI -DUSE_EXTERNAL_LIBBCM_HOST

omxplayer_bin_SOURCES = linux/XMemUtils.cpp \
		version.h \
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
                omxplayer.cpp 

omxplayer.cpp : version.h 

version.h: configure.ac
	bash gen_version.sh > version.h

omxplayer_bin_LDADD = $(INTI_LIBS) $(DVDNAV_LIBS) $(AVCODEC_LIBS) $(DBUS_LIBS) $(VC_LIBS) $(AVCODEC_LIBS) $(FREETYPE2_LIBS) \
	$(AVUTIL_LIBS) $(AVFORMAT_LIBS) $(SWSCALE_LIBS) $(SWRESAMPLE_LIBS) $(PCRE_LIBS) -lrt -lpthread 

clean-local:
	rm -f README
	rm -f configure
	rm -f Makefile.in
	rm -f version.h
	rm -f depcomp
	rm -f config.log
	rm -f config.status
	rm -rf autom4te.cache
	rm -f install-sh
	rm -f Makefile
	rm -f missing
	rm -f omxplayer
	rm -f aclocal.m4
	rm -f INSTALL
	rm -rf .deps

install-data-local:
	chmod a+x omxplayer
	cp omxplayer $(DESTDIR)/$(prefix)/bin/omxplayer
	mkdir -p $(DESTDIR)/$(prefix)/lib/omxplayer
	cp ffmpeg_compiled/lib/*.so* $(DESTDIR)/$(prefix)/lib/omxplayer
	mkdir -p $(DESTDIR)/$(prefix)/share/doc/omxplayer/
	cp README.md $(DESTDIR)/$(prefix)/share/doc/omxplayer/
	

README: README.md
	cat $< > $@
