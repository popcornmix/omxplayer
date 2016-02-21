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

#if (defined HAVE_CONFIG_H) && (!defined WIN32)
  #include "config.h"
#elif defined(_WIN32)
#include "system.h"
#endif

#include "OMXPlayerAudio.h"

#include <stdio.h>
#include <unistd.h>

#include "linux/XMemUtils.h"

OMXPlayerAudio::OMXPlayerAudio()
{
  m_open          = false;
  m_stream_id     = -1;
  m_pStream       = NULL;
  m_av_clock      = NULL;
  m_omx_reader    = NULL;
  m_decoder       = NULL;
  m_flush         = false;
  m_flush_requested = false;
  m_cached_size   = 0;
  m_pAudioCodec   = NULL;
  m_player_error  = true;
  m_CurrentVolume = 0.0f;
  m_amplification = 0;
  m_mute          = false;

  pthread_cond_init(&m_packet_cond, NULL);
  pthread_cond_init(&m_audio_cond, NULL);
  pthread_mutex_init(&m_lock, NULL);
  pthread_mutex_init(&m_lock_decoder, NULL);
}

OMXPlayerAudio::~OMXPlayerAudio()
{
  Close();

  pthread_cond_destroy(&m_audio_cond);
  pthread_cond_destroy(&m_packet_cond);
  pthread_mutex_destroy(&m_lock);
  pthread_mutex_destroy(&m_lock_decoder);
}

void OMXPlayerAudio::Lock()
{
  if(m_config.use_thread)
    pthread_mutex_lock(&m_lock);
}

void OMXPlayerAudio::UnLock()
{
  if(m_config.use_thread)
    pthread_mutex_unlock(&m_lock);
}

void OMXPlayerAudio::LockDecoder()
{
  if(m_config.use_thread)
    pthread_mutex_lock(&m_lock_decoder);
}

void OMXPlayerAudio::UnLockDecoder()
{
  if(m_config.use_thread)
    pthread_mutex_unlock(&m_lock_decoder);
}

bool OMXPlayerAudio::Open(OMXClock *av_clock, const OMXAudioConfig &config, OMXReader *omx_reader)
{
  if(ThreadHandle())
    Close();

  if (!m_dllAvUtil.Load() || !m_dllAvCodec.Load() || !m_dllAvFormat.Load() || !av_clock)
    return false;
  
  m_dllAvFormat.av_register_all();

  m_config      = config;
  m_av_clock    = av_clock;
  m_omx_reader  = omx_reader;
  m_passthrough = false;
  m_hw_decode   = false;
  m_iCurrentPts = DVD_NOPTS_VALUE;
  m_bAbort      = false;
  m_flush       = false;
  m_flush_requested = false;
  m_cached_size = 0;
  m_pAudioCodec = NULL;

  m_player_error = OpenAudioCodec();
  if(!m_player_error)
  {
    Close();
    return false;
  }

  m_player_error = OpenDecoder();
  if(!m_player_error)
  {
    Close();
    return false;
  }

  if(m_config.use_thread)
    Create();

  m_open        = true;

  return true;
}

bool OMXPlayerAudio::Close()
{
  m_bAbort  = true;

  Flush();

  if(ThreadHandle())
  {
    Lock();
    pthread_cond_broadcast(&m_packet_cond);
    UnLock();

    StopThread();
  }

  CloseDecoder();
  CloseAudioCodec();

  m_open          = false;
  m_stream_id     = -1;
  m_iCurrentPts   = DVD_NOPTS_VALUE;
  m_pStream       = NULL;

  m_dllAvUtil.Unload();
  m_dllAvCodec.Unload();
  m_dllAvFormat.Unload();

  return true;
}


bool OMXPlayerAudio::Decode(OMXPacket *pkt)
{
  if(!pkt)
    return false;

  /* last decoder reinit went wrong */
  if(!m_decoder || !m_pAudioCodec)
    return true;

  if(!m_omx_reader->IsActive(OMXSTREAM_AUDIO, pkt->stream_index))
    return true; 

  int channels = pkt->hints.channels;

  unsigned int old_bitrate = m_config.hints.bitrate;
  unsigned int new_bitrate = pkt->hints.bitrate;

  /* only check bitrate changes on AV_CODEC_ID_DTS, AV_CODEC_ID_AC3, AV_CODEC_ID_EAC3 */
  if(m_config.hints.codec != AV_CODEC_ID_DTS && m_config.hints.codec != AV_CODEC_ID_AC3 && m_config.hints.codec != AV_CODEC_ID_EAC3)
  {
    new_bitrate = old_bitrate = 0;
  }

  // for passthrough we only care about the codec and the samplerate
  bool minor_change = channels                 != m_config.hints.channels ||
                      pkt->hints.bitspersample != m_config.hints.bitspersample ||
                      old_bitrate              != new_bitrate;

  if(pkt->hints.codec          != m_config.hints.codec ||
     pkt->hints.samplerate     != m_config.hints.samplerate ||
     (!m_passthrough && minor_change))
  {
    printf("C : %d %d %d %d %d\n", m_config.hints.codec, m_config.hints.channels, m_config.hints.samplerate, m_config.hints.bitrate, m_config.hints.bitspersample);
    printf("N : %d %d %d %d %d\n", pkt->hints.codec, channels, pkt->hints.samplerate, pkt->hints.bitrate, pkt->hints.bitspersample);


    CloseDecoder();
    CloseAudioCodec();

    m_config.hints = pkt->hints;

    m_player_error = OpenAudioCodec();
    if(!m_player_error)
      return false;

    m_player_error = OpenDecoder();
    if(!m_player_error)
      return false;
  }

  CLog::Log(LOGINFO, "CDVDPlayerAudio::Decode dts:%.0f pts:%.0f size:%d", pkt->dts, pkt->pts, pkt->size);

  if(pkt->pts != DVD_NOPTS_VALUE)
    m_iCurrentPts = pkt->pts;
  else if(pkt->dts != DVD_NOPTS_VALUE)
    m_iCurrentPts = pkt->dts;

  const uint8_t *data_dec = pkt->data;
  int            data_len = pkt->size;

  if(!m_passthrough && !m_hw_decode)
  {
    double dts = pkt->dts, pts=pkt->pts;
    while(data_len > 0)
    {
      int len = m_pAudioCodec->Decode((BYTE *)data_dec, data_len, dts, pts);
      if( (len < 0) || (len >  data_len) )
      {
        m_pAudioCodec->Reset();
        break;
      }

      data_dec+= len;
      data_len -= len;

      uint8_t *decoded;
      int decoded_size = m_pAudioCodec->GetData(&decoded, dts, pts);

      if(decoded_size <=0)
        continue;

      while((int) m_decoder->GetSpace() < decoded_size)
      {
        OMXClock::OMXSleep(10);
        if(m_flush_requested) return true;
      }

      int ret = 0;

      ret = m_decoder->AddPackets(decoded, decoded_size, dts, pts, m_pAudioCodec->GetFrameSize());
      if(ret != decoded_size)
      {
        printf("error ret %d decoded_size %d\n", ret, decoded_size);
      }
    }
  }
  else
  {
    while((int) m_decoder->GetSpace() < pkt->size)
    {
      OMXClock::OMXSleep(10);
      if(m_flush_requested) return true;
    }

    m_decoder->AddPackets(pkt->data, pkt->size, pkt->dts, pkt->pts, 0);
  }

  return true;
}

void OMXPlayerAudio::Process()
{
  OMXPacket *omx_pkt = NULL;

  while(true)
  {
    Lock();
    if(!(m_bStop || m_bAbort) && m_packets.empty())
      pthread_cond_wait(&m_packet_cond, &m_lock);

    if (m_bStop || m_bAbort)
    {
      UnLock();
      break;
    }

    if(m_flush && omx_pkt)
    {
      OMXReader::FreePacket(omx_pkt);
      omx_pkt = NULL;
      m_flush = false;
    }
    else if(!omx_pkt && !m_packets.empty())
    {
      omx_pkt = m_packets.front();
      m_cached_size -= omx_pkt->size;
      m_packets.pop_front();
    }
    UnLock();
    
    LockDecoder();
    if(m_flush && omx_pkt)
    {
      OMXReader::FreePacket(omx_pkt);
      omx_pkt = NULL;
      m_flush = false;
    }
    else if(omx_pkt && Decode(omx_pkt))
    {
      OMXReader::FreePacket(omx_pkt);
      omx_pkt = NULL;
    }
    UnLockDecoder();
  }

  if(omx_pkt)
    OMXReader::FreePacket(omx_pkt);
}

void OMXPlayerAudio::Flush()
{
  m_flush_requested = true;
  Lock();
  LockDecoder();
  if(m_pAudioCodec)
    m_pAudioCodec->Reset();
  m_flush_requested = false;
  m_flush = true;
  while (!m_packets.empty())
  {
    OMXPacket *pkt = m_packets.front(); 
    m_packets.pop_front();
    OMXReader::FreePacket(pkt);
  }
  m_iCurrentPts = DVD_NOPTS_VALUE;
  m_cached_size = 0;
  if(m_decoder)
    m_decoder->Flush();
  UnLockDecoder();
  UnLock();
}

bool OMXPlayerAudio::AddPacket(OMXPacket *pkt)
{
  bool ret = false;

  if(!pkt)
    return ret;

  if(m_bStop || m_bAbort)
    return ret;

  if((m_cached_size + pkt->size) < m_config.queue_size * 1024 * 1024)
  {
    Lock();
    m_cached_size += pkt->size;
    m_packets.push_back(pkt);
    UnLock();
    ret = true;
    pthread_cond_broadcast(&m_packet_cond);
  }

  return ret;
}

bool OMXPlayerAudio::OpenAudioCodec()
{
  m_pAudioCodec = new COMXAudioCodecOMX();

  if(!m_pAudioCodec->Open(m_config.hints, m_config.layout))
  {
    delete m_pAudioCodec; m_pAudioCodec = NULL;
    return false;
  }

  return true;
}

void OMXPlayerAudio::CloseAudioCodec()
{
  if(m_pAudioCodec)
    delete m_pAudioCodec;
  m_pAudioCodec = NULL;
}

bool OMXPlayerAudio::IsPassthrough(COMXStreamInfo hints)
{
  if(m_config.device == "omx:local")
    return false;

  bool passthrough = false;

  if(hints.codec == AV_CODEC_ID_AC3)
  {
    passthrough = true;
  }
  if(hints.codec == AV_CODEC_ID_EAC3)
  {
    passthrough = true;
  }
  if(hints.codec == AV_CODEC_ID_DTS)
  {
    passthrough = true;
  }

  return passthrough;
}

bool OMXPlayerAudio::OpenDecoder()
{
  bool bAudioRenderOpen = false;

  m_decoder = new COMXAudio();

  if(m_config.passthrough)
    m_passthrough = IsPassthrough(m_config.hints);

  if(!m_passthrough && m_config.hwdecode)
    m_hw_decode = COMXAudio::HWDecode(m_config.hints.codec);

  if(m_passthrough)
    m_hw_decode = false;

  bAudioRenderOpen = m_decoder->Initialize(m_av_clock, m_config, m_pAudioCodec->GetChannelMap(), m_pAudioCodec->GetBitsPerSample());

  m_codec_name = m_omx_reader->GetCodecName(OMXSTREAM_AUDIO);
  
  if(!bAudioRenderOpen)
  {
    delete m_decoder; 
    m_decoder = NULL;
    return false;
  }
  else
  {
    if(m_passthrough)
    {
      printf("Audio codec %s passthrough channels %d samplerate %d bitspersample %d\n",
        m_codec_name.c_str(), m_config.hints.channels, m_config.hints.samplerate, m_config.hints.bitspersample);
    }
    else
    {
      printf("Audio codec %s channels %d samplerate %d bitspersample %d\n",
        m_codec_name.c_str(), m_config.hints.channels, m_config.hints.samplerate, m_config.hints.bitspersample);
    }
  }
  // setup current volume settings
  m_decoder->SetVolume(m_CurrentVolume);
  m_decoder->SetMute(m_mute);
  m_decoder->SetDynamicRangeCompression(m_amplification);

  return true;
}

bool OMXPlayerAudio::CloseDecoder()
{
  if(m_decoder)
    delete m_decoder;
  m_decoder   = NULL;
  return true;
}

double OMXPlayerAudio::GetDelay()
{
  if(m_decoder)
    return m_decoder->GetDelay();
  else
    return 0;
}

double OMXPlayerAudio::GetCacheTime()
{
  if(m_decoder)
    return m_decoder->GetCacheTime();
  else
    return 0;
}

double OMXPlayerAudio::GetCacheTotal()
{
  if(m_decoder)
    return m_decoder->GetCacheTotal();
  else
    return 0;
}

void OMXPlayerAudio::SubmitEOS()
{
  if(m_decoder)
    m_decoder->SubmitEOS();
}

bool OMXPlayerAudio::IsEOS()
{
  return m_packets.empty() && (!m_decoder || m_decoder->IsEOS());
}

void OMXPlayerAudio::WaitCompletion()
{
  if(!m_decoder)
    return;

  unsigned int nTimeOut = m_config.fifo_size * 1000;
  while(nTimeOut)
  {
    if(IsEOS())
    {
      CLog::Log(LOGDEBUG, "%s::%s - got eos\n", "OMXPlayerAudio", __func__);
      break;
    }

    if(nTimeOut == 0)
    {
      CLog::Log(LOGERROR, "%s::%s - wait for eos timed out\n", "OMXPlayerAudio", __func__);
      break;
    }
    OMXClock::OMXSleep(50);
    nTimeOut -= 50;
  }
} 

