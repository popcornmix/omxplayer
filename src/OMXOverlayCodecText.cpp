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

#include "system.h"
#include "OMXOverlayCodecText.h"
#include "OMXOverlayText.h"
#include "OMXStreamInfo.h"
#include "utils/log.h"
#include "OMXSubtitleTagSami.h"

COMXOverlayCodecText::COMXOverlayCodecText() : COMXOverlayCodec("Text Subtitle Decoder")
{
  m_pOverlay = NULL;
  m_bIsSSA = false;
}

COMXOverlayCodecText::~COMXOverlayCodecText()
{
  if(m_pOverlay)
    delete m_pOverlay;
  m_pOverlay = NULL;
}

bool COMXOverlayCodecText::Open(COMXStreamInfo &hints)
{
  m_bIsSSA = (hints.codec == AV_CODEC_ID_SSA);
  if(hints.codec == AV_CODEC_ID_SUBRIP || hints.codec == AV_CODEC_ID_SSA)
    return true;
  return false;
}

void COMXOverlayCodecText::Dispose()
{
  if(m_pOverlay)
    delete m_pOverlay;
  m_pOverlay = NULL;
}

int COMXOverlayCodecText::Decode(BYTE* data, int size, double pts, double duration)
{
  if(m_pOverlay)
    delete m_pOverlay;
  m_pOverlay = NULL;

  m_pOverlay = new COMXOverlayText();
  m_pOverlay->iPTSStartTime = 0;
  m_pOverlay->iPTSStopTime = 0;


  char *start, *end, *p;
  start = (char*)data;
  end   = (char*)data + size;
  p     = (char*)data;

  if (m_bIsSSA)
  {
    // currently just skip the prefixed ssa fields (8 (9?) fields)
    int nFieldCount = 9;
    while (nFieldCount > 0 && start < end)
    {
      if (*start == ',')
        nFieldCount--;

      start++;
      p++;
    }
  }

  COMXSubtitleTagSami TagConv;
  bool Taginit = TagConv.Init();

  while(p<end)
  {
    if(*p == '{')
    {
      if(p>start)
      {
        if(Taginit)
          TagConv.ConvertLine(m_pOverlay, start, p-start);
        else
          m_pOverlay->AddElement(new COMXOverlayText::CElementText(start, p-start));
      }
      start = p+1;

      while(*p != '}' && p<end)
        p++;

      char* override = (char*)malloc(p-start + 1);
      memcpy(override, start, p-start);
      override[p-start] = '\0';
      CLog::Log(LOGINFO, "%s - Skipped formatting tag %s", __FUNCTION__, override);
      free(override);

      start = p+1;
    }
    p++;
  }
  if(p>start)
  {
    if(Taginit)
    {
      TagConv.ConvertLine(m_pOverlay, start, p-start);
      TagConv.CloseTag(m_pOverlay);
    }
    else
      m_pOverlay->AddElement(new COMXOverlayText::CElementText(start, p-start));
  }
  return OC_OVERLAY;
}

void COMXOverlayCodecText::Reset()
{
  if(m_pOverlay)
    delete m_pOverlay;
  m_pOverlay = NULL;
}

void COMXOverlayCodecText::Flush()
{
  if(m_pOverlay)
    delete m_pOverlay;
  m_pOverlay = NULL;
}

COMXOverlay* COMXOverlayCodecText::GetOverlay()
{
  if(m_pOverlay)
  {
    COMXOverlay* overlay = m_pOverlay;
    m_pOverlay = NULL;
    return overlay;
  }
  return NULL;
}
