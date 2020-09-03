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

#ifndef _OMX_PLAYERAUDIO_H_
#define _OMX_PLAYERAUDIO_H_

#include "DllAvUtil.h"
#include "DllAvFormat.h"
#include "DllAvCodec.h"

#include "utils/PCMRemap.h"

#include "OMXReader.h"
#include "OMXClock.h"
#include "OMXStreamInfo.h"
#include "OMXAudio.h"
#include "OMXAudioCodecOMX.h"
#include "OMXThread.h"

#include <deque>
#include <string>
#include <atomic>
#include <sys/types.h>

using namespace std;

class OMXPlayerAudio : public OMXThread
{
protected:
  AVStream                  *m_pStream;
  int                       m_stream_id;
  std::deque<OMXPacket *>   m_packets;
  DllAvUtil                 m_dllAvUtil;
  DllAvCodec                m_dllAvCodec;
  DllAvFormat               m_dllAvFormat;
  bool                      m_open;
  COMXStreamInfo            m_hints;
  double                    m_iCurrentPts;
  pthread_cond_t            m_packet_cond;
  pthread_cond_t            m_audio_cond;
  pthread_mutex_t           m_lock;
  pthread_mutex_t           m_lock_decoder;
  OMXClock                  *m_av_clock;
  OMXReader                 *m_omx_reader;
  COMXAudio                 *m_decoder;
  std::string               m_codec_name;
  std::string               m_device;
  bool                      m_passthrough;
  bool                      m_hw_decode;
  bool                      m_boost_on_downmix;
  bool                      m_bAbort;
  bool                      m_flush;
  std::atomic<bool>         m_flush_requested;
  unsigned int              m_cached_size;
  OMXAudioConfig            m_config;
  COMXAudioCodecOMX         *m_pAudioCodec;
  float                     m_CurrentVolume;
  long                      m_amplification;
  bool                      m_mute;
  bool   m_player_error;

  void Lock();
  void UnLock();
  void LockDecoder();
  void UnLockDecoder();
private:
public:
  OMXPlayerAudio();
  ~OMXPlayerAudio();
  bool Open(OMXClock *av_clock, const OMXAudioConfig &config, OMXReader *omx_reader);
  bool Close();
  bool Decode(OMXPacket *pkt);
  void Process();
  void Flush();
  bool AddPacket(OMXPacket *pkt);
  bool OpenAudioCodec();
  void CloseAudioCodec();      
  bool IsPassthrough(COMXStreamInfo hints);
  bool OpenDecoder();
  bool CloseDecoder();
  double GetDelay();
  double GetCacheTime();
  double GetCacheTotal();
  double GetCurrentPTS() { return m_iCurrentPts; };
  void SubmitEOS();
  void SubmitEOSInternal();
  bool IsEOS();
  unsigned int GetCached() { return m_cached_size; };
  unsigned int GetMaxCached() { return m_config.queue_size * 1024 * 1024; };
  unsigned int GetLevel() { return m_config.queue_size ? 100.0f * m_cached_size / (m_config.queue_size * 1024.0f * 1024.0f) : 0; };
  void SetVolume(float fVolume)                          { m_CurrentVolume = fVolume; if(m_decoder) m_decoder->SetVolume(fVolume); }
  float GetVolume()                                      { return m_CurrentVolume; }
  void SetMute(bool bOnOff)                              { m_mute = bOnOff; if(m_decoder) m_decoder->SetMute(bOnOff); }
  void SetDynamicRangeCompression(long drc)              { m_amplification = drc; if(m_decoder) m_decoder->SetDynamicRangeCompression(drc); }
  bool Error() { return !m_player_error; };
};
#endif
