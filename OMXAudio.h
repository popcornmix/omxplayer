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
#include "utils/PCMRemap.h"
#include "OMXCore.h"
#include "OMXClock.h"
#include "OMXStreamInfo.h"
#include "BitstreamConverter.h"
#include "utils/PCMRemap.h"
#include "utils/SingleLock.h"

#define AUDIO_BUFFER_SECONDS 3

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
  float GetMaxLevel(double &pts);
  COMXAudio();
  bool Initialize(const CStdString& device, uint64_t channelMap,
                           COMXStreamInfo &hints, enum PCMLayout layout, unsigned int uiSamplesPerSec, unsigned int uiBitsPerSample, bool boostOnDownmix,
                           OMXClock *clock, bool bUsePassthrough = false, bool bUseHWDecode = false, bool is_live = false, float fifo_size = 0);
  ~COMXAudio();
  bool PortSettingsChanged();

  unsigned int AddPackets(const void* data, unsigned int len);
  unsigned int AddPackets(const void* data, unsigned int len, double dts, double pts, unsigned int frame_size);
  unsigned int GetSpace();
  bool Deinitialize();

  void SetVolume(float nVolume);
  float GetVolume();
  void SetBalance(float nBalance);
  float GetBalance();
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
  void UpdateAttenuation();
  void BuildChannelMap(enum PCMChannels *channelMap, uint64_t layout);
  int BuildChannelMapCEA(enum PCMChannels *channelMap, uint64_t layout);
  void BuildChannelMapOMX(enum OMX_AUDIO_CHANNELTYPE *channelMap, uint64_t layout);
  uint64_t GetChannelLayout(enum PCMLayout layout);

private:
  bool          m_Initialized;
  float         m_CurrentVolume;
  float         m_Balance;
  bool          m_Mute;
  long          m_drc;
  bool          m_Passthrough;
  bool          m_HWDecode;
  bool          m_normalize_downmix;
  unsigned int  m_BytesPerSec;
  unsigned int  m_InputBytesPerSec;
  unsigned int  m_BufferLen;
  unsigned int  m_ChunkLen;
  unsigned int  m_InputChannels;
  unsigned int  m_OutputChannels;
  unsigned int  m_BitsPerSample;
  float		m_maxLevel;
  float         m_amplification;
  float         m_attenuation;
  float         m_submitted;
  COMXCoreComponent *m_omx_clock;
  OMXClock      *m_av_clock;
  bool          m_settings_changed;
  bool          m_setStartTime;
  int           m_SampleRate;
  OMX_AUDIO_CODINGTYPE m_eEncoding;
  uint8_t       *m_extradata;
  int           m_extrasize;
  std::string   m_deviceuse;
  double        m_last_pts;
  bool          m_submitted_eos;
  bool          m_failed_eos;
  float         m_fifo_size;
  bool          m_live;

  OMX_AUDIO_CHANNELTYPE m_input_channels[OMX_AUDIO_MAXCHANNELS];
  OMX_AUDIO_CHANNELTYPE m_output_channels[OMX_AUDIO_MAXCHANNELS];
  OMX_AUDIO_PARAM_PCMMODETYPE m_pcm_output;
  OMX_AUDIO_PARAM_PCMMODETYPE m_pcm_input;
  OMX_AUDIO_PARAM_DTSTYPE     m_dtsParam;
  WAVEFORMATEXTENSIBLE        m_wave_header;
  typedef struct {
    double pts;
    float level;
  } amplitudes_t;
  std::deque<amplitudes_t> m_ampqueue;
  float m_downmix_matrix[OMX_AUDIO_MAXCHANNELS*OMX_AUDIO_MAXCHANNELS];

protected:
  COMXCoreComponent m_omx_render_analog;
  COMXCoreComponent m_omx_render_hdmi;
  COMXCoreComponent m_omx_splitter;
  COMXCoreComponent m_omx_mixer;
  COMXCoreComponent m_omx_decoder;
  COMXCoreTunel     m_omx_tunnel_clock_analog;
  COMXCoreTunel     m_omx_tunnel_clock_hdmi;
  COMXCoreTunel     m_omx_tunnel_mixer;
  COMXCoreTunel     m_omx_tunnel_decoder;
  COMXCoreTunel     m_omx_tunnel_splitter_analog;
  COMXCoreTunel     m_omx_tunnel_splitter_hdmi;
  DllAvUtil         m_dllAvUtil;
  CCriticalSection m_critSection;
};
#endif

