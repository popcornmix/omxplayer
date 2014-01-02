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

#include "OMXPlayerVideo.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>

#include "linux/XMemUtils.h"

OMXPlayerVideo::OMXPlayerVideo()
{
  m_open          = false;
  m_stream_id     = -1;
  m_pStream       = NULL;
  m_av_clock      = NULL;
  m_decoder       = NULL;
  m_fps           = 25.0f;
  m_flush         = false;
  m_flush_requested = false;
  m_cached_size   = 0;
  m_hdmi_clock_sync = false;
  m_iVideoDelay   = 0;
  m_iCurrentPts   = 0;
  m_iSubtitleDelay = 0;
  m_pSubtitleCodec = NULL;
  m_max_data_size = 10 * 1024 * 1024;
  m_fifo_size     = (float)80*1024*60 / (1024*1024);
  m_history_valid_pts = 0;

  pthread_cond_init(&m_packet_cond, NULL);
  pthread_cond_init(&m_picture_cond, NULL);
  pthread_mutex_init(&m_lock, NULL);
  pthread_mutex_init(&m_lock_decoder, NULL);
  pthread_mutex_init(&m_lock_subtitle, NULL);
}

OMXPlayerVideo::~OMXPlayerVideo()
{
  Close();

  pthread_cond_destroy(&m_packet_cond);
  pthread_cond_destroy(&m_picture_cond);
  pthread_mutex_destroy(&m_lock);
  pthread_mutex_destroy(&m_lock_decoder);
  pthread_mutex_destroy(&m_lock_subtitle);
}

void OMXPlayerVideo::Lock()
{
  if(m_use_thread)
    pthread_mutex_lock(&m_lock);
}

void OMXPlayerVideo::UnLock()
{
  if(m_use_thread)
    pthread_mutex_unlock(&m_lock);
}

void OMXPlayerVideo::LockDecoder()
{
  if(m_use_thread)
    pthread_mutex_lock(&m_lock_decoder);
}

void OMXPlayerVideo::UnLockDecoder()
{
  if(m_use_thread)
    pthread_mutex_unlock(&m_lock_decoder);
}

void OMXPlayerVideo::LockSubtitles()
{
  if(m_use_thread)
    pthread_mutex_lock(&m_lock_subtitle);
}

void OMXPlayerVideo::UnLockSubtitles()
{
  if(m_use_thread)
    pthread_mutex_unlock(&m_lock_subtitle);
}

bool OMXPlayerVideo::Open(COMXStreamInfo &hints, OMXClock *av_clock, const CRect& DestRect, EDEINTERLACEMODE deinterlace, bool hdmi_clock_sync, bool use_thread,
                             float display_aspect, float queue_size, float fifo_size)
{
  if (!m_dllAvUtil.Load() || !m_dllAvCodec.Load() || !m_dllAvFormat.Load() || !av_clock)
    return false;
  
  if(ThreadHandle())
    Close();

  m_dllAvFormat.av_register_all();

  m_hints       = hints;
  m_av_clock    = av_clock;
  m_fps         = 25.0f;
  m_frametime   = 0;
  m_Deinterlace = deinterlace;
  m_display_aspect = display_aspect;
  m_iCurrentPts = DVD_NOPTS_VALUE;
  m_bAbort      = false;
  m_use_thread  = use_thread;
  m_flush       = false;
  m_cached_size = 0;
  m_iVideoDelay = 0;
  m_hdmi_clock_sync = hdmi_clock_sync;
  m_iSubtitleDelay = 0;
  m_pSubtitleCodec = NULL;
  m_DestRect    = DestRect;
  if (queue_size != 0.0)
    m_max_data_size = queue_size * 1024 * 1024;
  if (fifo_size != 0.0)
    m_fifo_size = fifo_size;

  if(!OpenDecoder())
  {
    Close();
    return false;
  }

  if(m_use_thread)
    Create();

  m_open        = true;

  return true;
}

bool OMXPlayerVideo::Close()
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

  m_dllAvUtil.Unload();
  m_dllAvCodec.Unload();
  m_dllAvFormat.Unload();

  m_open          = false;
  m_stream_id     = -1;
  m_iCurrentPts   = DVD_NOPTS_VALUE;
  m_pStream       = NULL;
  m_pSubtitleCodec = NULL;

  return true;
}

static unsigned count_bits(int32_t value)
{
  unsigned bits = 0;
  for(;value;++bits)
    value &= value - 1;
  return bits;
}

bool OMXPlayerVideo::Decode(OMXPacket *pkt)
{
  if(!pkt)
    return false;

  if(pkt->hints.codec == CODEC_ID_TEXT ||
     pkt->hints.codec == CODEC_ID_SSA )
  {
    if(!m_pSubtitleCodec)
    {
      m_pSubtitleCodec = new COMXOverlayCodecText();
      m_pSubtitleCodec->Open( pkt->hints );
    }
    int result = m_pSubtitleCodec->Decode(pkt->data, pkt->size, pkt->pts, pkt->duration);
    COMXOverlay* overlay;

    std::string strSubtitle = "";

    double pts = pkt->dts != DVD_NOPTS_VALUE ? pkt->dts : pkt->pts;
    double duration = pkt->duration;

    if(result == OC_OVERLAY)
    {

      while((overlay = m_pSubtitleCodec->GetOverlay()) != NULL)
      {
        if(overlay->iPTSStopTime > overlay->iPTSStartTime)
          duration = overlay->iPTSStopTime - overlay->iPTSStartTime;
        else if(pkt->duration != DVD_NOPTS_VALUE)
          duration = pkt->duration;
        else
          duration = 0.0;

        if     (pkt->pts != DVD_NOPTS_VALUE)
          pts = pkt->pts;
        else if(pkt->dts != DVD_NOPTS_VALUE)
          pts = pkt->dts;
        else
          pts = overlay->iPTSStartTime;

        pts -= m_iSubtitleDelay;

        overlay->iPTSStartTime = pts;
        if(duration)
          overlay->iPTSStopTime = pts + duration;
        else
        {
          overlay->iPTSStopTime = 0;
          overlay->replace = true;
        }

        COMXOverlayText::CElement* e = ((COMXOverlayText*)overlay)->m_pHead;
        while (e)
        {
          if (e->IsElementType(COMXOverlayText::ELEMENT_TYPE_TEXT))
          {
            COMXOverlayText::CElementText* t = (COMXOverlayText::CElementText*)e;
            strSubtitle += t->m_text;
              strSubtitle += "\n";
          }
          e = e->pNext;
        }

        m_overlays.push_back(overlay);

        if(strSubtitle.length())
          m_decoder->DecodeText((uint8_t *)strSubtitle.c_str(), strSubtitle.length(), overlay->iPTSStartTime, overlay->iPTSStartTime);
      }
    }
  }
  else
  {
    // some packed bitstream AVI files set almost all pts values to DVD_NOPTS_VALUE, but have a scattering of real pts values.
    // the valid pts values match the dts values.
    // if a stream has had more than 4 valid pts values in the last 16, the use UNKNOWN, otherwise use dts
    m_history_valid_pts = (m_history_valid_pts << 1) | (pkt->pts != DVD_NOPTS_VALUE);
    double pts = pkt->pts;
    if(pkt->pts == DVD_NOPTS_VALUE && (m_iCurrentPts == DVD_NOPTS_VALUE || count_bits(m_history_valid_pts & 0xffff) < 4))
      pts = pkt->dts;

    if (pts != DVD_NOPTS_VALUE)
      pts += m_iVideoDelay;

    if(pts != DVD_NOPTS_VALUE)
      m_iCurrentPts = pts;

    while((int) m_decoder->GetFreeSpace() < pkt->size)
    {
      OMXClock::OMXSleep(10);
      if(m_flush_requested) return true;
    }

    CLog::Log(LOGINFO, "CDVDPlayerVideo::Decode dts:%.0f pts:%.0f cur:%.0f, size:%d", pkt->dts, pkt->pts, m_iCurrentPts, pkt->size);
    m_decoder->Decode(pkt->data, pkt->size, pts);
  }
  return true;
}

void OMXPlayerVideo::Process()
{
  OMXPacket *omx_pkt = NULL;

  while(!m_bStop && !m_bAbort)
  {
    Lock();
    if(m_packets.empty())
      pthread_cond_wait(&m_packet_cond, &m_lock);
    UnLock();

    if(m_bAbort)
      break;

    Lock();
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
    
    OMXPacket *subtitle_pkt = m_decoder->GetText();

    if(subtitle_pkt)
    {
      LockSubtitles();
      subtitle_pkt->pts = m_av_clock->OMXMediaTime();
      m_subtitle_packets.push_back(subtitle_pkt);
      UnLockSubtitles();
    }
  }

  if(omx_pkt)
    OMXReader::FreePacket(omx_pkt);
}

void OMXPlayerVideo::FlushSubtitles()
{
  LockDecoder();
  LockSubtitles();
  while (!m_subtitle_packets.empty())
  {
    OMXPacket *pkt = m_subtitle_packets.front(); 
    m_subtitle_packets.pop_front();
    OMXReader::FreePacket(pkt);
  }
  while (!m_overlays.empty())
  {
    COMXOverlay *overlay = m_overlays.front(); 
    m_overlays.pop_front();
    delete overlay;
  }
  if(m_pSubtitleCodec)
    delete m_pSubtitleCodec;
  m_pSubtitleCodec = NULL;
  UnLockSubtitles();
  UnLockDecoder();
}

void OMXPlayerVideo::Flush()
{
  m_flush_requested = true;
  Lock();
  LockDecoder();
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
    m_decoder->Reset();
  UnLockDecoder();
  FlushSubtitles();
  UnLock();
}

bool OMXPlayerVideo::AddPacket(OMXPacket *pkt)
{
  bool ret = false;

  if(!pkt)
    return ret;

  if(m_bStop || m_bAbort)
    return ret;

  if((m_cached_size + pkt->size) < m_max_data_size)
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

bool OMXPlayerVideo::OpenDecoder()
{
  if (m_hints.fpsrate && m_hints.fpsscale)
    m_fps = DVD_TIME_BASE / OMXReader::NormalizeFrameduration((double)DVD_TIME_BASE * m_hints.fpsscale / m_hints.fpsrate);
  else
    m_fps = 25;

  if( m_fps > 100 || m_fps < 5 )
  {
    printf("Invalid framerate %d, using forced 25fps and just trust timestamps\n", (int)m_fps);
    m_fps = 25;
  }

  m_frametime = (double)DVD_TIME_BASE / m_fps;

  m_decoder = new COMXVideo();
  if(!m_decoder->Open(m_hints, m_av_clock, m_DestRect, m_display_aspect, m_Deinterlace, m_hdmi_clock_sync, m_fifo_size))
  {
    CloseDecoder();
    return false;
  }
  else
  {
    printf("Video codec %s width %d height %d profile %d fps %f\n",
        m_decoder->GetDecoderName().c_str() , m_hints.width, m_hints.height, m_hints.profile, m_fps);
  }

  // start from assuming all recent frames had valid pts
  m_history_valid_pts = ~0;

  return true;
}

bool OMXPlayerVideo::CloseDecoder()
{
  if(m_decoder)
    delete m_decoder;
  m_decoder   = NULL;
  return true;
}

int  OMXPlayerVideo::GetDecoderBufferSize()
{
  if(m_decoder)
    return m_decoder->GetInputBufferSize();
  else
    return 0;
}

int  OMXPlayerVideo::GetDecoderFreeSpace()
{
  if(m_decoder)
    return m_decoder->GetFreeSpace();
  else
    return 0;
}

void OMXPlayerVideo::SubmitEOS()
{
  if(m_decoder)
    m_decoder->SubmitEOS();
}

bool OMXPlayerVideo::IsEOS()
{
  if(!m_decoder)
    return false;
  return m_packets.empty() && (!m_decoder || m_decoder->IsEOS());
}

std::string OMXPlayerVideo::GetText()
{
  OMXPacket *pkt = NULL;
  std::string strSubtitle = "";

  LockSubtitles();
  if (!m_subtitle_packets.empty())
  {
    pkt = m_subtitle_packets.front(); 
    if(!m_overlays.empty())
    {
      COMXOverlay *overlay = m_overlays.front();
      double now = m_av_clock->OMXMediaTime();
      double iPTSStartTime = pkt->pts;
      double iPTSStopTime = (overlay->iPTSStartTime > 0) ? iPTSStartTime + (overlay->iPTSStopTime - overlay->iPTSStartTime) : 0LL;

      if((iPTSStartTime <= now)
        && (iPTSStopTime >= now || iPTSStopTime == 0LL))
      {
        COMXOverlayText::CElement* e = ((COMXOverlayText*)overlay)->m_pHead;
        while (e)
        {
          if (e->IsElementType(COMXOverlayText::ELEMENT_TYPE_TEXT))
          {
            COMXOverlayText::CElementText* t = (COMXOverlayText::CElementText*)e;
            strSubtitle += t->m_text;
              strSubtitle += "\n";
          }
          e = e->pNext;
        }
      }
      else if(iPTSStopTime < now)
      {
        m_subtitle_packets.pop_front();
        m_overlays.pop_front();
        delete overlay;
        OMXReader::FreePacket(pkt);
      }
    }
  }
  UnLockSubtitles();

  return strSubtitle;
}
