#pragma once

/*
 * Author: Torarin Hals Bakke (2012)
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

#include "OMXThread.h"
#include "OMXReader.h"
#include "OMXClock.h"
#include "OMXOverlayCodecText.h"
#include "Subtitle.h"
#include "utils/Mailbox.h"

#include <boost/config.hpp>
#include <boost/circular_buffer.hpp>
#include <atomic>
#include <string>
#include <vector>
#include <utility>

class OMXPlayerSubtitles : public OMXThread
{
public:
  OMXPlayerSubtitles(const OMXPlayerSubtitles&) = delete;
  OMXPlayerSubtitles& operator=(const OMXPlayerSubtitles&) = delete;
  OMXPlayerSubtitles() BOOST_NOEXCEPT;
  ~OMXPlayerSubtitles() BOOST_NOEXCEPT;
  bool Open(size_t stream_count,
            std::vector<Subtitle>&& external_subtitles,
            const std::string& font_path,
            const std::string& italic_font_path,
            float font_size,
            bool centered,
            bool ghost_box,
            unsigned int lines,
            OMXClock* clock) BOOST_NOEXCEPT;
  void Close() BOOST_NOEXCEPT;
  void Flush() BOOST_NOEXCEPT;
  void Resume() BOOST_NOEXCEPT;
  void Pause() BOOST_NOEXCEPT;

  void SetVisible(bool visible) BOOST_NOEXCEPT;

  bool GetVisible() BOOST_NOEXCEPT
  {
    assert(m_open);
    return m_visible;
  }
  
  void SetActiveStream(size_t index) BOOST_NOEXCEPT;

  size_t GetActiveStream() BOOST_NOEXCEPT
  {
    assert(m_open);
    assert(!m_subtitle_buffers.empty());
    return m_active_index;
  }

  void SetDelay(int value) BOOST_NOEXCEPT;

  int GetDelay() BOOST_NOEXCEPT
  {
    assert(m_open);
    return m_delay;
  }

  void SetUseExternalSubtitles(bool use) BOOST_NOEXCEPT;

  bool GetUseExternalSubtitles() BOOST_NOEXCEPT
  {
    assert(m_open);
    return m_use_external_subtitles;
  }

  void DisplayText(const std::string& text, int duration) BOOST_NOEXCEPT;

  bool AddPacket(OMXPacket *pkt, size_t stream_index) BOOST_NOEXCEPT;

private:
  struct Message {
    struct Stop {};
    struct Flush
    {
      std::vector<Subtitle> subtitles;
    };
    struct Push
    {
      Subtitle subtitle;
    };
    struct Touch {};
    struct SetDelay
    {
      int value;
    };
    struct SetPaused
    {
      bool value;
    };
    struct DisplayText
    {
      std::vector<std::string> text_lines;
      int duration;
    };
  };

  template <typename T>
  void SendToRenderer(T&& msg)
  {
    if(m_thread_stopped.load(std::memory_order_relaxed))
    {
      CLog::Log(LOGERROR, "Subtitle rendering thread not running, message discarded");
      return;
    }
    m_mailbox.send(std::forward<T>(msg));
  }

  void Process();
  void RenderLoop(const std::string& font_path,
                  const std::string& italic_font_path,
                  float font_size,
                  bool centered,
                  bool ghost_box,
                  unsigned int lines,
                  OMXClock* clock);
  std::vector<std::string> GetTextLines(OMXPacket *pkt);
  void FlushRenderer();

  COMXOverlayCodecText                          m_subtitle_codec;
  std::vector<Subtitle>                         m_external_subtitles;
  std::vector<boost::circular_buffer<Subtitle>> m_subtitle_buffers;
  Mailbox<Message::Stop,
          Message::Flush,
          Message::Push,
          Message::Touch,
          Message::SetPaused,
          Message::SetDelay,
          Message::DisplayText>                 m_mailbox;
  bool                                          m_visible;
  bool                                          m_use_external_subtitles;
  size_t                                        m_active_index;
  int                                           m_delay;
  std::atomic<bool>                             m_thread_stopped;
  std::string                                   m_font_path;
  std::string                                   m_italic_font_path;
  float                                         m_font_size;
  bool                                          m_centered;
  bool                                          m_ghost_box;
  unsigned int                                  m_lines;
  OMXClock*                                     m_av_clock;

#ifndef NDEBUG
  bool m_open;
#endif
};
