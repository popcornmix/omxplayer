#pragma once
/*
 *      Copyright (C) 2005-2009 Team XBMC
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

/*
#define _HAVE_SBRK 1
#define HAVE_CMAKE_CONFIG 1
#define _REENTRANT 1
#ifndef VCHI_BULK_ALIGN
#define VCHI_BULK_ALIGN 1
#endif
#ifndef VCHI_BULK_GRANULARITY
#define VCHI_BULK_GRANULARITY 1 
#endif
*/
//#define OMX_SKIP64BIT 
#ifndef USE_VCHIQ_ARM
#define USE_VCHIQ_ARM
#endif
#ifndef __VIDEOCORE4__
#define __VIDEOCORE4__
#endif
#ifndef HAVE_VMCS_CONFIG
#define HAVE_VMCS_CONFIG
#endif

#ifndef HAVE_LIBBCM_HOST
#define HAVE_LIBBCM_HOST
#endif

#include "DllBCM.h"

class CRBP
{
public:
  CRBP();
  ~CRBP();

  bool Initialize();
  void Deinitialize();

private:
  DllBcmHost *m_DllBcmHost;
  bool       m_initialized;
};

extern CRBP g_RBP;
