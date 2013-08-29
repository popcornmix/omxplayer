/*
* XBMC Media Center
* Copyright (c) 2002 d7o3g4q and RUNTiME
* Portions Copyright (c) by the authors of ffmpeg and xvid
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

//////////////////////////////////////////////////////////////////////

#ifndef __OPENMAXAUDIORENDER_H__
#define __OPENMAXAUDIORENDER_H__

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "linux/PlatformDefs.h"
#include "DllAvCodec.h"
#include "DllAvUtil.h"
#include "OMXCore.h"
#include "OMXClock.h"
#include "OMXStreamInfo.h"
#include "BitstreamConverter.h"
#include "utils/PCMRemap.h"

class COMXAudio
{
public:
  enum EEncoded {
    ENCODED_NONE = 0,
    ENCODED_IEC61937_AC3,
    ENCODED_IEC61937_EAC3,
    ENCODED_IEC61937_DTS,
    ENCODED_IEC61937_MPEG,
    ENCODED_IEC61937_UNKNOWN,
  };

  unsigned int GetChunkLen();
  float GetDelay();
  float GetCacheTime();
  float GetCacheTotal();
  unsigned int GetAudioRenderingLatency();
  COMXAudio();
  bool Initialize(const CStdString& device, int iChannels, enum PCMChannels *channelMap,
                           COMXStreamInfo &hints, unsigned int downmixChannels, unsigned int uiSamplesPerSec, unsigned int uiBitsPerSample, bool boostOnDownmix,
                           OMXClock *clock, bool bUsePassthrough = false, bool bUseHWDecode = false, float fifo_size = 0);
  ~COMXAudio();
  bool PortSettingsChanged();

  unsigned int AddPackets(const void* data, unsigned int len);
  unsigned int AddPackets(const void* data, unsigned int len, double dts, double pts);
  unsigned int GetSpace();
  bool Deinitialize();

  void SetVolume(float nVolume);
  float GetVolume();
  void SetMute(bool bOnOff);
  void SetDynamicRangeCompression(long drc);
  bool ApplyVolume();
  void SubmitEOS();
  bool IsEOS();

  void Flush();

  void Process();

  bool SetClock(OMXClock *clock);
  void SetCodingType(AVCodecID codec);
  bool CanHWDecode(AVCodecID codec);
  static bool HWDecode(AVCodecID codec);

  void PrintChannels(OMX_AUDIO_CHANNELTYPE eChannelMapping[]);
  void PrintPCM(OMX_AUDIO_PARAM_PCMMODETYPE *pcm, std::string direction);
  void PrintDDP(OMX_AUDIO_PARAM_DDPTYPE *ddparm);
  void PrintDTS(OMX_AUDIO_PARAM_DTSTYPE *dtsparam);
  unsigned int SyncDTS(BYTE* pData, unsigned int iSize);
  unsigned int SyncAC3(BYTE* pData, unsigned int iSize);

private:
  bool          m_Initialized;
  float         m_CurrentVolume;
  bool          m_Mute;
  long          m_drc;
  bool          m_Passthrough;
  bool          m_HWDecode;
  bool          m_normalize_downmix;
  unsigned int  m_BytesPerSec;
  unsigned int  m_BufferLen;
  unsigned int  m_ChunkLen;
  unsigned int  m_InputChannels;
  unsigned int  m_OutputChannels;
  unsigned int  m_downmix_channels;
  unsigned int  m_BitsPerSample;
  COMXCoreComponent *m_omx_clock;
  OMXClock      *m_av_clock;
  bool          m_settings_changed;
  bool          m_setStartTime;
  bool          m_LostSync;
  int           m_SampleRate;
  OMX_AUDIO_CODINGTYPE m_eEncoding;
  uint8_t       *m_extradata;
  int           m_extrasize;
  std::string   m_deviceuse;
  double        m_last_pts;
  bool          m_submitted_eos;
  float         m_fifo_size;
  OMX_AUDIO_PARAM_PCMMODETYPE m_pcm_output;
  OMX_AUDIO_PARAM_PCMMODETYPE m_pcm_input;
  OMX_AUDIO_PARAM_DTSTYPE     m_dtsParam;
  WAVEFORMATEXTENSIBLE        m_wave_header;

protected:
  COMXCoreComponent m_omx_render;
  COMXCoreComponent m_omx_mixer;
  COMXCoreComponent m_omx_decoder;
  COMXCoreTunel     m_omx_tunnel_clock;
  COMXCoreTunel     m_omx_tunnel_mixer;
  COMXCoreTunel     m_omx_tunnel_decoder;
  DllAvUtil         m_dllAvUtil;
  OMX_AUDIO_CHANNELTYPE m_input_channels[OMX_AUDIO_MAXCHANNELS];
  CPCMRemap         m_remap;
};
#endif

