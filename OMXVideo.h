#pragma once
/*
 *      Copyright (C) 2010 Team XBMC
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

#if defined(HAVE_OMXLIB)

#include "OMXCore.h"
#include "OMXStreamInfo.h"

#include <IL/OMX_Video.h>

#include "OMXClock.h"
#include "OMXReader.h"

#include "guilib/Geometry.h"
#include "utils/SingleLock.h"

#define VIDEO_BUFFERS 60

enum EDEINTERLACEMODE
{
  VS_DEINTERLACEMODE_OFF=0,
  VS_DEINTERLACEMODE_AUTO=1,
  VS_DEINTERLACEMODE_FORCE=2
};

#define CLASSNAME "COMXVideo"

class DllAvUtil;
class DllAvFormat;
class COMXVideo
{
public:
  COMXVideo();
  ~COMXVideo();

  // Required overrides
  bool SendDecoderConfig();
  bool NaluFormatStartCodes(enum AVCodecID codec, uint8_t *in_extradata, int in_extrasize);
  bool Open(COMXStreamInfo &hints, OMXClock *clock, const CRect &m_DestRect, float display_aspect = 0.0f, EDEINTERLACEMODE deinterlace = VS_DEINTERLACEMODE_OFF,
            OMX_IMAGEFILTERANAGLYPHTYPE anaglyph = OMX_ImageFilterAnaglyphNone, bool hdmi_clock_sync = false, int alpha = 255, int display = 0, int layer = 0, float fifo_size = 0.0f);
  bool PortSettingsChanged();
  void Close(void);
  unsigned int GetFreeSpace();
  unsigned int GetSize();
  int  Decode(uint8_t *pData, int iSize, double pts);
  void Reset(void);
  void SetDropState(bool bDrop);
  std::string GetDecoderName() { return m_video_codec_name; };
  void SetVideoRect(const CRect& SrcRect, const CRect& DestRect);
  void SetAlpha(int alpha);
  int GetInputBufferSize();
  void SubmitEOS();
  bool IsEOS();
  bool SubmittedEOS() { return m_submitted_eos; }
  bool BadState() { return m_omx_decoder.BadState(); };
protected:
  // Video format
  bool              m_drop_state;
  unsigned int      m_decoded_width;
  unsigned int      m_decoded_height;
  float             m_display_pixel_aspect;

  OMX_VIDEO_CODINGTYPE m_codingType;

  COMXCoreComponent m_omx_decoder;
  COMXCoreComponent m_omx_render;
  COMXCoreComponent m_omx_sched;
  COMXCoreComponent m_omx_image_fx;
  COMXCoreComponent *m_omx_clock;
  OMXClock           *m_av_clock;

  COMXCoreTunel     m_omx_tunnel_decoder;
  COMXCoreTunel     m_omx_tunnel_clock;
  COMXCoreTunel     m_omx_tunnel_sched;
  COMXCoreTunel     m_omx_tunnel_image_fx;
  bool              m_is_open;

  bool              m_setStartTime;

  uint8_t           *m_extradata;
  int               m_extrasize;

  std::string       m_video_codec_name;

  bool              m_deinterlace;
  EDEINTERLACEMODE  m_deinterlace_request;
  OMX_IMAGEFILTERANAGLYPHTYPE m_anaglyph;
  bool              m_hdmi_clock_sync;
  CRect             m_dst_rect;
  CRect             m_src_rect;
  float             m_pixel_aspect;
  bool              m_submitted_eos;
  bool              m_failed_eos;
  OMX_DISPLAYTRANSFORMTYPE m_transform;
  bool              m_settings_changed;
  CCriticalSection  m_critSection;
  int               m_display;
  int               m_layer;
  int               m_alpha;
};

#endif
