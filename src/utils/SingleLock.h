// SingleLock.h: interface for the CSingleLock class.
//
//////////////////////////////////////////////////////////////////////

/*
 * XBMC Media Center
 * Copyright (c) 2002 Frodo
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

#pragma once

#include <pthread.h>

class CCriticalSection
{
public:
  inline CCriticalSection()
  {
    pthread_mutexattr_t mta;
    pthread_mutexattr_init(&mta);
    pthread_mutexattr_settype(&mta, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&m_lock, &mta);
  }
  inline ~CCriticalSection() { pthread_mutex_destroy(&m_lock); }
  inline void Lock()         { pthread_mutex_lock(&m_lock); }
  inline void Unlock()       { pthread_mutex_unlock(&m_lock); }

protected:
  pthread_mutex_t m_lock;
};


class CSingleLock
{
public:
  inline CSingleLock(CCriticalSection& cs) { m_section = cs; m_section.Lock(); }
  inline ~CSingleLock()                    { m_section.Unlock(); }

protected:
  CCriticalSection m_section;
};


