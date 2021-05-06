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

#include "OMXAudioCodecOMX.h"
#ifdef TARGET_LINUX
#include "XMemUtils.h"
#endif
#include "utils/log.h"

#include "utils/PCMRemap.h"

// the size of the audio_render output port buffers
#define AUDIO_DECODE_OUTPUT_BUFFER (32*1024)
static const char rounded_up_channels_shift[] = {0,0,1,2,2,3,3,3,3};

COMXAudioCodecOMX::COMXAudioCodecOMX()
{
  m_pBufferOutput = NULL;
  m_iBufferOutputAlloced = 0;
  m_iBufferOutputUsed = 0;

  m_pCodecContext = NULL;
  m_pConvert = NULL;
  m_bOpenedCodec = false;

  m_channels = 0;
  m_pFrame1 = NULL;
  m_frameSize = 0;
  m_bGotFrame = false;
  m_bNoConcatenate = false;
  m_iSampleFormat = AV_SAMPLE_FMT_NONE;
  m_desiredSampleFormat = AV_SAMPLE_FMT_NONE;
}

COMXAudioCodecOMX::~COMXAudioCodecOMX()
{
  m_dllAvUtil.av_free(m_pBufferOutput);
  m_pBufferOutput = NULL;
  m_iBufferOutputAlloced = 0;
  m_iBufferOutputUsed = 0;
  Dispose();
}

bool COMXAudioCodecOMX::Open(COMXStreamInfo &hints, enum PCMLayout layout)
{
  AVCodec* pCodec;
  m_bOpenedCodec = false;

  if (!m_dllAvUtil.Load() || !m_dllAvCodec.Load() || !m_dllSwResample.Load())
    return false;

  m_dllAvCodec.avcodec_register_all();

  if( hints.codec != AV_CODEC_ID_AAC ){
      pCodec = m_dllAvCodec.avcodec_find_decoder(hints.codec);
  } else {
      pCodec = m_dllAvCodec.avcodec_find_decoder_by_name("libfdk_aac");
      if (!pCodec){
          pCodec = m_dllAvCodec.avcodec_find_decoder(hints.codec);
      }
  }
  printf( "[Audio codec is %s]\n", pCodec->long_name );

  if (!pCodec)
  {
    CLog::Log(LOGDEBUG,"COMXAudioCodecOMX::Open() Unable to find codec %d", hints.codec);
    return false;
  }

  m_bFirstFrame = true;
  m_pCodecContext = m_dllAvCodec.avcodec_alloc_context3(pCodec);
  m_pCodecContext->debug_mv = 0;
  m_pCodecContext->debug = 0;
  m_pCodecContext->workaround_bugs = 1;

  if (pCodec->capabilities & AV_CODEC_CAP_TRUNCATED)
    m_pCodecContext->flags |= AV_CODEC_FLAG_TRUNCATED;

  m_channels = 0;
  m_pCodecContext->channels = hints.channels;
  m_pCodecContext->sample_rate = hints.samplerate;
  m_pCodecContext->block_align = hints.blockalign;
  m_pCodecContext->bit_rate = hints.bitrate;
  m_pCodecContext->bits_per_coded_sample = hints.bitspersample;
  if (hints.codec == AV_CODEC_ID_TRUEHD)
  {
    if (layout == PCM_LAYOUT_2_0)
    {
      m_pCodecContext->request_channel_layout = AV_CH_LAYOUT_STEREO;
      m_pCodecContext->channels = 2;
      m_pCodecContext->channel_layout = m_dllAvUtil.av_get_default_channel_layout(m_pCodecContext->channels);
    }
    else if (layout <= PCM_LAYOUT_5_1)
    {
      m_pCodecContext->request_channel_layout = AV_CH_LAYOUT_5POINT1;
      m_pCodecContext->channels = 6;
      m_pCodecContext->channel_layout = m_dllAvUtil.av_get_default_channel_layout(m_pCodecContext->channels);
    }
  }
  if (m_pCodecContext->request_channel_layout)
    CLog::Log(LOGNOTICE,"COMXAudioCodecOMX::Open() Requesting channel layout of %x", (unsigned)m_pCodecContext->request_channel_layout);

  if(m_pCodecContext->bits_per_coded_sample == 0)
    m_pCodecContext->bits_per_coded_sample = 16;

  if( hints.extradata && hints.extrasize > 0 )
  {
    m_pCodecContext->extradata_size = hints.extrasize;
    m_pCodecContext->extradata = (uint8_t*)m_dllAvUtil.av_mallocz(hints.extrasize + AV_INPUT_BUFFER_PADDING_SIZE);
    memcpy(m_pCodecContext->extradata, hints.extradata, hints.extrasize);
  }

  if (m_dllAvCodec.avcodec_open2(m_pCodecContext, pCodec, NULL) < 0)
  {
    CLog::Log(LOGDEBUG,"COMXAudioCodecOMX::Open() Unable to open codec");
    Dispose();
    return false;
  }

  m_pFrame1 = m_dllAvCodec.av_frame_alloc();
  m_bOpenedCodec = true;
  m_iSampleFormat = AV_SAMPLE_FMT_NONE;
  m_desiredSampleFormat = m_pCodecContext->sample_fmt == AV_SAMPLE_FMT_S16 ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_FLTP;
  return true;
}

void COMXAudioCodecOMX::Dispose()
{
  if (m_pFrame1) m_dllAvUtil.av_free(m_pFrame1);
    m_pFrame1 = NULL;

  if (m_pConvert)
    m_dllSwResample.swr_free(&m_pConvert);

  if (m_pCodecContext)
  {
    if (m_pCodecContext->extradata) m_dllAvUtil.av_free(m_pCodecContext->extradata);
    m_pCodecContext->extradata = NULL;
    if (m_bOpenedCodec) m_dllAvCodec.avcodec_close(m_pCodecContext);
    m_bOpenedCodec = false;
    m_dllAvUtil.av_free(m_pCodecContext);
    m_pCodecContext = NULL;
  }

  m_dllAvCodec.Unload();
  m_dllAvUtil.Unload();
  m_dllSwResample.Unload();

  m_bGotFrame = false;
}

int COMXAudioCodecOMX::Decode(BYTE* pData, int iSize, double dts, double pts)
{
  int iBytesUsed, got_frame;
  if (!m_pCodecContext) return -1;

  AVPacket avpkt;
  if (!m_iBufferOutputUsed)
  {
    m_dts = dts;
    m_pts = pts;
  }
  if (m_bGotFrame)
    return 0;

  m_dllAvCodec.av_init_packet(&avpkt);
  avpkt.data = pData;
  avpkt.size = iSize;
  iBytesUsed = m_dllAvCodec.avcodec_decode_audio4( m_pCodecContext
                                                 , m_pFrame1
                                                 , &got_frame
                                                 , &avpkt);
  if (iBytesUsed < 0 || !got_frame)
  {
    return iBytesUsed;
  }
  /* some codecs will attempt to consume more data than what we gave */
  if (iBytesUsed > iSize)
  {
    CLog::Log(LOGWARNING, "COMXAudioCodecOMX::Decode - decoder attempted to consume more data than given");
    iBytesUsed = iSize;
  }
  m_bGotFrame = true;

  if (m_bFirstFrame)
  {
    CLog::Log(LOGDEBUG, "COMXAudioCodecOMX::Decode(%p,%d) format=%d(%d) chan=%d samples=%d size=%d data=%p,%p,%p,%p,%p,%p,%p,%p",
             pData, iSize, m_pCodecContext->sample_fmt, m_desiredSampleFormat, m_pCodecContext->channels, m_pFrame1->nb_samples,
             m_pFrame1->linesize[0],
             m_pFrame1->data[0], m_pFrame1->data[1], m_pFrame1->data[2], m_pFrame1->data[3], m_pFrame1->data[4], m_pFrame1->data[5], m_pFrame1->data[6], m_pFrame1->data[7]
             );
  }
  return iBytesUsed;
}

int COMXAudioCodecOMX::GetData(BYTE** dst, double &dts, double &pts)
{
  if (!m_bGotFrame)
    return 0;
  int inLineSize, outLineSize;
  /* input audio is aligned */
  int inputSize = m_dllAvUtil.av_samples_get_buffer_size(&inLineSize, m_pCodecContext->channels, m_pFrame1->nb_samples, m_pCodecContext->sample_fmt, 0);
  /* output audio will be packed */
  int outputSize = m_dllAvUtil.av_samples_get_buffer_size(&outLineSize, m_pCodecContext->channels, m_pFrame1->nb_samples, m_desiredSampleFormat, 1);

  if (!m_bNoConcatenate && m_iBufferOutputUsed && (int)m_frameSize != outputSize)
  {
    CLog::Log(LOGERROR, "COMXAudioCodecOMX::GetData Unexpected change of size (%d->%d)", m_frameSize, outputSize);
    m_bNoConcatenate = true;
  }

  // if this buffer won't fit then flush out what we have
  int desired_size = AUDIO_DECODE_OUTPUT_BUFFER * (m_pCodecContext->channels * GetBitsPerSample()) >> (rounded_up_channels_shift[m_pCodecContext->channels] + 4);
  if (m_iBufferOutputUsed && (m_iBufferOutputUsed + outputSize > desired_size || m_bNoConcatenate))
  {
     int ret = m_iBufferOutputUsed;
     m_iBufferOutputUsed = 0;
     m_bNoConcatenate = false;
     dts = m_dts;
     pts = m_pts;
     *dst = m_pBufferOutput;
     return ret;
  }
  m_frameSize = outputSize;

  if (m_iBufferOutputAlloced < m_iBufferOutputUsed + outputSize)
  {
     m_pBufferOutput = (BYTE*)m_dllAvUtil.av_realloc(m_pBufferOutput, m_iBufferOutputUsed + outputSize + AV_INPUT_BUFFER_PADDING_SIZE);
     m_iBufferOutputAlloced = m_iBufferOutputUsed + outputSize;
  }

  /* need to convert format */
  if(m_pCodecContext->sample_fmt != m_desiredSampleFormat)
  {
    if(m_pConvert && (m_pCodecContext->sample_fmt != m_iSampleFormat || m_channels != m_pCodecContext->channels))
    {
      m_dllSwResample.swr_free(&m_pConvert);
      m_channels = m_pCodecContext->channels;
    }

    if(!m_pConvert)
    {
      m_iSampleFormat = m_pCodecContext->sample_fmt;
      m_pConvert = m_dllSwResample.swr_alloc_set_opts(NULL,
                      m_dllAvUtil.av_get_default_channel_layout(m_pCodecContext->channels), 
                      m_desiredSampleFormat, m_pCodecContext->sample_rate,
                      m_dllAvUtil.av_get_default_channel_layout(m_pCodecContext->channels), 
                      m_pCodecContext->sample_fmt, m_pCodecContext->sample_rate,
                      0, NULL);

      if(!m_pConvert || m_dllSwResample.swr_init(m_pConvert) < 0)
      {
        CLog::Log(LOGERROR, "COMXAudioCodecOMX::Decode - Unable to initialise convert format %d to %d", m_pCodecContext->sample_fmt, m_desiredSampleFormat);
        return 0;
      }
    }

    /* use unaligned flag to keep output packed */
    uint8_t *out_planes[m_pCodecContext->channels];
    if(m_dllAvUtil.av_samples_fill_arrays(out_planes, NULL, m_pBufferOutput + m_iBufferOutputUsed, m_pCodecContext->channels, m_pFrame1->nb_samples, m_desiredSampleFormat, 1) < 0 ||
       m_dllSwResample.swr_convert(m_pConvert, out_planes, m_pFrame1->nb_samples, (const uint8_t **)m_pFrame1->data, m_pFrame1->nb_samples) < 0)
    {
      CLog::Log(LOGERROR, "COMXAudioCodecOMX::Decode - Unable to convert format %d to %d", (int)m_pCodecContext->sample_fmt, m_desiredSampleFormat);
      outputSize = 0;
    }
  }
  else
  {
    /* copy to a contiguous buffer */
    uint8_t *out_planes[m_pCodecContext->channels];
    if (m_dllAvUtil.av_samples_fill_arrays(out_planes, NULL, m_pBufferOutput + m_iBufferOutputUsed, m_pCodecContext->channels, m_pFrame1->nb_samples, m_desiredSampleFormat, 1) < 0 ||
      m_dllAvUtil.av_samples_copy(out_planes, m_pFrame1->data, 0, 0, m_pFrame1->nb_samples, m_pCodecContext->channels, m_desiredSampleFormat) < 0 )
    {
      outputSize = 0;
    }
  }
  m_bGotFrame = false;

  if (m_bFirstFrame)
  {
    CLog::Log(LOGDEBUG, "COMXAudioCodecOMX::GetData size=%d/%d line=%d/%d buf=%p, desired=%d", inputSize, outputSize, inLineSize, outLineSize, m_pBufferOutput, desired_size);
    m_bFirstFrame = false;
  }
  m_iBufferOutputUsed += outputSize;
  return 0;
}

void COMXAudioCodecOMX::Reset()
{
  if (m_pCodecContext) m_dllAvCodec.avcodec_flush_buffers(m_pCodecContext);
  m_bGotFrame = false;
  m_iBufferOutputUsed = 0;
}

int COMXAudioCodecOMX::GetChannels()
{
  if (!m_pCodecContext)
    return 0;
  return m_pCodecContext->channels;
}

int COMXAudioCodecOMX::GetSampleRate()
{
  if (!m_pCodecContext)
    return 0;
  return m_pCodecContext->sample_rate;
}

int COMXAudioCodecOMX::GetBitsPerSample()
{
  if (!m_pCodecContext)
    return 0;
  return m_pCodecContext->sample_fmt == AV_SAMPLE_FMT_S16 ? 16 : 32;
}

int COMXAudioCodecOMX::GetBitRate()
{
  if (!m_pCodecContext)
    return 0;
  return m_pCodecContext->bit_rate;
}

static unsigned count_bits(int64_t value)
{
  unsigned bits = 0;
  for(;value;++bits)
    value &= value - 1;
  return bits;
}

uint64_t COMXAudioCodecOMX::GetChannelMap()
{
  uint64_t layout;
  int bits = count_bits(m_pCodecContext->channel_layout);
  if (bits == m_pCodecContext->channels)
    layout = m_pCodecContext->channel_layout;
  else
  {
    CLog::Log(LOGINFO, "COMXAudioCodecOMX::GetChannelMap - FFmpeg reported %d channels, but the layout contains %d ignoring", m_pCodecContext->channels, bits);
    layout = m_dllAvUtil.av_get_default_channel_layout(m_pCodecContext->channels);
  }


  return layout;
}
