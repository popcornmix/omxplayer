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

#include "OMXStreamInfo.h"

COMXStreamInfo::COMXStreamInfo()                                                     
{ 
  extradata = NULL; 
  Clear(); 
}

COMXStreamInfo::~COMXStreamInfo()
{
  //if( extradata && extrasize ) free(extradata);

  extradata = NULL;
  extrasize = 0;
}


void COMXStreamInfo::Clear()
{
  codec = CODEC_ID_NONE;
  software = false;
  codec_tag  = 0;

  //if( extradata && extrasize ) free(extradata);

  extradata = NULL;
  extrasize = 0;

  fpsscale = 0;
  fpsrate  = 0;
  height   = 0;
  width    = 0;
  aspect   = 0.0;
  vfr      = false;
  stills   = false;
  level    = 0;
  profile  = 0;
  ptsinvalid = false;

  channels   = 0;
  samplerate = 0;
  blockalign = 0;
  bitrate    = 0;
  bitspersample = 0;

  identifier = 0;

  framesize  = 0;
  syncword   = 0;
}
