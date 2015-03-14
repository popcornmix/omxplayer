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

#ifndef _OMX_PLAYERVIDEO_H_
#define _OMX_PLAYERVIDEO_H_

#include "DllAvUtil.h"
#include "DllAvFormat.h"
#include "DllAvCodec.h"

#include "OMXReader.h"
#include "OMXClock.h"
#include "OMXStreamInfo.h"
#include "OMXVideo.h"
#include "OMXThread.h"

#include <deque>
#include <sys/types.h>

#include <string>
#include <atomic>

using namespace std;

class OMXPlayerVideo : public OMXThread
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
  pthread_cond_t            m_picture_cond;
  pthread_mutex_t           m_lock;
  pthread_mutex_t           m_lock_decoder;
  OMXClock                  *m_av_clock;
  COMXVideo                 *m_decoder;
  float                     m_fps;
  double                    m_frametime;
  EDEINTERLACEMODE          m_Deinterlace;
  OMX_IMAGEFILTERANAGLYPHTYPE m_anaglyph;
  float                     m_display_aspect;
  CRect                     m_DestRect;
  bool                      m_bAbort;
  bool                      m_use_thread;
  bool                      m_flush;
  std::atomic<bool>         m_flush_requested;
  unsigned int              m_cached_size;
  unsigned int              m_max_data_size;
  float                     m_fifo_size;
  bool                      m_hdmi_clock_sync;
  double                    m_iVideoDelay;
  uint32_t                  m_history_valid_pts;
  int                       m_display;
  int                       m_layer;
  int                       m_alpha;

  void Lock();
  void UnLock();
  void LockDecoder();
  void UnLockDecoder();
private:
public:
  OMXPlayerVideo();
  ~OMXPlayerVideo();
  bool Open(COMXStreamInfo &hints, OMXClock *av_clock, const CRect& DestRect, EDEINTERLACEMODE deinterlace, OMX_IMAGEFILTERANAGLYPHTYPE anaglyph, bool hdmi_clock_sync, bool use_thread,
                   float display_aspect, int alpha, int display, int layer, float queue_size, float fifo_size);
  bool Close();
  bool Reset();
  bool Decode(OMXPacket *pkt);
  void Process();
  void Flush();
  bool AddPacket(OMXPacket *pkt);
  bool OpenDecoder();
  bool CloseDecoder();
  int  GetDecoderBufferSize();
  int  GetDecoderFreeSpace();
  double GetCurrentPTS() { return m_iCurrentPts; };
  double GetFPS() { return m_fps; };
  unsigned int GetCached() { return m_cached_size; };
  unsigned int GetMaxCached() { return m_max_data_size; };
  unsigned int GetLevel() { return m_max_data_size ? 100 * m_cached_size / m_max_data_size : 0; };
  void SubmitEOS();
  bool IsEOS();
  void SetDelay(double delay) { m_iVideoDelay = delay; }
  double GetDelay() { return m_iVideoDelay; }
  void SetAlpha(int alpha);

};
#endif
