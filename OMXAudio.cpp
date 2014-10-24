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

#if (defined HAVE_CONFIG_H) && (!defined WIN32)
  #include "config.h"
#elif defined(_WIN32)
#include "system.h"
#endif

#include "OMXAudio.h"
#include "utils/log.h"

#define CLASSNAME "COMXAudio"

#include "linux/XMemUtils.h"

#ifndef VOLUME_MINIMUM
#define VOLUME_MINIMUM 0
#endif

#include <algorithm>

using namespace std;

// the size of the audio_render output port buffers
#define AUDIO_DECODE_OUTPUT_BUFFER (32*1024)
static const char rounded_up_channels_shift[] = {0,0,1,2,2,3,3,3,3};

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
//***********************************************************************************************
COMXAudio::COMXAudio() :
  m_Initialized     (false  ),
  m_CurrentVolume   (0      ),
  m_Mute            (false  ),
  m_drc             (0      ),
  m_BytesPerSec     (0      ),
  m_InputBytesPerSec(0      ),
  m_BufferLen       (0      ),
  m_ChunkLen        (0      ),
  m_InputChannels   (0      ),
  m_OutputChannels  (0      ),
  m_BitsPerSample   (0      ),
  m_maxLevel        (0.0f   ),
  m_amplification   (1.0f   ),
  m_attenuation     (1.0f   ),
  m_submitted       (0.0f   ),
  m_omx_clock       (NULL   ),
  m_av_clock        (NULL   ),
  m_settings_changed(false  ),
  m_setStartTime    (false  ),
  m_eEncoding       (OMX_AUDIO_CodingPCM),
  m_last_pts        (DVD_NOPTS_VALUE),
  m_submitted_eos   (false  ),
  m_failed_eos      (false  )
{
}

COMXAudio::~COMXAudio()
{
  Deinitialize();
}


bool COMXAudio::PortSettingsChanged()
{
  CSingleLock lock (m_critSection);
  OMX_ERRORTYPE omx_err   = OMX_ErrorNone;

  if (m_settings_changed)
  {
    /* setup mixer input */
    OMX_INIT_STRUCTURE(m_pcm_output);
    m_pcm_output.nPortIndex = m_omx_decoder.GetOutputPort();
    omx_err = m_omx_decoder.GetParameter(OMX_IndexParamAudioPcm, &m_pcm_output);
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "%s::%s - error m_omx_decoder GetParameter omx_err(0x%08x)", CLASSNAME, __func__, omx_err);
    }

    //// decoder output -> mixer input
    memcpy(m_pcm_output.eChannelMapping, m_input_channels, sizeof(m_input_channels));
    // round up to power of 2
    m_pcm_output.nChannels = m_InputChannels > 4 ? 8 : m_InputChannels > 2 ? 4 : m_InputChannels;
    /* limit samplerate (through resampling) if requested */
    m_pcm_output.nSamplingRate = std::min(std::max((int)m_pcm_output.nSamplingRate, 8000), 192000);

    m_omx_decoder.DisablePort(m_omx_decoder.GetOutputPort(), true);
    m_omx_mixer.DisablePort(m_omx_mixer.GetInputPort(), false);

    m_pcm_output.nPortIndex = m_omx_mixer.GetInputPort();
    omx_err = m_omx_mixer.SetParameter(OMX_IndexParamAudioPcm, &m_pcm_output);
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "%s::%s - error m_omx_mixer(input) SetParameter omx_err(0x%08x)", CLASSNAME, __func__, omx_err);
    }
    m_omx_decoder.EnablePort(m_omx_decoder.GetOutputPort(), false);
    m_omx_mixer.EnablePort(m_omx_mixer.GetInputPort(), false);

    return true;
  }

  if(!m_config.passthrough)
  {
    if(!m_omx_mixer.Initialize("OMX.broadcom.audio_mixer", OMX_IndexParamAudioInit))
      return false;
  }
  if(m_config.device == "omx:both")
  {
    if(!m_omx_splitter.Initialize("OMX.broadcom.audio_splitter", OMX_IndexParamAudioInit))
      return false;
  }
  if (m_config.device == "omx:both" || m_config.device == "omx:local")
  {
    if(!m_omx_render_analog.Initialize("OMX.broadcom.audio_render", OMX_IndexParamAudioInit))
      return false;
  }
  if (m_config.device == "omx:both" || m_config.device == "omx:hdmi")
  {
    if(!m_omx_render_hdmi.Initialize("OMX.broadcom.audio_render", OMX_IndexParamAudioInit))
      return false;
  }

  UpdateAttenuation();

  if( m_omx_mixer.IsInitialized() )
  {
    /* setup mixer output */
    OMX_INIT_STRUCTURE(m_pcm_output);
    m_pcm_output.nPortIndex = m_omx_decoder.GetOutputPort();
    omx_err = m_omx_decoder.GetParameter(OMX_IndexParamAudioPcm, &m_pcm_output);
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "%s::%s - error m_omx_decoder GetParameter omx_err(0x%08x)", CLASSNAME, __func__, omx_err);
      return false;
    }

    memcpy(m_pcm_output.eChannelMapping, m_output_channels, sizeof(m_output_channels));
    // round up to power of 2
    m_pcm_output.nChannels = m_OutputChannels > 4 ? 8 : m_OutputChannels > 2 ? 4 : m_OutputChannels;
    /* limit samplerate (through resampling) if requested */
    m_pcm_output.nSamplingRate = std::min(std::max((int)m_pcm_output.nSamplingRate, 8000), 192000);

    m_pcm_output.nPortIndex = m_omx_mixer.GetOutputPort();
    omx_err = m_omx_mixer.SetParameter(OMX_IndexParamAudioPcm, &m_pcm_output);
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "%s::%s - error m_omx_mixer SetParameter omx_err(0x%08x)", CLASSNAME, __func__, omx_err);
      return false;
    }

    CLog::Log(LOGDEBUG, "%s::%s - Output bps %d samplerate %d channels %d buffer size %d bytes per second %d",
        CLASSNAME, __func__, (int)m_pcm_output.nBitPerSample, (int)m_pcm_output.nSamplingRate, (int)m_pcm_output.nChannels, m_BufferLen, m_BytesPerSec);
    PrintPCM(&m_pcm_output, std::string("output"));

    if( m_omx_splitter.IsInitialized() )
    {
      m_pcm_output.nPortIndex = m_omx_splitter.GetInputPort();
      omx_err = m_omx_splitter.SetParameter(OMX_IndexParamAudioPcm, &m_pcm_output);
      if(omx_err != OMX_ErrorNone)
      {
        CLog::Log(LOGERROR, "%s::%s - error m_omx_splitter SetParameter omx_err(0x%08x)", CLASSNAME, __func__, omx_err);
        return false;
      }

      m_pcm_output.nPortIndex = m_omx_splitter.GetOutputPort();
      omx_err = m_omx_splitter.SetParameter(OMX_IndexParamAudioPcm, &m_pcm_output);
      if(omx_err != OMX_ErrorNone)
      {
        CLog::Log(LOGERROR, "%s::%s - error m_omx_splitter SetParameter omx_err(0x%08x)", CLASSNAME, __func__, omx_err);
        return false;
      }
      m_pcm_output.nPortIndex = m_omx_splitter.GetOutputPort() + 1;
      omx_err = m_omx_splitter.SetParameter(OMX_IndexParamAudioPcm, &m_pcm_output);
      if(omx_err != OMX_ErrorNone)
      {
        CLog::Log(LOGERROR, "%s::%s - error m_omx_splitter SetParameter omx_err(0x%08x)", CLASSNAME, __func__, omx_err);
        return false;
      }
    }

    if( m_omx_render_analog.IsInitialized() )
    {
      m_pcm_output.nPortIndex = m_omx_render_analog.GetInputPort();
      omx_err = m_omx_render_analog.SetParameter(OMX_IndexParamAudioPcm, &m_pcm_output);
      if(omx_err != OMX_ErrorNone)
      {
        CLog::Log(LOGERROR, "%s::%s - error m_omx_render_analog SetParameter omx_err(0x%08x)", CLASSNAME, __func__, omx_err);
        return false;
      }
    }

    if( m_omx_render_hdmi.IsInitialized() )
    {
      m_pcm_output.nPortIndex = m_omx_render_hdmi.GetInputPort();
      omx_err = m_omx_render_hdmi.SetParameter(OMX_IndexParamAudioPcm, &m_pcm_output);
      if(omx_err != OMX_ErrorNone)
      {
        CLog::Log(LOGERROR, "%s::%s - error m_omx_render_hdmi SetParameter omx_err(0x%08x)", CLASSNAME, __func__, omx_err);
        return false;
      }
    }
  }
  if( m_omx_render_analog.IsInitialized() )
  {
    m_omx_tunnel_clock_analog.Initialize(m_omx_clock, m_omx_clock->GetInputPort(),
      &m_omx_render_analog, m_omx_render_analog.GetInputPort()+1);

    omx_err = m_omx_tunnel_clock_analog.Establish();
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "%s::%s - m_omx_tunnel_clock_analog.Establish omx_err(0x%08x)", CLASSNAME, __func__, omx_err);
      return false;
    }
    m_omx_render_analog.ResetEos();
  }
  if( m_omx_render_hdmi.IsInitialized() )
  {
    m_omx_tunnel_clock_hdmi.Initialize(m_omx_clock, m_omx_clock->GetInputPort() + (m_omx_render_analog.IsInitialized() ? 2 : 0),
      &m_omx_render_hdmi, m_omx_render_hdmi.GetInputPort()+1);

    omx_err = m_omx_tunnel_clock_hdmi.Establish();
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "%s::%s - m_omx_tunnel_clock_hdmi.Establish omx_err(0x%08x)", CLASSNAME, __func__, omx_err);
      return false;
    }
    m_omx_render_hdmi.ResetEos();
  }

  if( m_omx_render_analog.IsInitialized() )
  {
    // By default audio_render is the clock master, and if output samples don't fit the timestamps, it will speed up/slow down the clock.
    // This tends to be better for maintaining audio sync and avoiding audio glitches, but can affect video/display sync
    // when in dual audio mode, make analogue the slave
    OMX_CONFIG_BOOLEANTYPE configBool;
    OMX_INIT_STRUCTURE(configBool);
    configBool.bEnabled = m_config.is_live || m_config.device == "omx:both" ? OMX_FALSE:OMX_TRUE;

    omx_err = m_omx_render_analog.SetConfig(OMX_IndexConfigBrcmClockReferenceSource, &configBool);
    if (omx_err != OMX_ErrorNone)
       return false;

    OMX_CONFIG_BRCMAUDIODESTINATIONTYPE audioDest;
    OMX_INIT_STRUCTURE(audioDest);
    strncpy((char *)audioDest.sName, "local", strlen("local"));
    omx_err = m_omx_render_analog.SetConfig(OMX_IndexConfigBrcmAudioDestination, &audioDest);
    if (omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "%s::%s - m_omx_render_analog.SetConfig omx_err(0x%08x)", CLASSNAME, __func__, omx_err);
      return false;
    }
  }

  if( m_omx_render_hdmi.IsInitialized() )
  {
    // By default audio_render is the clock master, and if output samples don't fit the timestamps, it will speed up/slow down the clock.
    // This tends to be better for maintaining audio sync and avoiding audio glitches, but can affect video/display sync
    OMX_CONFIG_BOOLEANTYPE configBool;
    OMX_INIT_STRUCTURE(configBool);
    configBool.bEnabled = m_config.is_live ? OMX_FALSE:OMX_TRUE;

    omx_err = m_omx_render_hdmi.SetConfig(OMX_IndexConfigBrcmClockReferenceSource, &configBool);
    if (omx_err != OMX_ErrorNone)
       return false;

    OMX_CONFIG_BRCMAUDIODESTINATIONTYPE audioDest;
    OMX_INIT_STRUCTURE(audioDest);
    strncpy((char *)audioDest.sName, "hdmi", strlen("hdmi"));
    omx_err = m_omx_render_hdmi.SetConfig(OMX_IndexConfigBrcmAudioDestination, &audioDest);
    if (omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "%s::%s - m_omx_render_hdmi.SetConfig omx_err(0x%08x)", CLASSNAME, __func__, omx_err);
      return false;
    }
  }

  if( m_omx_splitter.IsInitialized() )
  {
    m_omx_tunnel_splitter_analog.Initialize(&m_omx_splitter, m_omx_splitter.GetOutputPort(), &m_omx_render_analog, m_omx_render_analog.GetInputPort());
    omx_err = m_omx_tunnel_splitter_analog.Establish();
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "COMXAudio::Initialize - Error m_omx_tunnel_splitter_analog.Establish 0x%08x", omx_err);
      return false;
    }

    m_omx_tunnel_splitter_hdmi.Initialize(&m_omx_splitter, m_omx_splitter.GetOutputPort() + 1, &m_omx_render_hdmi, m_omx_render_hdmi.GetInputPort());
    omx_err = m_omx_tunnel_splitter_hdmi.Establish();
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "COMXAudio::Initialize - Error m_omx_tunnel_splitter_hdmi.Establish 0x%08x", omx_err);
      return false;
    }
  }
  if( m_omx_mixer.IsInitialized() )
  {
    m_omx_tunnel_decoder.Initialize(&m_omx_decoder, m_omx_decoder.GetOutputPort(), &m_omx_mixer, m_omx_mixer.GetInputPort());
    if( m_omx_splitter.IsInitialized() )
    {
      m_omx_tunnel_mixer.Initialize(&m_omx_mixer, m_omx_mixer.GetOutputPort(), &m_omx_splitter, m_omx_splitter.GetInputPort());
    }
    else
    {
      if( m_omx_render_analog.IsInitialized() )
      {
        m_omx_tunnel_mixer.Initialize(&m_omx_mixer, m_omx_mixer.GetOutputPort(), &m_omx_render_analog, m_omx_render_analog.GetInputPort());
      }
      if( m_omx_render_hdmi.IsInitialized() )
      {
        m_omx_tunnel_mixer.Initialize(&m_omx_mixer, m_omx_mixer.GetOutputPort(), &m_omx_render_hdmi, m_omx_render_hdmi.GetInputPort());
      }
    }
    CLog::Log(LOGDEBUG, "%s::%s - bits:%d mode:%d channels:%d srate:%d nopassthrough", CLASSNAME, __func__,
            (int)m_pcm_input.nBitPerSample, m_pcm_input.ePCMMode, (int)m_pcm_input.nChannels, (int)m_pcm_input.nSamplingRate);
  }
  else
  {
    if( m_omx_render_analog.IsInitialized() )
    {
      m_omx_tunnel_decoder.Initialize(&m_omx_decoder, m_omx_decoder.GetOutputPort(), &m_omx_render_analog, m_omx_render_analog.GetInputPort());
    }
    else if( m_omx_render_hdmi.IsInitialized() )
    {
      m_omx_tunnel_decoder.Initialize(&m_omx_decoder, m_omx_decoder.GetOutputPort(), &m_omx_render_hdmi, m_omx_render_hdmi.GetInputPort());
    }
    CLog::Log(LOGDEBUG, "%s::%s - bits:%d mode:%d channels:%d srate:%d passthrough", CLASSNAME, __func__,
            0, 0, 0, 0);
  }

  omx_err = m_omx_tunnel_decoder.Establish();
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "%s::%s - m_omx_tunnel_decoder.Establish omx_err(0x%08x)", CLASSNAME, __func__, omx_err);
    return false;
  }

  if( m_omx_mixer.IsInitialized() )
  {
    omx_err = m_omx_mixer.SetStateForComponent(OMX_StateExecuting);
    if(omx_err != OMX_ErrorNone) {
      CLog::Log(LOGERROR, "%s::%s - m_omx_mixer OMX_StateExecuting omx_err(0x%08x)", CLASSNAME, __func__, omx_err);
      return false;
    }
  }

  if( m_omx_mixer.IsInitialized() )
  {
    omx_err = m_omx_tunnel_mixer.Establish();
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "%s::%s - m_omx_tunnel_decoder.Establish omx_err(0x%08x)", CLASSNAME, __func__, omx_err);
      return false;
    }
  }

  if( m_omx_splitter.IsInitialized() )
  {
    omx_err = m_omx_splitter.SetStateForComponent(OMX_StateExecuting);
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "%s::%s - m_omx_splitter OMX_StateExecuting 0x%08x", CLASSNAME, __func__, omx_err);
     return false;
    }
  }
  if( m_omx_render_analog.IsInitialized() )
  {
    omx_err = m_omx_render_analog.SetStateForComponent(OMX_StateExecuting);
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "%s::%s - m_omx_render_analog OMX_StateExecuting omx_err(0x%08x)", CLASSNAME, __func__, omx_err);
      return false;
    }
  }
  if( m_omx_render_hdmi.IsInitialized() )
  {
    omx_err = m_omx_render_hdmi.SetStateForComponent(OMX_StateExecuting);
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "%s::%s - m_omx_render_hdmi OMX_StateExecuting omx_err(0x%08x)", CLASSNAME, __func__, omx_err);
      return false;
    }
  }

  m_settings_changed = true;
  return true;
}

static unsigned count_bits(uint64_t value)
{
  unsigned bits = 0;
  for(;value;++bits)
    value &= value - 1;
  return bits;
}

bool COMXAudio::Initialize(OMXClock *clock, const OMXAudioConfig &config, uint64_t channelMap, unsigned int uiBitsPerSample)
{
  CSingleLock lock (m_critSection);
  OMX_ERRORTYPE omx_err;

  Deinitialize();

  if(!m_dllAvUtil.Load())
    return false;

  m_config = config;
  m_InputChannels = count_bits(channelMap);

  if(m_InputChannels == 0)
    return false;

  if(m_config.hints.samplerate == 0)
    return false;

  m_av_clock = clock;

  if(!m_av_clock)
    return false;

  /* passthrough overwrites hw decode */
  if(m_config.passthrough)
  {
    m_config.hwdecode = false;
  }
  else if(m_config.hwdecode)
  {
    /* check again if we are capable to hw decode the format */
    m_config.hwdecode = CanHWDecode(m_config.hints.codec);
  }

  if(m_config.passthrough || m_config.hwdecode)
    SetCodingType(m_config.hints.codec);
  else
    SetCodingType(AV_CODEC_ID_PCM_S16LE);

  m_omx_clock = m_av_clock->GetOMXClock();

  m_drc         = 0;

  memset(m_input_channels, 0x0, sizeof(m_input_channels));
  memset(m_output_channels, 0x0, sizeof(m_output_channels));
  memset(&m_wave_header, 0x0, sizeof(m_wave_header));

  m_wave_header.Format.nChannels  = 2;
  m_wave_header.dwChannelMask     = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;

  // set the input format, and get the channel layout so we know what we need to open
  if (!m_config.passthrough && channelMap)
  {
    enum PCMChannels inLayout[OMX_AUDIO_MAXCHANNELS];
    enum PCMChannels outLayout[OMX_AUDIO_MAXCHANNELS];
    // force out layout to stereo if input is not multichannel - it gives the receiver a chance to upmix
    if (channelMap == (AV_CH_FRONT_LEFT | AV_CH_FRONT_RIGHT) || channelMap == AV_CH_FRONT_CENTER)
      m_config.layout = PCM_LAYOUT_2_0;
    BuildChannelMap(inLayout, channelMap);
    m_OutputChannels = BuildChannelMapCEA(outLayout, GetChannelLayout(m_config.layout));
    CPCMRemap m_remap;
    m_remap.Reset();
    /*outLayout = */m_remap.SetInputFormat (m_InputChannels, inLayout, uiBitsPerSample / 8, m_config.hints.samplerate, m_config.layout, m_config.boostOnDownmix);
    m_remap.SetOutputFormat(m_OutputChannels, outLayout);
    m_remap.GetDownmixMatrix(m_downmix_matrix);
    m_wave_header.dwChannelMask = channelMap;
    BuildChannelMapOMX(m_input_channels, channelMap);
    BuildChannelMapOMX(m_output_channels, GetChannelLayout(m_config.layout));
  }

  m_BitsPerSample = uiBitsPerSample;

  m_BytesPerSec   = m_config.hints.samplerate * 2 << rounded_up_channels_shift[m_InputChannels];
  m_BufferLen     = m_BytesPerSec * AUDIO_BUFFER_SECONDS;
  m_InputBytesPerSec = m_config.hints.samplerate * m_BitsPerSample * m_InputChannels >> 3;

  // should be big enough that common formats (e.g. 6 channel DTS) fit in a single packet.
  // we don't mind less common formats being split (e.g. ape/wma output large frames)
  // 6 channel 32bpp float to 8 channel 16bpp in, so a full 48K input buffer will fit the output buffer
//  m_ChunkLen = AUDIO_DECODE_OUTPUT_BUFFER * (m_InputChannels * m_BitsPerSample) >> (rounded_up_channels_shift[m_InputChannels] + 4);
  m_ChunkLen = AUDIO_DECODE_OUTPUT_BUFFER * (6 * m_BitsPerSample) >> (rounded_up_channels_shift[6] + 4);

  m_wave_header.Samples.wSamplesPerBlock    = 0;
  m_wave_header.Format.nChannels            = m_InputChannels;
  m_wave_header.Format.nBlockAlign          = m_InputChannels *
    (m_BitsPerSample >> 3);
  // 0x8000 is custom format interpreted by GPU as WAVE_FORMAT_IEEE_FLOAT_PLANAR
  m_wave_header.Format.wFormatTag           = m_BitsPerSample == 32 ? 0x8000 : WAVE_FORMAT_PCM;
  m_wave_header.Format.nSamplesPerSec       = m_config.hints.samplerate;
  m_wave_header.Format.nAvgBytesPerSec      = m_BytesPerSec;
  m_wave_header.Format.wBitsPerSample       = m_BitsPerSample;
  m_wave_header.Samples.wValidBitsPerSample = m_BitsPerSample;
  m_wave_header.Format.cbSize               = 0;
  m_wave_header.SubFormat                   = KSDATAFORMAT_SUBTYPE_PCM;

  if(!m_omx_decoder.Initialize("OMX.broadcom.audio_decode", OMX_IndexParamAudioInit))
    return false;

  OMX_CONFIG_BOOLEANTYPE boolType;
  OMX_INIT_STRUCTURE(boolType);
  if(m_config.passthrough)
    boolType.bEnabled = OMX_TRUE;
  else
    boolType.bEnabled = OMX_FALSE;
  omx_err = m_omx_decoder.SetParameter(OMX_IndexParamBrcmDecoderPassThrough, &boolType);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXAudio::Initialize - Error OMX_IndexParamBrcmDecoderPassThrough 0x%08x", omx_err);
    printf("OMX_IndexParamBrcmDecoderPassThrough omx_err(0x%08x)\n", omx_err);
    return false;
  }

  // set up the number/size of buffers for decoder input
  OMX_PARAM_PORTDEFINITIONTYPE port_param;
  OMX_INIT_STRUCTURE(port_param);
  port_param.nPortIndex = m_omx_decoder.GetInputPort();

  omx_err = m_omx_decoder.GetParameter(OMX_IndexParamPortDefinition, &port_param);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXAudio::Initialize error get OMX_IndexParamPortDefinition (input) omx_err(0x%08x)\n", omx_err);
    return false;
  }

  port_param.format.audio.eEncoding = m_eEncoding;
  port_param.nBufferSize = m_ChunkLen;
  port_param.nBufferCountActual = std::max(port_param.nBufferCountMin, 16U);

  omx_err = m_omx_decoder.SetParameter(OMX_IndexParamPortDefinition, &port_param);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXAudio::Initialize error set OMX_IndexParamPortDefinition (intput) omx_err(0x%08x)\n", omx_err);
    return false;
  }

  // set up the number/size of buffers for decoder output
  OMX_INIT_STRUCTURE(port_param);
  port_param.nPortIndex = m_omx_decoder.GetOutputPort();

  omx_err = m_omx_decoder.GetParameter(OMX_IndexParamPortDefinition, &port_param);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXAudio::Initialize error get OMX_IndexParamPortDefinition (output) omx_err(0x%08x)\n", omx_err);
    return false;
  }

  port_param.nBufferCountActual = std::max((unsigned int)port_param.nBufferCountMin, m_BufferLen / port_param.nBufferSize);

  omx_err = m_omx_decoder.SetParameter(OMX_IndexParamPortDefinition, &port_param);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXAudio::Initialize error set OMX_IndexParamPortDefinition (output) omx_err(0x%08x)\n", omx_err);
    return false;
  }

  {
    OMX_AUDIO_PARAM_PORTFORMATTYPE formatType;
    OMX_INIT_STRUCTURE(formatType);
    formatType.nPortIndex = m_omx_decoder.GetInputPort();

    formatType.eEncoding = m_eEncoding;

    omx_err = m_omx_decoder.SetParameter(OMX_IndexParamAudioPortFormat, &formatType);
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "COMXAudio::Initialize error OMX_IndexParamAudioPortFormat omx_err(0x%08x)\n", omx_err);
      return false;
    }
  }

  omx_err = m_omx_decoder.AllocInputBuffers();
  if(omx_err != OMX_ErrorNone) 
  {
    CLog::Log(LOGERROR, "COMXAudio::Initialize - Error alloc buffers 0x%08x", omx_err);
    return false;
  }

    omx_err = m_omx_decoder.SetStateForComponent(OMX_StateExecuting);
    if(omx_err != OMX_ErrorNone) {
      CLog::Log(LOGERROR, "COMXAudio::Initialize - Error setting OMX_StateExecuting 0x%08x", omx_err);
      return false;
    }


  if(m_eEncoding == OMX_AUDIO_CodingPCM)
  {
    OMX_BUFFERHEADERTYPE *omx_buffer = m_omx_decoder.GetInputBuffer();
    if(omx_buffer == NULL)
    {
      CLog::Log(LOGERROR, "COMXAudio::Initialize - buffer error 0x%08x", omx_err);
      return false;
    }

    omx_buffer->nOffset = 0;
    omx_buffer->nFilledLen  = std::min(sizeof(m_wave_header), omx_buffer->nAllocLen);

    memset((unsigned char *)omx_buffer->pBuffer, 0x0, omx_buffer->nAllocLen);
    memcpy((unsigned char *)omx_buffer->pBuffer, &m_wave_header, omx_buffer->nFilledLen);
    omx_buffer->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;

    omx_err = m_omx_decoder.EmptyThisBuffer(omx_buffer);
    if (omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "%s::%s - OMX_EmptyThisBuffer() failed with result(0x%x)\n", CLASSNAME, __func__, omx_err);
      m_omx_decoder.DecoderEmptyBufferDone(m_omx_decoder.GetComponent(), omx_buffer);
      return false;
    }
  } 
  else if(m_config.hwdecode)
  {
    // send decoder config
    if(m_config.hints.extrasize > 0 && m_config.hints.extradata != NULL)
    {
      OMX_BUFFERHEADERTYPE *omx_buffer = m_omx_decoder.GetInputBuffer();
  
      if(omx_buffer == NULL)
      {
        CLog::Log(LOGERROR, "%s::%s - buffer error 0x%08x", CLASSNAME, __func__, omx_err);
        return false;
      }
  
      omx_buffer->nOffset = 0;
      omx_buffer->nFilledLen = std::min((OMX_U32)m_config.hints.extrasize, omx_buffer->nAllocLen);

      memset((unsigned char *)omx_buffer->pBuffer, 0x0, omx_buffer->nAllocLen);
      memcpy((unsigned char *)omx_buffer->pBuffer, m_config.hints.extradata, omx_buffer->nFilledLen);
      omx_buffer->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;
  
      omx_err = m_omx_decoder.EmptyThisBuffer(omx_buffer);
      if (omx_err != OMX_ErrorNone)
      {
        CLog::Log(LOGERROR, "%s::%s - OMX_EmptyThisBuffer() failed with result(0x%x)\n", CLASSNAME, __func__, omx_err);
        m_omx_decoder.DecoderEmptyBufferDone(m_omx_decoder.GetComponent(), omx_buffer);
        return false;
      }
    }
  }

  /* return on decoder error so m_Initialized stays false */
  if(m_omx_decoder.BadState())
    return false;

  OMX_INIT_STRUCTURE(m_pcm_input);
  m_pcm_input.nPortIndex            = m_omx_decoder.GetInputPort();
  memcpy(m_pcm_input.eChannelMapping, m_input_channels, sizeof(m_input_channels));
  m_pcm_input.eNumData              = OMX_NumericalDataSigned;
  m_pcm_input.eEndian               = OMX_EndianLittle;
  m_pcm_input.bInterleaved          = OMX_TRUE;
  m_pcm_input.nBitPerSample         = m_BitsPerSample;
  m_pcm_input.ePCMMode              = OMX_AUDIO_PCMModeLinear;
  m_pcm_input.nChannels             = m_InputChannels;
  m_pcm_input.nSamplingRate         = m_config.hints.samplerate;

  m_Initialized   = true;
  m_settings_changed = false;
  m_setStartTime  = true;
  m_submitted_eos = false;
  m_failed_eos = false;
  m_last_pts      = DVD_NOPTS_VALUE;
  m_submitted     = 0.0f;
  m_maxLevel      = 0.0f;

  CLog::Log(LOGDEBUG, "COMXAudio::Initialize Input bps %d samplerate %d channels %d buffer size %d bytes per second %d",
      (int)m_pcm_input.nBitPerSample, (int)m_pcm_input.nSamplingRate, (int)m_pcm_input.nChannels, m_BufferLen, m_InputBytesPerSec);
  PrintPCM(&m_pcm_input, std::string("input"));
  CLog::Log(LOGDEBUG, "COMXAudio::Initialize device %s passthrough %d hwdecode %d",
      m_config.device.c_str(), m_config.passthrough, m_config.hwdecode);

  return true;
}

bool COMXAudio::ChangeInputFormat(const CStdString& device, int iChannels, uint64_t channelMap,
                           COMXStreamInfo &hints, enum PCMLayout layout, unsigned int uiSampleRate, unsigned int uiBitsPerSample, bool boostOnDownmix,
                           OMXClock *clock, bool bUsePassthrough, bool bUseHWDecode, bool is_live, float fifo_size)
{
  CSingleLock lock (m_critSection);
  OMX_ERRORTYPE omx_err;

  layout = PCM_LAYOUT_2_0;

  m_InputChannels = iChannels;

  if(m_InputChannels == 0)
    return false;

//  if(hints.samplerate == 0)
//    return false;

  memset(m_input_channels, 0x0, sizeof(m_input_channels));
//  memset(m_output_channels, 0x0, sizeof(m_output_channels));
  memset(&m_wave_header, 0x0, sizeof(m_wave_header));

  m_wave_header.Format.nChannels  = 2;
  m_wave_header.dwChannelMask     = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;

  // set the input format, and get the channel layout so we know what we need to open
  if (!m_Passthrough && channelMap)
  {
    enum PCMChannels inLayout[OMX_AUDIO_MAXCHANNELS];
    enum PCMChannels outLayout[OMX_AUDIO_MAXCHANNELS];
    // force out layout to stereo if input is not multichannel - it gives the receiver a chance to upmix
    if (channelMap == (AV_CH_FRONT_LEFT | AV_CH_FRONT_RIGHT) || channelMap == AV_CH_FRONT_CENTER)
      layout = PCM_LAYOUT_2_0;
    BuildChannelMap(inLayout, channelMap);
    m_OutputChannels = BuildChannelMapCEA(outLayout, GetChannelLayout(layout));
    CPCMRemap m_remap;
    m_remap.Reset();
    /*outLayout = */m_remap.SetInputFormat (m_InputChannels, inLayout, uiBitsPerSample / 8, uiSampleRate, layout, !m_normalize_downmix);
    m_remap.SetOutputFormat(m_OutputChannels, outLayout);
    m_remap.GetDownmixMatrix(m_downmix_matrix);
    m_wave_header.dwChannelMask = channelMap;
    BuildChannelMapOMX(m_input_channels, channelMap);
    BuildChannelMapOMX(m_output_channels, GetChannelLayout(layout));
  }

  m_SampleRate = uiSampleRate;
  m_BitsPerSample = uiBitsPerSample;

  m_BytesPerSec   = m_SampleRate * 2 << rounded_up_channels_shift[m_InputChannels];
  m_BufferLen     = m_BytesPerSec * AUDIO_BUFFER_SECONDS;
  m_InputBytesPerSec = m_SampleRate * m_BitsPerSample * m_InputChannels >> 3;

  // should be big enough that common formats (e.g. 6 channel DTS) fit in a single packet.
  // we don't mind less common formats being split (e.g. ape/wma output large frames)
  // 6 channel 32bpp float to 8 channel 16bpp in, so a full 48K input buffer will fit the output buffer
//  m_ChunkLen = AUDIO_DECODE_OUTPUT_BUFFER * (m_InputChannels * m_BitsPerSample) >> (rounded_up_channels_shift[m_InputChannels] + 4);

  m_wave_header.Samples.wSamplesPerBlock    = 0;
  m_wave_header.Format.nChannels            = m_InputChannels;
  m_wave_header.Format.nBlockAlign          = m_InputChannels * (m_BitsPerSample >> 3);
  // 0x8000 is custom format interpreted by GPU as WAVE_FORMAT_IEEE_FLOAT_PLANAR
  m_wave_header.Format.wFormatTag           = m_BitsPerSample == 32 ? 0x8000 : WAVE_FORMAT_PCM;
  m_wave_header.Format.nSamplesPerSec       = m_SampleRate;
  m_wave_header.Format.nAvgBytesPerSec      = m_BytesPerSec;
  m_wave_header.Format.wBitsPerSample       = m_BitsPerSample;
  m_wave_header.Samples.wValidBitsPerSample = m_BitsPerSample;
  m_wave_header.Format.cbSize               = 0;
  m_wave_header.SubFormat                   = KSDATAFORMAT_SUBTYPE_PCM;

  OMX_INIT_STRUCTURE(m_pcm_input);
  memcpy(m_pcm_input.eChannelMapping, m_input_channels, sizeof(m_input_channels));
  m_pcm_input.eNumData              = OMX_NumericalDataSigned;
  m_pcm_input.eEndian               = OMX_EndianLittle;
  m_pcm_input.bInterleaved          = OMX_TRUE;
  m_pcm_input.nBitPerSample         = m_BitsPerSample;
  m_pcm_input.ePCMMode              = OMX_AUDIO_PCMModeLinear;
  m_pcm_input.nChannels             = m_InputChannels;
  m_pcm_input.nSamplingRate         = m_SampleRate;

  // set up the number/size of buffers for decoder input
  OMX_PARAM_PORTDEFINITIONTYPE port_param;
  OMX_INIT_STRUCTURE(port_param);
  port_param.nPortIndex = m_omx_decoder.GetInputPort();

  omx_err = m_omx_decoder.GetParameter(OMX_IndexParamPortDefinition, &port_param);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXAudio::ChangeInputFormat error get OMX_IndexParamPortDefinition (input) omx_err(0x%08x)\n", omx_err);
    return false;
  }

  port_param.format.audio.eEncoding = m_eEncoding;
  port_param.nBufferSize = m_ChunkLen;
  port_param.nBufferCountActual = std::max(port_param.nBufferCountMin, 16U);

  // set up the number/size of buffers for decoder output
  OMX_INIT_STRUCTURE(port_param);
  port_param.nPortIndex = m_omx_decoder.GetOutputPort();

  omx_err = m_omx_decoder.GetParameter(OMX_IndexParamPortDefinition, &port_param);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXAudio::ChangeInputFormat error get OMX_IndexParamPortDefinition (output) omx_err(0x%08x)\n", omx_err);
    return false;
  }

  port_param.nBufferCountActual = std::max((unsigned int)port_param.nBufferCountMin, m_BufferLen / port_param.nBufferSize);

  if(m_eEncoding == OMX_AUDIO_CodingPCM)
  {
    /* setup decoder input */
    OMX_INIT_STRUCTURE(m_pcm_output);
    m_pcm_output.nPortIndex = m_omx_decoder.GetOutputPort();
    omx_err = m_omx_decoder.GetParameter(OMX_IndexParamAudioPcm, &m_pcm_output);
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "%s::%s - error m_omx_decoder GetParameter omx_err(0x%08x)", CLASSNAME, __func__, omx_err);
    }

    printf( "m_omx_decoder OMX_IndexParamAudioPcm nChannels %d\n", m_pcm_output.nChannels );

    //// decoder output -> mixer input
    memcpy(m_pcm_output.eChannelMapping, m_input_channels, sizeof(m_input_channels));
    // round up to power of 2
    m_pcm_output.nChannels = m_InputChannels > 4 ? 8 : m_InputChannels > 2 ? 4 : m_InputChannels;
    /* limit samplerate (through resampling) if requested */
    m_pcm_output.nSamplingRate = std::min(std::max((int)m_pcm_output.nSamplingRate, 8000), 192000);

    printf( "m_omx_decoder OMX_IndexParamAudioPcm nChannels %d\n", m_pcm_output.nChannels );

    m_omx_decoder.DisablePort(m_omx_decoder.GetInputPort(), false);
    m_omx_decoder.EnablePort(m_omx_decoder.GetInputPort(), true);

    OMX_BUFFERHEADERTYPE *omx_buffer = m_omx_decoder.GetInputBuffer();
    if(omx_buffer == NULL)
    {
      CLog::Log(LOGERROR, "COMXAudio::ChangeInputFormat - buffer error 0x%08x", omx_err);
      return false;
    }

    omx_buffer->nOffset = 0;
    omx_buffer->nFilledLen = sizeof(m_wave_header);
    if(omx_buffer->nFilledLen > omx_buffer->nAllocLen)
    {
      CLog::Log(LOGERROR, "COMXAudio::ChangeInputFormat - omx_buffer->nFilledLen > omx_buffer->nAllocLen");
      return false;
    }
    memset((unsigned char *)omx_buffer->pBuffer, 0x0, omx_buffer->nAllocLen);
    memcpy((unsigned char *)omx_buffer->pBuffer, &m_wave_header, omx_buffer->nFilledLen);
    omx_buffer->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;

    omx_err = m_omx_decoder.EmptyThisBuffer(omx_buffer);
    if (omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "%s::%s - OMX_EmptyThisBuffer() failed with result(0x%x)\n", CLASSNAME, __func__, omx_err);
      return false;
    }
  } 
  else if(m_HWDecode)
  {
    // send decoder config
    if(m_extrasize > 0 && m_extradata != NULL)
    {
      OMX_BUFFERHEADERTYPE *omx_buffer = m_omx_decoder.GetInputBuffer();
  
      if(omx_buffer == NULL)
      {
        CLog::Log(LOGERROR, "%s::%s - buffer error 0x%08x", CLASSNAME, __func__, omx_err);
        return false;
      }
  
      omx_buffer->nOffset = 0;
      omx_buffer->nFilledLen = m_extrasize;
      if(omx_buffer->nFilledLen > omx_buffer->nAllocLen)
      {
        CLog::Log(LOGERROR, "%s::%s - omx_buffer->nFilledLen > omx_buffer->nAllocLen", CLASSNAME, __func__);
        return false;
      }

      memset((unsigned char *)omx_buffer->pBuffer, 0x0, omx_buffer->nAllocLen);
      memcpy((unsigned char *)omx_buffer->pBuffer, m_extradata, omx_buffer->nFilledLen);
      omx_buffer->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;
  
      omx_err = m_omx_decoder.EmptyThisBuffer(omx_buffer);
      if (omx_err != OMX_ErrorNone)
      {
        CLog::Log(LOGERROR, "%s::%s - OMX_EmptyThisBuffer() failed with result(0x%x)\n", CLASSNAME, __func__, omx_err);
        return false;
      }
    }
  }

  CLog::Log(LOGDEBUG, "COMXAudio::ChangeInputFormat Input bps %d samplerate %d channels %d buffer size %d bytes per second %d",
      (int)m_pcm_input.nBitPerSample, (int)m_pcm_input.nSamplingRate, (int)m_pcm_input.nChannels, m_BufferLen, m_InputBytesPerSec);
  PrintPCM(&m_pcm_input, std::string("input"));
  CLog::Log(LOGDEBUG, "COMXAudio::ChangeInputFormat device %s passthrough %d hwdecode %d",
      device.c_str(), m_Passthrough, m_HWDecode);

  return true;
}


//***********************************************************************************************
bool COMXAudio::Deinitialize()
{
  CSingleLock lock (m_critSection);

  if ( m_omx_tunnel_clock_analog.IsInitialized() )
    m_omx_tunnel_clock_analog.Deestablish();
  if ( m_omx_tunnel_clock_hdmi.IsInitialized() )
    m_omx_tunnel_clock_hdmi.Deestablish();

  // ignore expected errors on teardown
  if ( m_omx_mixer.IsInitialized() )
    m_omx_mixer.IgnoreNextError(OMX_ErrorPortUnpopulated);
  else
  {
    if ( m_omx_render_hdmi.IsInitialized() )
      m_omx_render_hdmi.IgnoreNextError(OMX_ErrorPortUnpopulated);
    if ( m_omx_render_analog.IsInitialized() )
      m_omx_render_analog.IgnoreNextError(OMX_ErrorPortUnpopulated);
  }

  m_omx_tunnel_decoder.Deestablish();
  if ( m_omx_tunnel_mixer.IsInitialized() )
    m_omx_tunnel_mixer.Deestablish();
  if ( m_omx_tunnel_splitter_hdmi.IsInitialized() )
    m_omx_tunnel_splitter_hdmi.Deestablish();
  if ( m_omx_tunnel_splitter_analog.IsInitialized() )
    m_omx_tunnel_splitter_analog.Deestablish();

  m_omx_decoder.FlushInput();

  m_omx_decoder.Deinitialize();
  if ( m_omx_mixer.IsInitialized() )
    m_omx_mixer.Deinitialize();
  if ( m_omx_splitter.IsInitialized() )
    m_omx_splitter.Deinitialize();
  if ( m_omx_render_hdmi.IsInitialized() )
    m_omx_render_hdmi.Deinitialize();
  if ( m_omx_render_analog.IsInitialized() )
    m_omx_render_analog.Deinitialize();

  m_BytesPerSec = 0;
  m_BufferLen   = 0;

  m_omx_clock = NULL;
  m_av_clock  = NULL;

  m_Initialized = false;

  m_dllAvUtil.Unload();

  while(!m_ampqueue.empty())
    m_ampqueue.pop_front();

  m_last_pts      = DVD_NOPTS_VALUE;
  m_submitted     = 0.0f;
  m_maxLevel      = 0.0f;

  return true;
}

void COMXAudio::Flush()
{
  CSingleLock lock (m_critSection);
  if(!m_Initialized)
    return;

  m_omx_decoder.FlushAll();
  if ( m_omx_mixer.IsInitialized() )
    m_omx_mixer.FlushAll();
  if ( m_omx_splitter.IsInitialized() )
    m_omx_splitter.FlushAll();

  if ( m_omx_render_analog.IsInitialized() )
    m_omx_render_analog.FlushAll();
  if ( m_omx_render_hdmi.IsInitialized() )
    m_omx_render_hdmi.FlushAll();
  
  while(!m_ampqueue.empty())
    m_ampqueue.pop_front();

  if( m_omx_render_analog.IsInitialized() )
    m_omx_render_analog.ResetEos();
  if( m_omx_render_hdmi.IsInitialized() )
    m_omx_render_hdmi.ResetEos();

  m_last_pts      = DVD_NOPTS_VALUE;
  m_submitted     = 0.0f;
  m_maxLevel      = 0.0f;
  m_setStartTime  = true;
}

//***********************************************************************************************
void COMXAudio::SetDynamicRangeCompression(long drc)
{
  CSingleLock lock (m_critSection);
  m_amplification = powf(10.0f, (float)drc / 2000.0f);
  if (m_settings_changed)
    UpdateAttenuation();
}

//***********************************************************************************************
void COMXAudio::SetMute(bool bMute)
{
  CSingleLock lock (m_critSection);
  m_Mute = bMute;
  if (m_settings_changed)
    UpdateAttenuation();
}

//***********************************************************************************************
void COMXAudio::SetVolume(float fVolume)
{
  CSingleLock lock (m_critSection);
  m_CurrentVolume = fVolume;
  if (m_settings_changed)
    UpdateAttenuation();
}

float COMXAudio::GetVolume() 
{
  return m_Mute ? VOLUME_MINIMUM : m_CurrentVolume;
}

//***********************************************************************************************
bool COMXAudio::ApplyVolume(void)
{
  float m_ac3Gain = 12.0f;
  CSingleLock lock (m_critSection);

  if(!m_Initialized || m_config.passthrough)
    return false;

  float fVolume = m_Mute ? VOLUME_MINIMUM : m_CurrentVolume;

  // the analogue volume is too quiet for some. Allow use of an advancedsetting to boost this (at risk of distortion) (deprecated)
  double gain = pow(10, (m_ac3Gain - 12.0f) / 20.0);

  const float* coeff = m_downmix_matrix;

  OMX_CONFIG_BRCMAUDIODOWNMIXCOEFFICIENTS8x8 mix;
  OMX_INIT_STRUCTURE(mix);
  OMX_ERRORTYPE omx_err;

  assert(sizeof(mix.coeff)/sizeof(mix.coeff[0]) == 64);

  if (m_amplification != 1.0)
  {
    // reduce scaling so overflow can be seen
    for(size_t i = 0; i < 8*8; ++i)
      mix.coeff[i] = static_cast<unsigned int>(0x10000 * (coeff[i] * gain * 0.01f));

    mix.nPortIndex = m_omx_decoder.GetInputPort();
    omx_err = m_omx_decoder.SetConfig(OMX_IndexConfigBrcmAudioDownmixCoefficients8x8, &mix);
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "%s::%s - error setting decoder OMX_IndexConfigBrcmAudioDownmixCoefficients, error 0x%08x\n",
            CLASSNAME, __func__, omx_err);
      return false;
    }
  }
  for(size_t i = 0; i < 8*8; ++i)
    mix.coeff[i] = static_cast<unsigned int>(0x10000 * (coeff[i] * gain * fVolume * m_amplification * m_attenuation));

  mix.nPortIndex = m_omx_mixer.GetInputPort();
  omx_err = m_omx_mixer.SetConfig(OMX_IndexConfigBrcmAudioDownmixCoefficients8x8, &mix);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "%s::%s - error setting mixer OMX_IndexConfigBrcmAudioDownmixCoefficients, error 0x%08x\n",
              CLASSNAME, __func__, omx_err);
    return false;
  }
  CLog::Log(LOGINFO, "%s::%s - Volume=%.2f (* %.2f * %.2f)\n", CLASSNAME, __func__, fVolume, m_amplification, m_attenuation);
  return true;
}


//***********************************************************************************************
unsigned int COMXAudio::AddPackets(const void* data, unsigned int len)
{
  return AddPackets(data, len, 0, 0, 0);
}

//***********************************************************************************************
unsigned int COMXAudio::AddPackets(const void* data, unsigned int len, double dts, double pts, unsigned int frame_size)
{
  CSingleLock lock (m_critSection);

  if(!m_Initialized)
  {
    CLog::Log(LOGERROR,"COMXAudio::AddPackets - sanity failed. no valid play handle!");
    return len;
  }

  unsigned pitch = (m_config.passthrough || m_config.hwdecode) ? 1:(m_BitsPerSample >> 3) * m_InputChannels;
  unsigned int demuxer_samples = len / pitch;
  unsigned int demuxer_samples_sent = 0;
  uint8_t *demuxer_content = (uint8_t *)data;

  OMX_ERRORTYPE omx_err;

  OMX_BUFFERHEADERTYPE *omx_buffer = NULL;

  while(demuxer_samples_sent < demuxer_samples)
  {
    // 200ms timeout
    omx_buffer = m_omx_decoder.GetInputBuffer(200);

    if(omx_buffer == NULL)
    {
      CLog::Log(LOGERROR, "COMXAudio::Decode timeout\n");
      printf("COMXAudio::Decode timeout\n");
      return len;
    }

    omx_buffer->nOffset = 0;
    omx_buffer->nFlags  = 0;

    // we want audio_decode output buffer size to be no more than AUDIO_DECODE_OUTPUT_BUFFER.
    // it will be 16-bit and rounded up to next power of 2 in channels
    unsigned int max_buffer = AUDIO_DECODE_OUTPUT_BUFFER * (m_InputChannels * m_BitsPerSample) >> (rounded_up_channels_shift[m_InputChannels] + 4);

    unsigned int remaining = demuxer_samples-demuxer_samples_sent;
    unsigned int samples_space = std::min(max_buffer, omx_buffer->nAllocLen)/pitch;
    unsigned int samples = std::min(remaining, samples_space);

    omx_buffer->nFilledLen = samples * pitch;

    unsigned int frames = frame_size ? len/frame_size:0;
    if ((samples < demuxer_samples || frames > 1) && m_BitsPerSample==32 && !(m_config.passthrough || m_config.hwdecode))
    {
      const unsigned int sample_pitch   = m_BitsPerSample >> 3;
      const unsigned int frame_samples  = frame_size / pitch;
      const unsigned int plane_size     = frame_samples * sample_pitch;
      const unsigned int out_plane_size = samples * sample_pitch;
      //CLog::Log(LOGDEBUG, "%s::%s samples:%d/%d ps:%d ops:%d fs:%d pitch:%d filled:%d frames=%d", CLASSNAME, __func__, samples, demuxer_samples, plane_size, out_plane_size, frame_size, pitch, omx_buffer
      for (unsigned int sample = 0; sample < samples; )
      {
        unsigned int frame = (demuxer_samples_sent + sample) / frame_samples;
        unsigned int sample_in_frame = (demuxer_samples_sent + sample) - frame * frame_samples;
        int out_remaining = std::min(std::min(frame_samples - sample_in_frame, samples), samples-sample);
        uint8_t *src = demuxer_content + frame*frame_size + sample_in_frame * sample_pitch;
        uint8_t *dst = (uint8_t *)omx_buffer->pBuffer + sample * sample_pitch;
        for (unsigned int channel = 0; channel < m_InputChannels; channel++)
        {
          //CLog::Log(LOGDEBUG, "%s::%s copy(%d,%d,%d) (s:%d f:%d sin:%d c:%d)", CLASSNAME, __func__, dst-(uint8_t *)omx_buffer->pBuffer, src-demuxer_content, out_remaining, sample, frame, sample_in_frame
          memcpy(dst, src, out_remaining * sample_pitch);
          src += plane_size;
          dst += out_plane_size;
        }
        sample += out_remaining;
      }
    }
    else
    {
       uint8_t *dst = omx_buffer->pBuffer;
       uint8_t *src = demuxer_content + demuxer_samples_sent * pitch;
       memcpy(dst, src, omx_buffer->nFilledLen);
    }

    uint64_t val  = (uint64_t)(pts == DVD_NOPTS_VALUE) ? 0 : pts;

    if(m_setStartTime)
    {
      omx_buffer->nFlags = OMX_BUFFERFLAG_STARTTIME;

      m_last_pts = pts;

      CLog::Log(LOGDEBUG, "COMXAudio::Decode ADec : setStartTime %f\n", (float)val / DVD_TIME_BASE);
      m_setStartTime = false;
    }
    else
    {
      if(pts == DVD_NOPTS_VALUE)
      {
        omx_buffer->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;
        m_last_pts = pts;
      }
      else if (m_last_pts != pts)
      {
        if(pts > m_last_pts)
          m_last_pts = pts;
        else
          omx_buffer->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;
      }
      else if (m_last_pts == pts)
      {
        omx_buffer->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;
      }
    }

    omx_buffer->nTimeStamp = ToOMXTime(val);

    demuxer_samples_sent += samples;

    if(demuxer_samples_sent == demuxer_samples)
      omx_buffer->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;

    omx_err = m_omx_decoder.EmptyThisBuffer(omx_buffer);
    if (omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "%s::%s - OMX_EmptyThisBuffer() failed with result(0x%x)\n", CLASSNAME, __func__, omx_err);
      printf("%s::%s - OMX_EmptyThisBuffer() failed with result(0x%x)\n", CLASSNAME, __func__, omx_err);
      m_omx_decoder.DecoderEmptyBufferDone(m_omx_decoder.GetComponent(), omx_buffer);
      return 0;
    }
    //CLog::Log(LOGINFO, "AudiD: dts:%.0f pts:%.0f size:%d\n", dts, pts, len);

    omx_err = m_omx_decoder.WaitForEvent(OMX_EventPortSettingsChanged, 0);
    if (omx_err == OMX_ErrorNone)
    {
      if(!PortSettingsChanged())
      {
        CLog::Log(LOGERROR, "%s::%s - error PortSettingsChanged omx_err(0x%08x)\n", CLASSNAME, __func__, omx_err);
      }
    }
  }
  m_submitted += (float)demuxer_samples / m_config.hints.samplerate;
  UpdateAttenuation();
  return len;
}

void COMXAudio::UpdateAttenuation()
{
  if (m_amplification == 1.0)
  {
    ApplyVolume();
    return;
  }

  double level_pts = 0.0;
  float level = GetMaxLevel(level_pts);
  if (level_pts != 0.0)
  {
    amplitudes_t v;
    v.level = level;
    v.pts = level_pts;
    m_ampqueue.push_back(v);
  }
  double stamp = m_av_clock->OMXMediaTime();
  // discard too old data
  while(!m_ampqueue.empty())
  {
    amplitudes_t &v = m_ampqueue.front();
    /* we'll also consume if queue gets unexpectedly long to avoid filling memory */
    if (v.pts == DVD_NOPTS_VALUE || v.pts < stamp || v.pts - stamp > DVD_SEC_TO_TIME(15.0))
      m_ampqueue.pop_front();
    else break;
  }
  float maxlevel = 0.0f, imminent_maxlevel = 0.0f;
  for (int i=0; i < (int)m_ampqueue.size(); i++)
  {
    amplitudes_t &v = m_ampqueue[i];
    maxlevel = std::max(maxlevel, v.level);
    // check for maximum volume in next 200ms
    if (v.pts != DVD_NOPTS_VALUE && v.pts < stamp + DVD_SEC_TO_TIME(0.2))
      imminent_maxlevel = std::max(imminent_maxlevel, v.level);
  }

  if (maxlevel != 0.0)
  {
    float m_limiterHold = 0.025f;
    float m_limiterRelease = 0.100f;
    float alpha_h = -1.0f/(0.025f*log10f(0.999f));
    float alpha_r = -1.0f/(0.100f*log10f(0.900f));
    float decay  = powf(10.0f, -1.0f / (alpha_h * m_limiterHold));
    float attack = powf(10.0f, -1.0f / (alpha_r * m_limiterRelease));
    // if we are going to clip imminently then deal with it now
    if (imminent_maxlevel > m_maxLevel)
      m_maxLevel = imminent_maxlevel;
    // clip but not imminently can ramp up more slowly
    else if (maxlevel > m_maxLevel)
      m_maxLevel = attack * m_maxLevel + (1.0f-attack) * maxlevel;
    // not clipping, decay more slowly
    else
      m_maxLevel = decay  * m_maxLevel + (1.0f-decay ) * maxlevel;

    // want m_maxLevel * amp -> 1.0
    float amp = m_amplification * m_attenuation;

    // We fade in the attenuation over first couple of seconds
    float start = std::min(std::max((m_submitted-1.0f), 0.0f), 1.0f);
    float attenuation = std::min(1.0f, std::max(m_attenuation / (amp * m_maxLevel), 1.0f/m_amplification));
    m_attenuation = (1.0f - start) * 1.0f/m_amplification + start * attenuation;
  }
  else
  {
    m_attenuation = 1.0f/m_amplification;
  }
  ApplyVolume();
}

//***********************************************************************************************
unsigned int COMXAudio::GetSpace()
{
  int free = m_omx_decoder.GetInputBufferSpace();
  return free;
}

float COMXAudio::GetDelay()
{
  CSingleLock lock (m_critSection);
  double stamp = DVD_NOPTS_VALUE;
  double ret = 0.0;
  if (m_last_pts != DVD_NOPTS_VALUE && m_av_clock)
    stamp = m_av_clock->OMXMediaTime();
  // if possible the delay is current media time - time of last submitted packet
  if (stamp != DVD_NOPTS_VALUE)
  {
    ret = (m_last_pts - stamp) * (1.0 / DVD_TIME_BASE);
    //CLog::Log(LOGINFO, "%s::%s - %.2f %.0f %.0f", CLASSNAME, __func__, ret, stamp, m_last_pts);
  }
  else // just measure the input fifo
  {
    unsigned int used = m_omx_decoder.GetInputBufferSize() - m_omx_decoder.GetInputBufferSpace();
    ret = m_InputBytesPerSec ? (float)used / (float)m_InputBytesPerSec : 0.0f;
    //CLog::Log(LOGINFO, "%s::%s - %.2f %d, %d, %d", CLASSNAME, __func__, ret, used, m_omx_decoder.GetInputBufferSize(), m_omx_decoder.GetInputBufferSpace());
  }
  return ret;
}

float COMXAudio::GetCacheTime()
{
  return GetDelay();
}

float COMXAudio::GetCacheTotal()
{
  float audioplus_buffer = m_config.hints.samplerate ? 32.0f * 512.0f / m_config.hints.samplerate : 0.0f;
  float input_buffer = m_InputBytesPerSec ? (float)m_omx_decoder.GetInputBufferSize() / (float)m_InputBytesPerSec : 0;
  return AUDIO_BUFFER_SECONDS + input_buffer + audioplus_buffer;
}

//***********************************************************************************************
unsigned int COMXAudio::GetChunkLen()
{
  return m_ChunkLen;
}

unsigned int COMXAudio::GetAudioRenderingLatency()
{
  CSingleLock lock (m_critSection);

  if(!m_Initialized)
    return 0;

  OMX_PARAM_U32TYPE param;
  OMX_INIT_STRUCTURE(param);

  if(m_omx_render_analog.IsInitialized())
  {
    param.nPortIndex = m_omx_render_analog.GetInputPort();

    OMX_ERRORTYPE omx_err = m_omx_render_analog.GetConfig(OMX_IndexConfigAudioRenderingLatency, &param);

    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "%s::%s - error getting OMX_IndexConfigAudioRenderingLatency error 0x%08x\n",
        CLASSNAME, __func__, omx_err);
      return 0;
    }
  }
  else if(m_omx_render_hdmi.IsInitialized())
  {
    param.nPortIndex = m_omx_render_hdmi.GetInputPort();

    OMX_ERRORTYPE omx_err = m_omx_render_hdmi.GetConfig(OMX_IndexConfigAudioRenderingLatency, &param);

    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "%s::%s - error getting OMX_IndexConfigAudioRenderingLatency error 0x%08x\n",
        CLASSNAME, __func__, omx_err);
      return 0;
    }
  }
  return param.nU32;
}

float COMXAudio::GetMaxLevel(double &pts)
{
  CSingleLock lock (m_critSection);

  if(!m_Initialized)
    return 0;

  OMX_CONFIG_BRCMAUDIOMAXSAMPLE param;
  OMX_INIT_STRUCTURE(param);

  if(m_omx_decoder.IsInitialized())
  {
    param.nPortIndex = m_omx_decoder.GetInputPort();

    OMX_ERRORTYPE omx_err = m_omx_decoder.GetConfig(OMX_IndexConfigBrcmAudioMaxSample, &param);

    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "%s::%s - error getting OMX_IndexConfigBrcmAudioMaxSample error 0x%08x\n",
        CLASSNAME, __func__, omx_err);
      return 0;
    }
  }
  pts = FromOMXTime(param.nTimeStamp);
  return (float)param.nMaxSample * (100.0f / (1<<15));
}

void COMXAudio::SubmitEOS()
{
  CSingleLock lock (m_critSection);

  if(!m_Initialized)
    return;

  m_submitted_eos = true;
  m_failed_eos = false;

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  OMX_BUFFERHEADERTYPE *omx_buffer = m_omx_decoder.GetInputBuffer(1000);

  if(omx_buffer == NULL)
  {
    CLog::Log(LOGERROR, "%s::%s - buffer error 0x%08x", CLASSNAME, __func__, omx_err);
    m_failed_eos = true;
    return;
  }

  omx_buffer->nOffset     = 0;
  omx_buffer->nFilledLen  = 0;
  omx_buffer->nTimeStamp  = ToOMXTime(0LL);

  omx_buffer->nFlags = OMX_BUFFERFLAG_ENDOFFRAME | OMX_BUFFERFLAG_EOS | OMX_BUFFERFLAG_TIME_UNKNOWN;

  omx_err = m_omx_decoder.EmptyThisBuffer(omx_buffer);
  if (omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "%s::%s - OMX_EmptyThisBuffer() failed with result(0x%x)\n", CLASSNAME, __func__, omx_err);
    m_omx_decoder.DecoderEmptyBufferDone(m_omx_decoder.GetComponent(), omx_buffer);
    return;
  }
  CLog::Log(LOGINFO, "%s::%s", CLASSNAME, __func__);
}

bool COMXAudio::IsEOS()
{
  if(!m_Initialized)
    return true;
  unsigned int latency = GetAudioRenderingLatency();
  CSingleLock lock (m_critSection);

  if (!m_failed_eos && !(m_omx_decoder.IsEOS() && latency == 0))
    return false;

  if (m_submitted_eos)
  {
    CLog::Log(LOGINFO, "%s::%s", CLASSNAME, __func__);
    m_submitted_eos = false;
  }
  return true;
}

void COMXAudio::SetCodingType(AVCodecID codec)
{
  switch(codec)
  { 
    case AV_CODEC_ID_DTS:
      CLog::Log(LOGDEBUG, "COMXAudio::SetCodingType OMX_AUDIO_CodingDTS\n");
      m_eEncoding = OMX_AUDIO_CodingDTS;
      break;
    case AV_CODEC_ID_AC3:
    case AV_CODEC_ID_EAC3:
      CLog::Log(LOGDEBUG, "COMXAudio::SetCodingType OMX_AUDIO_CodingDDP\n");
      m_eEncoding = OMX_AUDIO_CodingDDP;
      break;
    default:
      CLog::Log(LOGDEBUG, "COMXAudio::SetCodingType OMX_AUDIO_CodingPCM\n");
      m_eEncoding = OMX_AUDIO_CodingPCM;
      break;
  } 
}

bool COMXAudio::CanHWDecode(AVCodecID codec)
{
  switch(codec)
  { 
    /*
    case AV_CODEC_ID_VORBIS:
      CLog::Log(LOGDEBUG, "COMXAudio::CanHWDecode OMX_AUDIO_CodingVORBIS\n");
      m_eEncoding = OMX_AUDIO_CodingVORBIS;
      m_config.hwdecode = true;
      break;
    case AV_CODEC_ID_AAC:
      CLog::Log(LOGDEBUG, "COMXAudio::CanHWDecode OMX_AUDIO_CodingAAC\n");
      m_eEncoding = OMX_AUDIO_CodingAAC;
      m_config.hwdecode = true;
      break;
    */
    case AV_CODEC_ID_MP2:
    case AV_CODEC_ID_MP3:
      CLog::Log(LOGDEBUG, "COMXAudio::CanHWDecode OMX_AUDIO_CodingMP3\n");
      m_eEncoding = OMX_AUDIO_CodingMP3;
      m_config.hwdecode = true;
      break;
    case AV_CODEC_ID_DTS:
      CLog::Log(LOGDEBUG, "COMXAudio::CanHWDecode OMX_AUDIO_CodingDTS\n");
      m_eEncoding = OMX_AUDIO_CodingDTS;
      m_config.hwdecode = true;
      break;
    case AV_CODEC_ID_AC3:
    case AV_CODEC_ID_EAC3:
      CLog::Log(LOGDEBUG, "COMXAudio::CanHWDecode OMX_AUDIO_CodingDDP\n");
      m_eEncoding = OMX_AUDIO_CodingDDP;
      m_config.hwdecode = true;
      break;
    default:
      CLog::Log(LOGDEBUG, "COMXAudio::CanHWDecode OMX_AUDIO_CodingPCM\n");
      m_eEncoding = OMX_AUDIO_CodingPCM;
      m_config.hwdecode = false;
      break;
  } 

  return m_config.hwdecode;
}

bool COMXAudio::HWDecode(AVCodecID codec)
{
  bool ret = false;

  switch(codec)
  { 
    /*
    case AV_CODEC_ID_VORBIS:
      CLog::Log(LOGDEBUG, "COMXAudio::HWDecode AV_CODEC_ID_VORBIS\n");
      ret = true;
      break;
    case AV_CODEC_ID_AAC:
      CLog::Log(LOGDEBUG, "COMXAudio::HWDecode AV_CODEC_ID_AAC\n");
      ret = true;
      break;
    */
    case AV_CODEC_ID_MP2:
    case AV_CODEC_ID_MP3:
      CLog::Log(LOGDEBUG, "COMXAudio::HWDecode AV_CODEC_ID_MP2 / AV_CODEC_ID_MP3\n");
      ret = true;
      break;
    case AV_CODEC_ID_DTS:
      CLog::Log(LOGDEBUG, "COMXAudio::HWDecode AV_CODEC_ID_DTS\n");
      ret = true;
      break;
    case AV_CODEC_ID_AC3:
    case AV_CODEC_ID_EAC3:
      CLog::Log(LOGDEBUG, "COMXAudio::HWDecode AV_CODEC_ID_AC3 / AV_CODEC_ID_EAC3\n");
      ret = true;
      break;
    default:
      ret = false;
      break;
  } 

  return ret;
}

void COMXAudio::PrintChannels(OMX_AUDIO_CHANNELTYPE eChannelMapping[])
{
  for(int i = 0; i < OMX_AUDIO_MAXCHANNELS; i++)
  {
    switch(eChannelMapping[i])
    {
      case OMX_AUDIO_ChannelLF:
        CLog::Log(LOGDEBUG, "OMX_AUDIO_ChannelLF\n");
        break;
      case OMX_AUDIO_ChannelRF:
        CLog::Log(LOGDEBUG, "OMX_AUDIO_ChannelRF\n");
        break;
      case OMX_AUDIO_ChannelCF:
        CLog::Log(LOGDEBUG, "OMX_AUDIO_ChannelCF\n");
        break;
      case OMX_AUDIO_ChannelLS:
        CLog::Log(LOGDEBUG, "OMX_AUDIO_ChannelLS\n");
        break;
      case OMX_AUDIO_ChannelRS:
        CLog::Log(LOGDEBUG, "OMX_AUDIO_ChannelRS\n");
        break;
      case OMX_AUDIO_ChannelLFE:
        CLog::Log(LOGDEBUG, "OMX_AUDIO_ChannelLFE\n");
        break;
      case OMX_AUDIO_ChannelCS:
        CLog::Log(LOGDEBUG, "OMX_AUDIO_ChannelCS\n");
        break;
      case OMX_AUDIO_ChannelLR:
        CLog::Log(LOGDEBUG, "OMX_AUDIO_ChannelLR\n");
        break;
      case OMX_AUDIO_ChannelRR:
        CLog::Log(LOGDEBUG, "OMX_AUDIO_ChannelRR\n");
        break;
      case OMX_AUDIO_ChannelNone:
      case OMX_AUDIO_ChannelKhronosExtensions:
      case OMX_AUDIO_ChannelVendorStartUnused:
      case OMX_AUDIO_ChannelMax:
      default:
        break;
    }
  }
}

void COMXAudio::PrintPCM(OMX_AUDIO_PARAM_PCMMODETYPE *pcm, std::string direction)
{
  CLog::Log(LOGDEBUG, "pcm->direction      : %s\n", direction.c_str());
  CLog::Log(LOGDEBUG, "pcm->nPortIndex     : %d\n", (int)pcm->nPortIndex);
  CLog::Log(LOGDEBUG, "pcm->eNumData       : %d\n", pcm->eNumData);
  CLog::Log(LOGDEBUG, "pcm->eEndian        : %d\n", pcm->eEndian);
  CLog::Log(LOGDEBUG, "pcm->bInterleaved   : %d\n", (int)pcm->bInterleaved);
  CLog::Log(LOGDEBUG, "pcm->nBitPerSample  : %d\n", (int)pcm->nBitPerSample);
  CLog::Log(LOGDEBUG, "pcm->ePCMMode       : %d\n", pcm->ePCMMode);
  CLog::Log(LOGDEBUG, "pcm->nChannels      : %d\n", (int)pcm->nChannels);
  CLog::Log(LOGDEBUG, "pcm->nSamplingRate  : %d\n", (int)pcm->nSamplingRate);

  PrintChannels(pcm->eChannelMapping);
}

void COMXAudio::BuildChannelMap(enum PCMChannels *channelMap, uint64_t layout)
{
  int index = 0;
  if (layout & AV_CH_FRONT_LEFT           ) channelMap[index++] = PCM_FRONT_LEFT           ;
  if (layout & AV_CH_FRONT_RIGHT          ) channelMap[index++] = PCM_FRONT_RIGHT          ;
  if (layout & AV_CH_FRONT_CENTER         ) channelMap[index++] = PCM_FRONT_CENTER         ;
  if (layout & AV_CH_LOW_FREQUENCY        ) channelMap[index++] = PCM_LOW_FREQUENCY        ;
  if (layout & AV_CH_BACK_LEFT            ) channelMap[index++] = PCM_BACK_LEFT            ;
  if (layout & AV_CH_BACK_RIGHT           ) channelMap[index++] = PCM_BACK_RIGHT           ;
  if (layout & AV_CH_FRONT_LEFT_OF_CENTER ) channelMap[index++] = PCM_FRONT_LEFT_OF_CENTER ;
  if (layout & AV_CH_FRONT_RIGHT_OF_CENTER) channelMap[index++] = PCM_FRONT_RIGHT_OF_CENTER;
  if (layout & AV_CH_BACK_CENTER          ) channelMap[index++] = PCM_BACK_CENTER          ;
  if (layout & AV_CH_SIDE_LEFT            ) channelMap[index++] = PCM_SIDE_LEFT            ;
  if (layout & AV_CH_SIDE_RIGHT           ) channelMap[index++] = PCM_SIDE_RIGHT           ;
  if (layout & AV_CH_TOP_CENTER           ) channelMap[index++] = PCM_TOP_CENTER           ;
  if (layout & AV_CH_TOP_FRONT_LEFT       ) channelMap[index++] = PCM_TOP_FRONT_LEFT       ;
  if (layout & AV_CH_TOP_FRONT_CENTER     ) channelMap[index++] = PCM_TOP_FRONT_CENTER     ;
  if (layout & AV_CH_TOP_FRONT_RIGHT      ) channelMap[index++] = PCM_TOP_FRONT_RIGHT      ;
  if (layout & AV_CH_TOP_BACK_LEFT        ) channelMap[index++] = PCM_TOP_BACK_LEFT        ;
  if (layout & AV_CH_TOP_BACK_CENTER      ) channelMap[index++] = PCM_TOP_BACK_CENTER      ;
  if (layout & AV_CH_TOP_BACK_RIGHT       ) channelMap[index++] = PCM_TOP_BACK_RIGHT       ;
  while (index<OMX_AUDIO_MAXCHANNELS)
    channelMap[index++] = PCM_INVALID;
}

// See CEA spec: Table 20, Audio InfoFrame data byte 4 for the ordering here
int COMXAudio::BuildChannelMapCEA(enum PCMChannels *channelMap, uint64_t layout)
{
  int index = 0;
  if (layout & AV_CH_FRONT_LEFT           ) channelMap[index++] = PCM_FRONT_LEFT;
  if (layout & AV_CH_FRONT_RIGHT          ) channelMap[index++] = PCM_FRONT_RIGHT;
  if (layout & AV_CH_LOW_FREQUENCY        ) channelMap[index++] = PCM_LOW_FREQUENCY;
  if (layout & AV_CH_FRONT_CENTER         ) channelMap[index++] = PCM_FRONT_CENTER;
  if (layout & AV_CH_BACK_LEFT            ) channelMap[index++] = PCM_BACK_LEFT;
  if (layout & AV_CH_BACK_RIGHT           ) channelMap[index++] = PCM_BACK_RIGHT;
  if (layout & AV_CH_SIDE_LEFT            ) channelMap[index++] = PCM_SIDE_LEFT;
  if (layout & AV_CH_SIDE_RIGHT           ) channelMap[index++] = PCM_SIDE_RIGHT;

  while (index<OMX_AUDIO_MAXCHANNELS)
    channelMap[index++] = PCM_INVALID;

  int num_channels = 0;
  for (index=0; index<OMX_AUDIO_MAXCHANNELS; index++)
    if (channelMap[index] != PCM_INVALID)
       num_channels = index+1;
  // round up to power of 2
  num_channels = num_channels > 4 ? 8 : num_channels > 2 ? 4 : num_channels;
  return num_channels;
}

void COMXAudio::BuildChannelMapOMX(enum OMX_AUDIO_CHANNELTYPE *	channelMap, uint64_t layout)
{
  int index = 0;

  if (layout & AV_CH_FRONT_LEFT           ) channelMap[index++] = OMX_AUDIO_ChannelLF;
  if (layout & AV_CH_FRONT_RIGHT          ) channelMap[index++] = OMX_AUDIO_ChannelRF;
  if (layout & AV_CH_FRONT_CENTER         ) channelMap[index++] = OMX_AUDIO_ChannelCF;
  if (layout & AV_CH_LOW_FREQUENCY        ) channelMap[index++] = OMX_AUDIO_ChannelLFE;
  if (layout & AV_CH_BACK_LEFT            ) channelMap[index++] = OMX_AUDIO_ChannelLR;
  if (layout & AV_CH_BACK_RIGHT           ) channelMap[index++] = OMX_AUDIO_ChannelRR;
  if (layout & AV_CH_SIDE_LEFT            ) channelMap[index++] = OMX_AUDIO_ChannelLS;
  if (layout & AV_CH_SIDE_RIGHT           ) channelMap[index++] = OMX_AUDIO_ChannelRS;
  if (layout & AV_CH_BACK_CENTER          ) channelMap[index++] = OMX_AUDIO_ChannelCS;
  // following are not in openmax spec, but gpu does accept them
  if (layout & AV_CH_FRONT_LEFT_OF_CENTER ) channelMap[index++] = (enum OMX_AUDIO_CHANNELTYPE)10;
  if (layout & AV_CH_FRONT_RIGHT_OF_CENTER) channelMap[index++] = (enum OMX_AUDIO_CHANNELTYPE)11;
  if (layout & AV_CH_TOP_CENTER           ) channelMap[index++] = (enum OMX_AUDIO_CHANNELTYPE)12;
  if (layout & AV_CH_TOP_FRONT_LEFT       ) channelMap[index++] = (enum OMX_AUDIO_CHANNELTYPE)13;
  if (layout & AV_CH_TOP_FRONT_CENTER     ) channelMap[index++] = (enum OMX_AUDIO_CHANNELTYPE)14;
  if (layout & AV_CH_TOP_FRONT_RIGHT      ) channelMap[index++] = (enum OMX_AUDIO_CHANNELTYPE)15;
  if (layout & AV_CH_TOP_BACK_LEFT        ) channelMap[index++] = (enum OMX_AUDIO_CHANNELTYPE)16;
  if (layout & AV_CH_TOP_BACK_CENTER      ) channelMap[index++] = (enum OMX_AUDIO_CHANNELTYPE)17;
  if (layout & AV_CH_TOP_BACK_RIGHT       ) channelMap[index++] = (enum OMX_AUDIO_CHANNELTYPE)18;

  while (index<OMX_AUDIO_MAXCHANNELS)
    channelMap[index++] = OMX_AUDIO_ChannelNone;
}

uint64_t COMXAudio::GetChannelLayout(enum PCMLayout layout)
{
  uint64_t layouts[] = {
    /* 2.0 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT,
    /* 2.1 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_LOW_FREQUENCY,
    /* 3.0 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_FRONT_CENTER,
    /* 3.1 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_FRONT_CENTER | 1<<PCM_LOW_FREQUENCY,
    /* 4.0 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_BACK_LEFT | 1<<PCM_BACK_RIGHT,
    /* 4.1 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_BACK_LEFT | 1<<PCM_BACK_RIGHT | 1<<PCM_LOW_FREQUENCY,
    /* 5.0 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_FRONT_CENTER | 1<<PCM_BACK_LEFT | 1<<PCM_BACK_RIGHT,
    /* 5.1 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_FRONT_CENTER | 1<<PCM_BACK_LEFT | 1<<PCM_BACK_RIGHT | 1<<PCM_LOW_FREQUENCY,
    /* 7.0 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_FRONT_CENTER | 1<<PCM_SIDE_LEFT | 1<<PCM_SIDE_RIGHT | 1<<PCM_BACK_LEFT | 1<<PCM_BACK_RIGHT,
    /* 7.1 */ 1<<PCM_FRONT_LEFT | 1<<PCM_FRONT_RIGHT | 1<<PCM_FRONT_CENTER | 1<<PCM_SIDE_LEFT | 1<<PCM_SIDE_RIGHT | 1<<PCM_BACK_LEFT | 1<<PCM_BACK_RIGHT | 1<<PCM_LOW_FREQUENCY
  };
  return (int)layout < 10 ? layouts[(int)layout] : 0;
}
