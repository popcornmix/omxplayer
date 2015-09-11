/*
 *      Copyright (C) 2005-2008 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#pragma once

#if (defined HAVE_CONFIG_H) && (!defined WIN32)
  #include "config.h"
#endif
#ifndef TARGET_LINUX
enum AVCodecID;
#else
extern "C" {
  #include "libavcodec/avcodec.h"
}
#endif

class CDemuxStream;

class COMXStreamInfo
{
public:
  COMXStreamInfo();

  ~COMXStreamInfo();

  void Clear(); // clears current information

  enum AVCodecID codec;
  bool software;  //force software decoding


  // VIDEO
  int fpsscale; // scale of 1000 and a rate of 29970 will result in 29.97 fps
  int fpsrate;
  int height; // height of the stream reported by the demuxer
  int width; // width of the stream reported by the demuxer
  float aspect; // display aspect as reported by demuxer
  bool vfr; // variable framerate
  bool stills; // there may be odd still frames in video
  int level; // encoder level of the stream reported by the decoder. used to qualify hw decoders.
  int profile; // encoder profile of the stream reported by the decoder. used to qualify hw decoders.
  bool ptsinvalid;  // pts cannot be trusted (avi's).
  int orientation; // video orientation in clockwise degrees

  // AUDIO
  int channels;
  int samplerate;
  int bitrate;
  int blockalign;
  int bitspersample;

  // SUBTITLE
  int identifier;

  // CODEC EXTRADATA
  void*        extradata; // extra data for codec to use
  unsigned int extrasize; // size of extra data
  unsigned int codec_tag; // extra identifier hints for decoding

  /* ac3/dts indof */
  unsigned int framesize;
  uint32_t     syncword;
};
