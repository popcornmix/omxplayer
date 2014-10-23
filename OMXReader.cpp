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

#include "OMXReader.h"
#include "OMXClock.h"

#include <stdio.h>
#include <unistd.h>

#include "linux/XMemUtils.h"

#define MAX_DATA_SIZE_VIDEO    8 * 1024 * 1024
#define MAX_DATA_SIZE_AUDIO    2 * 1024 * 1024
#define MAX_DATA_SIZE          10 * 1024 * 1024

static bool g_abort = false;

static int64_t timeout_start;
static int64_t timeout_default_duration;
static int64_t timeout_duration;

static int64_t CurrentHostCounter(void)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return( ((int64_t)now.tv_sec * 1000000000LL) + now.tv_nsec );
}

#define RESET_TIMEOUT(x) do { \
  timeout_start = CurrentHostCounter(); \
  timeout_duration = (x) * timeout_default_duration; \
} while (0)


OMXReader::OMXReader()
{
  m_open        = false;
  m_filename    = "";
  m_bMatroska   = false;
  m_bAVI        = false;
  g_abort       = false;
  m_pFile       = NULL;
  m_ioContext   = NULL;
  m_pFormatContext = NULL;
  m_eof           = false;
  m_chapter_count = 0;
  m_iCurrentPts   = DVD_NOPTS_VALUE;

  for(int i = 0; i < MAX_STREAMS; i++)
    m_streams[i].extradata = NULL;

  ClearStreams();

  pthread_mutex_init(&m_lock, NULL);
}

OMXReader::~OMXReader()
{
  Close();

  pthread_mutex_destroy(&m_lock);
}

void OMXReader::Lock()
{
  pthread_mutex_lock(&m_lock);
}

void OMXReader::UnLock()
{
  pthread_mutex_unlock(&m_lock);
}

static int interrupt_cb(void *unused)
{
  int ret = 0;
  if (g_abort)
  {
    CLog::Log(LOGERROR, "COMXPlayer::interrupt_cb - Told to abort");
    ret = 1;
  }
  else if (timeout_duration && CurrentHostCounter() - timeout_start > timeout_duration)
  {
    CLog::Log(LOGERROR, "COMXPlayer::interrupt_cb - Timed out");
    ret = 1;
  }
  return ret;
}

static int dvd_file_read(void *h, uint8_t* buf, int size)
{
  RESET_TIMEOUT(1);
  if(interrupt_cb(NULL))
    return -1;

  XFILE::CFile *pFile = (XFILE::CFile *)h;
  return pFile->Read(buf, size);
}

static offset_t dvd_file_seek(void *h, offset_t pos, int whence)
{
  RESET_TIMEOUT(1);
  if(interrupt_cb(NULL))
    return -1;

  XFILE::CFile *pFile = (XFILE::CFile *)h;
  if(whence == AVSEEK_SIZE)
    return pFile->GetLength();
  else
    return pFile->Seek(pos, whence & ~AVSEEK_FORCE);
}

bool OMXReader::Open(std::string filename, bool dump_format, bool live /* =false */, float timeout /* = 0.0f */, std::string cookie /* = "" */, std::string user_agent /* = "" */, std::string lavfdopts /* = "" */, std::string avdict /* = "" */)
{
  if (!m_dllAvUtil.Load() || !m_dllAvCodec.Load() || !m_dllAvFormat.Load())
    return false;

  timeout_default_duration = (int64_t) (timeout * 1e9);
  m_iCurrentPts = DVD_NOPTS_VALUE;
  m_filename    = filename; 
  m_speed       = DVD_PLAYSPEED_NORMAL;
  m_program     = UINT_MAX;
  const AVIOInterruptCB int_cb = { interrupt_cb, NULL };
  RESET_TIMEOUT(3);

  ClearStreams();

  m_dllAvFormat.av_register_all();
  m_dllAvFormat.avformat_network_init();
  m_dllAvUtil.av_log_set_level(dump_format ? AV_LOG_INFO:AV_LOG_QUIET);

  int           result    = -1;
  AVInputFormat *iformat  = NULL;
  unsigned char *buffer   = NULL;
  unsigned int  flags     = READ_TRUNCATED | READ_BITRATE | READ_CHUNKED;

  m_pFormatContext     = m_dllAvFormat.avformat_alloc_context();

  result = m_dllAvFormat.av_set_options_string(m_pFormatContext, lavfdopts.c_str(), ":", ",");

  if (result < 0)
  {
    CLog::Log(LOGERROR, "COMXPlayer::OpenFile - invalid lavfdopts %s ", lavfdopts.c_str());
    Close();
    return false;
  }

  AVDictionary *d = NULL;
  result = m_dllAvUtil.av_dict_parse_string(&d, avdict.c_str(), ":", ",", 0);

  if (result < 0)
  {
    CLog::Log(LOGERROR, "COMXPlayer::OpenFile - invalid avdict %s ", avdict.c_str());
    Close();
    return false;
  }

  // set the interrupt callback, appeared in libavformat 53.15.0
  m_pFormatContext->interrupt_callback = int_cb;

  // if format can be nonblocking, let's use that
  m_pFormatContext->flags |= AVFMT_FLAG_NONBLOCK;

  // strip off file://
  if(m_filename.substr(0, 7) == "file://" )
    m_filename.replace(0, 7, "");

  if(m_filename.substr(0, 8) == "shout://" )
    m_filename.replace(0, 8, "http://");

  if(m_filename.substr(0,6) == "mms://" || m_filename.substr(0,7) == "mmsh://" || m_filename.substr(0,7) == "mmst://" || m_filename.substr(0,7) == "mmsu://" ||
      m_filename.substr(0,7) == "http://" || m_filename.substr(0,8) == "https://" ||
      m_filename.substr(0,6) == "tcp://" || 
      m_filename.substr(0,7) == "rtmp://" || m_filename.substr(0,6) == "udp://" ||
      m_filename.substr(0,7) == "rtsp://" || m_filename.substr(0,6) == "rtp://" ||
      m_filename.substr(0,6) == "ftp://" || m_filename.substr(0,7) == "sftp://" ||
      m_filename.substr(0,6) == "tcp://" || m_filename.substr(0,7) == "unix://" ||
      m_filename.substr(0,6) == "smb://")
  {
    // ffmpeg dislikes the useragent from AirPlay urls
    //int idx = m_filename.Find("|User-Agent=AppleCoreMedia");
    size_t idx = m_filename.find("|");
    if(idx != string::npos)
      m_filename = m_filename.substr(0, idx);

    // Enable seeking if http, ftp
    if(!live && (m_filename.substr(0,7) == "http://" || m_filename.substr(0,6) == "ftp://" ||
       m_filename.substr(0,7) == "sftp://" || m_filename.substr(0,6) == "smb://"))
    {
       av_dict_set(&d, "seekable", "1", 0);
       if(!cookie.empty())
       {
          av_dict_set(&d, "cookies", cookie.c_str(), 0);
       }
       if(!user_agent.empty())
       {
          av_dict_set(&d, "user_agent", user_agent.c_str(), 0);
       }
    }
    CLog::Log(LOGDEBUG, "COMXPlayer::OpenFile - avformat_open_input %s ", m_filename.c_str());
    result = m_dllAvFormat.avformat_open_input(&m_pFormatContext, m_filename.c_str(), iformat, &d);
    if(av_dict_count(d) == 0)
    {
       CLog::Log(LOGDEBUG, "COMXPlayer::OpenFile - avformat_open_input enabled SEEKING ");
       if(m_filename.substr(0,7) == "http://")
         m_pFormatContext->pb->seekable = AVIO_SEEKABLE_NORMAL;
    }
    av_dict_free(&d);
    if(result < 0)
    {
      CLog::Log(LOGERROR, "COMXPlayer::OpenFile - avformat_open_input %s ", m_filename.c_str());
      Close();
      return false;
    }
  }
  else
  {
    m_pFile = new CFile();

    if (!m_pFile->Open(m_filename, flags))
    {
      CLog::Log(LOGERROR, "COMXPlayer::OpenFile - %s ", m_filename.c_str());
      Close();
      return false;
    }

    buffer = (unsigned char*)m_dllAvUtil.av_malloc(FFMPEG_FILE_BUFFER_SIZE);
    m_ioContext = m_dllAvFormat.avio_alloc_context(buffer, FFMPEG_FILE_BUFFER_SIZE, 0, m_pFile, dvd_file_read, NULL, dvd_file_seek);
    m_ioContext->max_packet_size = 6144;
    if(m_ioContext->max_packet_size)
      m_ioContext->max_packet_size *= FFMPEG_FILE_BUFFER_SIZE / m_ioContext->max_packet_size;

    if(m_pFile->IoControl(IOCTRL_SEEK_POSSIBLE, NULL) == 0)
      m_ioContext->seekable = 0;

    m_dllAvFormat.av_probe_input_buffer(m_ioContext, &iformat, m_filename.c_str(), NULL, 0, 0);

    if(!iformat)
    {
      CLog::Log(LOGERROR, "COMXPlayer::OpenFile - av_probe_input_buffer %s ", m_filename.c_str());
      Close();
      return false;
    }

    m_pFormatContext->pb = m_ioContext;
    result = m_dllAvFormat.avformat_open_input(&m_pFormatContext, m_filename.c_str(), iformat, &d);
    av_dict_free(&d);
    if(result < 0)
    {
      Close();
      return false;
    }
  }

  m_bMatroska = strncmp(m_pFormatContext->iformat->name, "matroska", 8) == 0; // for "matroska.webm"
  m_bAVI = strcmp(m_pFormatContext->iformat->name, "avi") == 0;

  // analyse very short to speed up mjpeg playback start
  if (iformat && (strcmp(iformat->name, "mjpeg") == 0) && m_ioContext->seekable == 0)
    m_pFormatContext->max_analyze_duration = 500000;

  if(/*m_bAVI || */m_bMatroska)
    m_pFormatContext->max_analyze_duration = 0;

  if (live)
    m_pFormatContext->flags |= AVFMT_FLAG_NOBUFFER;

  result = m_dllAvFormat.avformat_find_stream_info(m_pFormatContext, NULL);
  if(result < 0)
  {
    Close();
    return false;
  }

  if(!GetStreams())
  {
    Close();
    return false;
  }

  if(m_pFile)
  {
    int64_t len = m_pFile->GetLength();
    int64_t tim = GetStreamLength();

    if(len > 0 && tim > 0)
    {
      unsigned rate = len * 1000 / tim;
      unsigned maxrate = rate + 1024 * 1024 / 8;
      if(m_pFile->IoControl(IOCTRL_CACHE_SETRATE, &maxrate) >= 0)
        CLog::Log(LOGDEBUG, "COMXPlayer::OpenFile - set cache throttle rate to %u bytes per second", maxrate);
    }
  }

  m_speed       = DVD_PLAYSPEED_NORMAL;

  if(dump_format)
    m_dllAvFormat.av_dump_format(m_pFormatContext, 0, m_filename.c_str(), 0);

  UpdateCurrentPTS();

  m_open        = true;

  return true;
}

void OMXReader::ClearStreams()
{
  m_audio_index     = -1;
  m_video_index     = -1;
  m_subtitle_index  = -1;

  m_audio_count     = 0;
  m_video_count     = 0;
  m_subtitle_count  = 0;

  for(int i = 0; i < MAX_STREAMS; i++)
  {
    if(m_streams[i].extradata)
      free(m_streams[i].extradata);

    memset(m_streams[i].language, 0, sizeof(m_streams[i].language));
    m_streams[i].codec_name = "";
    m_streams[i].name       = "";
    m_streams[i].type       = OMXSTREAM_NONE;
    m_streams[i].stream     = NULL;
    m_streams[i].extradata  = NULL;
    m_streams[i].extrasize  = 0;
    m_streams[i].index      = 0;
    m_streams[i].id         = 0;
  }

  m_program     = UINT_MAX;
}

bool OMXReader::Close()
{
  if (m_pFormatContext)
  {
    if (m_ioContext && m_pFormatContext->pb && m_pFormatContext->pb != m_ioContext)
    {
      CLog::Log(LOGWARNING, "CDVDDemuxFFmpeg::Dispose - demuxer changed our byte context behind our back, possible memleak");
      m_ioContext = m_pFormatContext->pb;
    }
    m_dllAvFormat.avformat_close_input(&m_pFormatContext);
  }

  if(m_ioContext)
  {
    m_dllAvUtil.av_free(m_ioContext->buffer);
    m_dllAvUtil.av_free(m_ioContext);
  }
  
  m_ioContext       = NULL;
  m_pFormatContext  = NULL;

  if(m_pFile)
  {
    m_pFile->Close();
    delete m_pFile;
    m_pFile = NULL;
  }

  m_dllAvFormat.avformat_network_deinit();

  m_dllAvUtil.Unload();
  m_dllAvCodec.Unload();
  m_dllAvFormat.Unload();

  m_open            = false;
  m_filename        = "";
  m_bMatroska       = false;
  m_bAVI            = false;
  m_video_count     = 0;
  m_audio_count     = 0;
  m_subtitle_count  = 0;
  m_audio_index     = -1;
  m_video_index     = -1;
  m_subtitle_index  = -1;
  m_eof             = false;
  m_chapter_count   = 0;
  m_iCurrentPts     = DVD_NOPTS_VALUE;
  m_speed           = DVD_PLAYSPEED_NORMAL;

  ClearStreams();

  return true;
}

/*void OMXReader::FlushRead()
{
  m_iCurrentPts = DVD_NOPTS_VALUE;

  if(!m_pFormatContext)
    return;

  ff_read_frame_flush(m_pFormatContext);
}*/

bool OMXReader::SeekTime(int time, bool backwords, double *startpts)
{
  if(time < 0)
    time = 0;

  if(!m_pFormatContext)
    return false;

  if(m_pFile && !m_pFile->IoControl(IOCTRL_SEEK_POSSIBLE, NULL))
  {
    CLog::Log(LOGDEBUG, "%s - input stream reports it is not seekable", __FUNCTION__);
    return false;
  }

  Lock();

  //FlushRead();

  if(m_ioContext)
    m_ioContext->buf_ptr = m_ioContext->buf_end;

  int64_t seek_pts = (int64_t)time * (AV_TIME_BASE / 1000);
  if (m_pFormatContext->start_time != (int64_t)AV_NOPTS_VALUE)
    seek_pts += m_pFormatContext->start_time;

  RESET_TIMEOUT(1);
  int ret = m_dllAvFormat.av_seek_frame(m_pFormatContext, -1, seek_pts, backwords ? AVSEEK_FLAG_BACKWARD : 0);

  if(ret >= 0)
    UpdateCurrentPTS();

  // in this case the start time is requested time
  if(startpts)
    *startpts = DVD_MSEC_TO_TIME(time);

  // demuxer will return failure, if you seek to eof
  m_eof = false;
  if (ret < 0)
  {
    m_eof = true;
    ret = 0;
  }

  CLog::Log(LOGDEBUG, "OMXReader::SeekTime(%d) - seek ended up on time %d",time,(int)(m_iCurrentPts / DVD_TIME_BASE * 1000));

  UnLock();

  return (ret >= 0);
}

AVMediaType OMXReader::PacketType(OMXPacket *pkt)
{
  if(!m_pFormatContext || !pkt)
    return AVMEDIA_TYPE_UNKNOWN;

  return m_pFormatContext->streams[pkt->stream_index]->codec->codec_type;
}

OMXPacket *OMXReader::Read()
{
  AVPacket  pkt;
  OMXPacket *m_omx_pkt = NULL;
  int       result = -1;

  if(!m_pFormatContext || m_eof)
    return NULL;

  Lock();

  // assume we are not eof
  if(m_pFormatContext->pb)
    m_pFormatContext->pb->eof_reached = 0;

  // keep track if ffmpeg doesn't always set these
  pkt.size = 0;
  pkt.data = NULL;
  pkt.stream_index = MAX_OMX_STREAMS;

  RESET_TIMEOUT(1);
  result = m_dllAvFormat.av_read_frame(m_pFormatContext, &pkt);
  if (result < 0)
  {
    m_eof = true;
    //FlushRead();
    //m_dllAvCodec.av_free_packet(&pkt);
    UnLock();
    return NULL;
  }

  if (pkt.size < 0 || pkt.stream_index >= MAX_OMX_STREAMS || interrupt_cb(NULL))
  {
    // XXX, in some cases ffmpeg returns a negative packet size
    if(m_pFormatContext->pb && !m_pFormatContext->pb->eof_reached)
    {
      CLog::Log(LOGERROR, "OMXReader::Read no valid packet");
      //FlushRead();
    }

    m_dllAvCodec.av_free_packet(&pkt);

    m_eof = true;
    UnLock();
    return NULL;
  }

  AVStream *pStream = m_pFormatContext->streams[pkt.stream_index];

  /* only read packets for active streams */
  /*
  if(!IsActive(pkt.stream_index))
  {
    m_dllAvCodec.av_free_packet(&pkt);
    UnLock();
    return NULL;
  }
  */

  // lavf sometimes bugs out and gives 0 dts/pts instead of no dts/pts
  // since this could only happens on initial frame under normal
  // circomstances, let's assume it is wrong all the time
#if 0
  if(pkt.dts == 0)
    pkt.dts = AV_NOPTS_VALUE;
  if(pkt.pts == 0)
    pkt.pts = AV_NOPTS_VALUE;
#endif
  if(m_bMatroska && pStream->codec && pStream->codec->codec_type == AVMEDIA_TYPE_VIDEO)
  { // matroska can store different timestamps
    // for different formats, for native stored
    // stuff it is pts, but for ms compatibility
    // tracks, it is really dts. sadly ffmpeg
    // sets these two timestamps equal all the
    // time, so we select it here instead
    if(pStream->codec->codec_tag == 0)
      pkt.dts = AV_NOPTS_VALUE;
    else
      pkt.pts = AV_NOPTS_VALUE;
  }
  // we need to get duration slightly different for matroska embedded text subtitels
  if(m_bMatroska && pStream->codec->codec_id == AV_CODEC_ID_SUBRIP && pkt.convergence_duration != 0)
    pkt.duration = pkt.convergence_duration;

  if(m_bAVI && pStream->codec && pStream->codec->codec_type == AVMEDIA_TYPE_VIDEO)
  {
    // AVI's always have borked pts, specially if m_pFormatContext->flags includes
    // AVFMT_FLAG_GENPTS so always use dts
    pkt.pts = AV_NOPTS_VALUE;
  }

  m_omx_pkt = AllocPacket(pkt.size);
  /* oom error allocation av packet */
  if(!m_omx_pkt)
  {
    m_eof = true;
    m_dllAvCodec.av_free_packet(&pkt);
    UnLock();
    return NULL;
  }

  m_omx_pkt->codec_type = pStream->codec->codec_type;

  /* copy content into our own packet */
  m_omx_pkt->size = pkt.size;

  if (pkt.data)
    memcpy(m_omx_pkt->data, pkt.data, m_omx_pkt->size);

  m_omx_pkt->stream_index = pkt.stream_index;
  GetHints(pStream, &m_omx_pkt->hints);

  m_omx_pkt->dts = ConvertTimestamp(pkt.dts, pStream->time_base.den, pStream->time_base.num);
  m_omx_pkt->pts = ConvertTimestamp(pkt.pts, pStream->time_base.den, pStream->time_base.num);
  m_omx_pkt->duration = DVD_SEC_TO_TIME((double)pkt.duration * pStream->time_base.num / pStream->time_base.den);

  // used to guess streamlength
  if (m_omx_pkt->dts != DVD_NOPTS_VALUE && (m_omx_pkt->dts > m_iCurrentPts || m_iCurrentPts == DVD_NOPTS_VALUE))
    m_iCurrentPts = m_omx_pkt->dts;

  // check if stream has passed full duration, needed for live streams
  if(pkt.dts != (int64_t)AV_NOPTS_VALUE)
  {
    int64_t duration;
    duration = pkt.dts;
    if(pStream->start_time != (int64_t)AV_NOPTS_VALUE)
      duration -= pStream->start_time;

    if(duration > pStream->duration)
    {
      pStream->duration = duration;
      duration = m_dllAvUtil.av_rescale_rnd(pStream->duration, (int64_t)pStream->time_base.num * AV_TIME_BASE, 
                                            pStream->time_base.den, AV_ROUND_NEAR_INF);
      if ((m_pFormatContext->duration == (int64_t)AV_NOPTS_VALUE)
          ||  (m_pFormatContext->duration != (int64_t)AV_NOPTS_VALUE && duration > m_pFormatContext->duration))
        m_pFormatContext->duration = duration;
    }
  }

  m_dllAvCodec.av_free_packet(&pkt);

  UnLock();
  return m_omx_pkt;
}

bool OMXReader::GetStreams()
{
  if(!m_pFormatContext)
    return false;

  unsigned int    m_program         = UINT_MAX;

  ClearStreams();

  if (m_pFormatContext->nb_programs)
  {
    // look for first non empty stream and discard nonselected programs
    for (unsigned int i = 0; i < m_pFormatContext->nb_programs; i++)
    {
      if(m_program == UINT_MAX && m_pFormatContext->programs[i]->nb_stream_indexes > 0)
        m_program = i;

      if(i != m_program)
        m_pFormatContext->programs[i]->discard = AVDISCARD_ALL;
    }
      if(m_program != UINT_MAX)
      {
        // add streams from selected program
        for (unsigned int i = 0; i < m_pFormatContext->programs[m_program]->nb_stream_indexes; i++)
          AddStream(m_pFormatContext->programs[m_program]->stream_index[i]);
      }
    }

  // if there were no programs or they were all empty, add all streams
  if (m_program == UINT_MAX)
  {
    for (unsigned int i = 0; i < m_pFormatContext->nb_streams; i++)
      AddStream(i);
  }

  if(m_video_count)
    SetActiveStreamInternal(OMXSTREAM_VIDEO, 0);

  if(m_audio_count)
    SetActiveStreamInternal(OMXSTREAM_AUDIO, 0);

  if(m_subtitle_count)
    SetActiveStreamInternal(OMXSTREAM_SUBTITLE, 0);

  int i = 0;
  for(i = 0; i < MAX_OMX_CHAPTERS; i++)
  {
    m_chapters[i].name      = "";
    m_chapters[i].seekto_ms = 0;
    m_chapters[i].ts        = 0;
  }

  m_chapter_count = 0;

  if(m_video_index != -1)
  {
    //m_current_chapter = 0;
#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(52,14,0)
    m_chapter_count = (m_pFormatContext->nb_chapters > MAX_OMX_CHAPTERS) ? MAX_OMX_CHAPTERS : m_pFormatContext->nb_chapters;
    for(i = 0; i < m_chapter_count; i++)
    {
      if(i > MAX_OMX_CHAPTERS)
        break;

      AVChapter *chapter = m_pFormatContext->chapters[i];
      if(!chapter)
        continue;

      m_chapters[i].seekto_ms = ConvertTimestamp(chapter->start, chapter->time_base.den, chapter->time_base.num) / 1000;
      m_chapters[i].ts        = m_chapters[i].seekto_ms / 1000;

#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(52,83,0)
      AVDictionaryEntry *titleTag = m_dllAvUtil.av_dict_get(m_pFormatContext->chapters[i]->metadata,"title", NULL, 0);
      if (titleTag)
        m_chapters[i].name = titleTag->value;
#else
      if(m_pFormatContext->chapters[i]->title)
        m_chapters[i].name = m_pFormatContext->chapters[i]->title;
#endif
      printf("Chapter : \t%d \t%s \t%8.2f\n", i, m_chapters[i].name.c_str(), m_chapters[i].ts);
    }
  }
#endif

  return true;
}

void OMXReader::AddStream(int id)
{
  if(id > MAX_STREAMS || !m_pFormatContext)
    return;

  AVStream *pStream = m_pFormatContext->streams[id];

  // discard if it's a picture attachment (e.g. album art embedded in MP3 or AAC)
  if(pStream->codec->codec_type == AVMEDIA_TYPE_VIDEO &&
    (pStream->disposition & AV_DISPOSITION_ATTACHED_PIC))
    return;

  switch (pStream->codec->codec_type)
  {
    case AVMEDIA_TYPE_AUDIO:
      m_streams[id].stream      = pStream;
      m_streams[id].type        = OMXSTREAM_AUDIO;
      m_streams[id].index       = m_audio_count;
      m_streams[id].codec_name  = GetStreamCodecName(pStream);
      m_streams[id].id          = id;
      m_audio_count++;
      GetHints(pStream, &m_streams[id].hints);
      break;
    case AVMEDIA_TYPE_VIDEO:
      m_streams[id].stream      = pStream;
      m_streams[id].type        = OMXSTREAM_VIDEO;
      m_streams[id].index       = m_video_count;
      m_streams[id].codec_name  = GetStreamCodecName(pStream);
      m_streams[id].id          = id;
      m_video_count++;
      GetHints(pStream, &m_streams[id].hints);
      break;
    case AVMEDIA_TYPE_SUBTITLE:
      m_streams[id].stream      = pStream;
      m_streams[id].type        = OMXSTREAM_SUBTITLE;
      m_streams[id].index       = m_subtitle_count;
      m_streams[id].codec_name  = GetStreamCodecName(pStream);
      m_streams[id].id          = id;
      m_subtitle_count++;
      GetHints(pStream, &m_streams[id].hints);
      break;
    default:
      return;
  }

#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(52,83,0)
  AVDictionaryEntry *langTag = m_dllAvUtil.av_dict_get(pStream->metadata, "language", NULL, 0);
  if (langTag)
    strncpy(m_streams[id].language, langTag->value, 3);
#else
  strcpy( m_streams[id].language, pStream->language );
#endif

#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(52,83,0)
  AVDictionaryEntry *titleTag = m_dllAvUtil.av_dict_get(pStream->metadata,"title", NULL, 0);
  if (titleTag)
    m_streams[id].name = titleTag->value;
#else
  m_streams[id].name = pStream->title;
#endif

  if( pStream->codec->extradata && pStream->codec->extradata_size > 0 )
  {
    m_streams[id].extrasize = pStream->codec->extradata_size;
    m_streams[id].extradata = malloc(pStream->codec->extradata_size);
    memcpy(m_streams[id].extradata, pStream->codec->extradata, pStream->codec->extradata_size);
  }
}

bool OMXReader::SetActiveStreamInternal(OMXStreamType type, unsigned int index)
{
  bool ret = false;

  switch(type)
  {
    case OMXSTREAM_AUDIO:
      if((int)index > (m_audio_count - 1))
        index = (m_audio_count - 1);
      break;
    case OMXSTREAM_VIDEO:
      if((int)index > (m_video_count - 1))
        index = (m_video_count - 1);
      break;
    case OMXSTREAM_SUBTITLE:
      if((int)index > (m_subtitle_count - 1))
        index = (m_subtitle_count - 1);
      break;
    default:
      break;
  }

  for(int i = 0; i < MAX_STREAMS; i++)
  {
    if(m_streams[i].type == type &&  m_streams[i].index == index)
    {
      switch(m_streams[i].type)
      {
        case OMXSTREAM_AUDIO:
          m_audio_index = i;
          ret = true;
          break;
        case OMXSTREAM_VIDEO:
          m_video_index = i;
          ret = true;
          break;
        case OMXSTREAM_SUBTITLE:
          m_subtitle_index = i;
          ret = true;
          break;
        default:
          break;
      }
    }
  }

  if(!ret)
  {
    switch(type)
    {
      case OMXSTREAM_AUDIO:
        m_audio_index = -1;
        break;
      case OMXSTREAM_VIDEO:
        m_video_index = -1;
        break;
      case OMXSTREAM_SUBTITLE:
        m_subtitle_index = -1;
        break;
      default:
        break;
    }
  }

  return ret;
}

bool OMXReader::IsActive(int stream_index)
{
  if((m_audio_index != -1)    && m_streams[m_audio_index].id      == stream_index)
    return true;
  if((m_video_index != -1)    && m_streams[m_video_index].id      == stream_index)
    return true;
  if((m_subtitle_index != -1) && m_streams[m_subtitle_index].id   == stream_index)
    return true;

  return false;
}

bool OMXReader::IsActive(OMXStreamType type, int stream_index)
{
  if((m_audio_index != -1)    && m_streams[m_audio_index].id      == stream_index && m_streams[m_audio_index].type == type)
    return true;
  if((m_video_index != -1)    && m_streams[m_video_index].id      == stream_index && m_streams[m_video_index].type == type)
    return true;
  if((m_subtitle_index != -1) && m_streams[m_subtitle_index].id   == stream_index && m_streams[m_subtitle_index].type == type)
    return true;

  return false;
}

double OMXReader::SelectAspect(AVStream* st, bool& forced)
{
  // trust matroshka container
  if (m_bMatroska && st->sample_aspect_ratio.num != 0)
  {
    forced = true;
    return av_q2d(st->sample_aspect_ratio);
  }

  forced = false;
  /* if stream aspect is 1:1 or 0:0 use codec aspect */
  if((st->sample_aspect_ratio.den == 1 || st->sample_aspect_ratio.den == 0) &&
     (st->sample_aspect_ratio.num == 1 || st->sample_aspect_ratio.num == 0) &&
      st->codec->sample_aspect_ratio.num != 0)
  {
    return av_q2d(st->codec->sample_aspect_ratio);
  }

  forced = true;
  if(st->sample_aspect_ratio.num != 0)
    return av_q2d(st->sample_aspect_ratio);

  return 0.0;
}

bool OMXReader::GetHints(AVStream *stream, COMXStreamInfo *hints)
{
  if(!hints || !stream)
    return false;

  hints->codec         = stream->codec->codec_id;
  hints->extradata     = stream->codec->extradata;
  hints->extrasize     = stream->codec->extradata_size;
  hints->channels      = stream->codec->channels;
  hints->samplerate    = stream->codec->sample_rate;
  hints->blockalign    = stream->codec->block_align;
  hints->bitrate       = stream->codec->bit_rate;
  hints->bitspersample = stream->codec->bits_per_coded_sample;
  if(hints->bitspersample == 0)
    hints->bitspersample = 16;

  hints->width         = stream->codec->width;
  hints->height        = stream->codec->height;
  hints->profile       = stream->codec->profile;
  hints->orientation   = 0;

  if(stream->codec->codec_type == AVMEDIA_TYPE_VIDEO)
  {
    hints->fpsrate       = stream->r_frame_rate.num;
    hints->fpsscale      = stream->r_frame_rate.den;

    if(m_bMatroska && stream->avg_frame_rate.den && stream->avg_frame_rate.num)
    {
      hints->fpsrate      = stream->avg_frame_rate.num;
      hints->fpsscale     = stream->avg_frame_rate.den;
    }
    else if(stream->r_frame_rate.num && stream->r_frame_rate.den)
    {
      hints->fpsrate      = stream->r_frame_rate.num;
      hints->fpsscale     = stream->r_frame_rate.den;
    }
    else
    {
      hints->fpsscale     = 0;
      hints->fpsrate      = 0;
    }

    hints->aspect = SelectAspect(stream, hints->forced_aspect) * stream->codec->width / stream->codec->height;

    if (m_bAVI && stream->codec->codec_id == AV_CODEC_ID_H264)
      hints->ptsinvalid = true;
    AVDictionaryEntry *rtag = m_dllAvUtil.av_dict_get(stream->metadata, "rotate", NULL, 0);
    if (rtag)
      hints->orientation = atoi(rtag->value);
    m_aspect = hints->aspect;
    m_width = hints->width;
    m_height = hints->height;
  }

  return true;
}

bool OMXReader::GetHints(OMXStreamType type, unsigned int index, COMXStreamInfo &hints)
{
  for(unsigned int i = 0; i < MAX_STREAMS; i++)
  {
    if(m_streams[i].type == type && m_streams[i].index == i)
    {
      hints = m_streams[i].hints;
      return true;
    }
  }

  return false;
}

bool OMXReader::GetHints(OMXStreamType type, COMXStreamInfo &hints)
{
  bool ret = false;

  switch (type)
  {
    case OMXSTREAM_AUDIO:
      if(m_audio_index != -1)
      {
        ret = true;
        hints = m_streams[m_audio_index].hints;
      }
      break;
    case OMXSTREAM_VIDEO:
      if(m_video_index != -1)
      {
        ret = true;
        hints = m_streams[m_video_index].hints;
      }
      break;
    case OMXSTREAM_SUBTITLE:
      if(m_subtitle_index != -1)
      {
        ret = true;
        hints = m_streams[m_subtitle_index].hints;
      }
      break;
    default:
      break;
  }

  return ret;
}

bool OMXReader::IsEof()
{
  return m_eof;
}

void OMXReader::FreePacket(OMXPacket *pkt)
{
  if(pkt)
  {
    if(pkt->data)
      free(pkt->data);
    free(pkt);
  }
}

OMXPacket *OMXReader::AllocPacket(int size)
{
  OMXPacket *pkt = (OMXPacket *)malloc(sizeof(OMXPacket));
  if(pkt)
  {
    memset(pkt, 0, sizeof(OMXPacket));

    pkt->data = (uint8_t*) malloc(size + FF_INPUT_BUFFER_PADDING_SIZE);
    if(!pkt->data)
    {
      free(pkt);
      pkt = NULL;
    }
    else
    {
      memset(pkt->data + size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
      pkt->size = size;
      pkt->dts  = DVD_NOPTS_VALUE;
      pkt->pts  = DVD_NOPTS_VALUE;
      pkt->now  = DVD_NOPTS_VALUE;
      pkt->duration = DVD_NOPTS_VALUE;
    }
  }
  return pkt;
}

bool OMXReader::SetActiveStream(OMXStreamType type, unsigned int index)
{
  bool ret = false;
  Lock();
  ret = SetActiveStreamInternal(type, index);
  UnLock();
  return ret;
}

bool OMXReader::SeekChapter(int chapter, double* startpts)
{
  if(chapter < 1)
    chapter = 1;

  if(m_pFormatContext == NULL)
    return false;

#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(52,14,0)
  if(chapter < 1 || chapter > (int)m_pFormatContext->nb_chapters)
    return false;

  AVChapter *ch = m_pFormatContext->chapters[chapter-1];
  double dts = ConvertTimestamp(ch->start, ch->time_base.den, ch->time_base.num);
  return SeekTime(DVD_TIME_TO_MSEC(dts), 0, startpts);
#else
  return false;
#endif
}

double OMXReader::ConvertTimestamp(int64_t pts, int den, int num)
{
  if(m_pFormatContext == NULL)
    return DVD_NOPTS_VALUE;

  if (pts == (int64_t)AV_NOPTS_VALUE)
    return DVD_NOPTS_VALUE;

  // do calculations in floats as they can easily overflow otherwise
  // we don't care for having a completly exact timestamp anyway
  double timestamp = (double)pts * num  / den;
  double starttime = 0.0f;

  if (m_pFormatContext->start_time != (int64_t)AV_NOPTS_VALUE)
    starttime = (double)m_pFormatContext->start_time / AV_TIME_BASE;

  if(timestamp > starttime)
    timestamp -= starttime;
  else if( timestamp + 0.1f > starttime )
    timestamp = 0;

  return timestamp*DVD_TIME_BASE;
}

int OMXReader::GetChapter()
{
  if(m_pFormatContext == NULL
  || m_iCurrentPts == DVD_NOPTS_VALUE)
    return 0;

#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(52,14,0)
  for(unsigned i = 0; i < m_pFormatContext->nb_chapters; i++)
  {
    AVChapter *chapter = m_pFormatContext->chapters[i];
    if(m_iCurrentPts >= ConvertTimestamp(chapter->start, chapter->time_base.den, chapter->time_base.num)
      && m_iCurrentPts <  ConvertTimestamp(chapter->end,   chapter->time_base.den, chapter->time_base.num))
      return i + 1;
  }
#endif
  return 0;
}

void OMXReader::GetChapterName(std::string& strChapterName)
{
  strChapterName = "";
#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(52,14,0)
  int chapterIdx = GetChapter();
  if(chapterIdx <= 0)
    return;
#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(52,83,0)
  // API added on: 2010-10-15
  // (Note that while the function was available earlier, the generic
  // metadata tags were not populated by default)
  AVDictionaryEntry *titleTag = m_dllAvUtil.av_dict_get(m_pFormatContext->chapters[chapterIdx-1]->metadata,
                                                              "title", NULL, 0);
  if (titleTag)
    strChapterName = titleTag->value;
#else
  if (m_pFormatContext->chapters[chapterIdx-1]->title)
    strChapterName = m_pFormatContext->chapters[chapterIdx-1]->title;
#endif
#endif
}

void OMXReader::UpdateCurrentPTS()
{
  m_iCurrentPts = DVD_NOPTS_VALUE;
  for(unsigned int i = 0; i < m_pFormatContext->nb_streams; i++)
  {
    AVStream *stream = m_pFormatContext->streams[i];
    if(stream && stream->cur_dts != (int64_t)AV_NOPTS_VALUE)
    {
      double ts = ConvertTimestamp(stream->cur_dts, stream->time_base.den, stream->time_base.num);
      if(m_iCurrentPts == DVD_NOPTS_VALUE || m_iCurrentPts > ts )
        m_iCurrentPts = ts;
    }
  }
}

void OMXReader::SetSpeed(int iSpeed)
{
  if(!m_pFormatContext)
    return;

  if(m_speed != DVD_PLAYSPEED_PAUSE && iSpeed == DVD_PLAYSPEED_PAUSE)
  {
    m_dllAvFormat.av_read_pause(m_pFormatContext);
  }
  else if(m_speed == DVD_PLAYSPEED_PAUSE && iSpeed != DVD_PLAYSPEED_PAUSE)
  {
    m_dllAvFormat.av_read_play(m_pFormatContext);
  }
  m_speed = iSpeed;

  AVDiscard discard = AVDISCARD_NONE;
  if(m_speed > 4*DVD_PLAYSPEED_NORMAL)
    discard = AVDISCARD_NONKEY;
  else if(m_speed > 2*DVD_PLAYSPEED_NORMAL)
    discard = AVDISCARD_BIDIR;
  else if(m_speed < DVD_PLAYSPEED_PAUSE)
    discard = AVDISCARD_NONKEY;

  for(unsigned int i = 0; i < m_pFormatContext->nb_streams; i++)
  {
    if(m_pFormatContext->streams[i])
    {
      if(m_pFormatContext->streams[i]->discard != AVDISCARD_ALL)
        m_pFormatContext->streams[i]->discard = discard;
    }
  }
}

int OMXReader::GetStreamLength()
{
  if (!m_pFormatContext)
    return 0;

  return (int)(m_pFormatContext->duration / (AV_TIME_BASE / 1000));
}

double OMXReader::NormalizeFrameduration(double frameduration)
{
  //if the duration is within 20 microseconds of a common duration, use that
  const double durations[] = {DVD_TIME_BASE * 1.001 / 24.0, DVD_TIME_BASE / 24.0, DVD_TIME_BASE / 25.0,
                              DVD_TIME_BASE * 1.001 / 30.0, DVD_TIME_BASE / 30.0, DVD_TIME_BASE / 50.0,
                              DVD_TIME_BASE * 1.001 / 60.0, DVD_TIME_BASE / 60.0};

  double lowestdiff = DVD_TIME_BASE;
  int    selected   = -1;
  for (size_t i = 0; i < sizeof(durations) / sizeof(durations[0]); i++)
  {
    double diff = fabs(frameduration - durations[i]);
    if (diff < DVD_MSEC_TO_TIME(0.02) && diff < lowestdiff)
    {
      selected = i;
      lowestdiff = diff;
    }
  }

  if (selected != -1)
    return durations[selected];
  else
    return frameduration;
}

std::string OMXReader::GetStreamCodecName(AVStream *stream)
{
  std::string strStreamName = "";

  if(!stream)
    return strStreamName;

  unsigned int in = stream->codec->codec_tag;
  // FourCC codes are only valid on video streams, audio codecs in AVI/WAV
  // are 2 bytes and audio codecs in transport streams have subtle variation
  // e.g AC-3 instead of ac3
  if (stream->codec->codec_type == AVMEDIA_TYPE_VIDEO && in != 0)
  {
    char fourcc[5];
    memcpy(fourcc, &in, 4);
    fourcc[4] = 0;
    // fourccs have to be 4 characters
    if (strlen(fourcc) == 4)
    {
      strStreamName = fourcc;
      return strStreamName;
    }
  }

#ifdef FF_PROFILE_DTS_HD_MA
  /* use profile to determine the DTS type */
  if (stream->codec->codec_id == AV_CODEC_ID_DTS)
  {
    if (stream->codec->profile == FF_PROFILE_DTS_HD_MA)
      strStreamName = "dtshd_ma";
    else if (stream->codec->profile == FF_PROFILE_DTS_HD_HRA)
      strStreamName = "dtshd_hra";
    else
      strStreamName = "dca";
    return strStreamName;
  }
#endif

  AVCodec *codec = m_dllAvCodec.avcodec_find_decoder(stream->codec->codec_id);

  if (codec)
    strStreamName = codec->name;

  return strStreamName;
}

std::string OMXReader::GetCodecName(OMXStreamType type)
{
  std::string strStreamName;

  Lock();
  switch (type)
  {
    case OMXSTREAM_AUDIO:
      if(m_audio_index != -1)
        strStreamName = m_streams[m_audio_index].codec_name;
      break;
    case OMXSTREAM_VIDEO:
      if(m_video_index != -1)
        strStreamName = m_streams[m_video_index].codec_name;
      break;
    case OMXSTREAM_SUBTITLE:
      if(m_subtitle_index != -1)
        strStreamName = m_streams[m_subtitle_index].codec_name;
      break;
    default:
      break;
  }
  UnLock();

  return strStreamName;
}

std::string OMXReader::GetCodecName(OMXStreamType type, unsigned int index)
{
  std::string strStreamName = "";

  for(int i = 0; i < MAX_STREAMS; i++)
  {
    if(m_streams[i].type == type &&  m_streams[i].index == index)
    {
      strStreamName = m_streams[i].codec_name;
      break;
    }
  }

  return strStreamName;
}

std::string OMXReader::GetStreamLanguage(OMXStreamType type, unsigned int index)
{
  std::string language = "";

  for(int i = 0; i < MAX_STREAMS; i++)
  {
    if(m_streams[i].type == type &&  m_streams[i].index == index)
    {
      language = m_streams[i].language;
      break;
    }
  }

  return language;
}

std::string OMXReader::GetStreamName(OMXStreamType type, unsigned int index)
{
  std::string name = "";

  for(int i = 0; i < MAX_STREAMS; i++)
  {
    if(m_streams[i].type == type &&  m_streams[i].index == index)
    {
      name = m_streams[i].name;
      break;
    }
  }

  return name;
}

std::string OMXReader::GetStreamType(OMXStreamType type, unsigned int index)
{
  std::string strInfo;
  char sInfo[64];

  for(int i = 0; i < MAX_STREAMS; i++)
  {
    if(m_streams[i].type == type &&  m_streams[i].index == index)
    {
      if (m_streams[i].hints.codec == AV_CODEC_ID_AC3) strcpy(sInfo, "AC3 ");
      else if (m_streams[i].hints.codec == AV_CODEC_ID_DTS)
      {
#ifdef FF_PROFILE_DTS_HD_MA
        if (m_streams[i].hints.profile == FF_PROFILE_DTS_HD_MA)
          strcpy(sInfo, "DTS-HD MA ");
        else if (m_streams[i].hints.profile == FF_PROFILE_DTS_HD_HRA)
          strcpy(sInfo, "DTS-HD HRA ");
        else
#endif
          strcpy(sInfo, "DTS ");
      }
      else if (m_streams[i].hints.codec == AV_CODEC_ID_MP2) strcpy(sInfo, "MP2 ");
      else strcpy(sInfo, "");

      if (m_streams[i].hints.channels == 1) strcat(sInfo, "Mono");
        else if (m_streams[i].hints.channels == 2) strcat(sInfo, "Stereo");
      else if (m_streams[i].hints.channels == 6) strcat(sInfo, "5.1");
      else if (m_streams[i].hints.channels != 0)
      {
        char temp[32];
        sprintf(temp, " %d %s", m_streams[i].hints.channels, "Channels");
        strcat(sInfo, temp);
      }
      break;
    }
  }

  strInfo = sInfo;
  return strInfo;
}

bool OMXReader::CanSeek()
{
  if(m_ioContext)
    return m_ioContext->seekable;

  if(!m_pFormatContext || !m_pFormatContext->pb)
    return false;

  if(m_pFormatContext->pb->seekable == AVIO_SEEKABLE_NORMAL)
    return true;

  return false;
}
