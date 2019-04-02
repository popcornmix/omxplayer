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

#include "OMXPlayerSubtitles.h"
#include "OMXOverlayText.h"
#include "SubtitleRenderer.h"
#include "utils/Enforce.h"
#include "utils/ScopeExit.h"
#include "utils/Clamp.h"
#include "utils/log.h"
#include "utils/Strprintf.h"

#include <boost/algorithm/string.hpp>
#include <utility>
#include <algorithm>
#include <typeinfo>
#include <cstdint>

using namespace std;
using namespace boost;

OMXPlayerSubtitles::OMXPlayerSubtitles() BOOST_NOEXCEPT
: m_visible(),
  m_use_external_subtitles(),
  m_active_index(),
  m_delay(),
  m_thread_stopped(),
  m_font_size(),
  m_centered(),
  m_title_centered(),
  m_ghost_box(),
  m_lines(),
  m_av_clock(),
#ifndef NDEBUG
  m_open()
#endif
{}

OMXPlayerSubtitles::~OMXPlayerSubtitles() BOOST_NOEXCEPT
{
  Close();
}

bool OMXPlayerSubtitles::Open(size_t stream_count,
                              vector<Subtitle>&& external_subtitles,
                              const string& font_path,
                              const string& italic_font_path,
                              const string& title_font_path,
                              float font_size,
                              float title_font_size,
                              bool centered,
                              bool title_centered,
                              bool ghost_box,
                              unsigned int lines,
                              int display, int layer,
                              OMXClock* clock,
                              const string& title,
                              bool show_time) BOOST_NOEXCEPT
{
  assert(!m_open);

  m_subtitle_buffers.resize(stream_count, circular_buffer<Subtitle>(32));
  m_external_subtitles = std::move(external_subtitles);
  
  m_visible = true;
  m_use_external_subtitles = true;
  m_active_index = 0;
  m_delay = 0;
  m_thread_stopped.store(false, memory_order_relaxed);

  m_font_path = font_path;
  m_italic_font_path = italic_font_path;
  m_title_font_path = title_font_path;
  m_font_size = font_size;
  m_title_font_size = title_font_size;
  m_centered = centered;
  m_title_centered = title_centered;
  m_ghost_box = ghost_box;
  m_lines = lines;
  m_av_clock = clock;
  m_display = display;
  m_layer = layer;
  m_title = title;
  m_show_time = show_time;

  if(!Create())
    return false;

  SendToRenderer(Message::Flush{m_external_subtitles, m_title, m_show_time});

#ifndef NDEBUG
  m_open = true;
#endif

  return true;
}

void OMXPlayerSubtitles::Close() BOOST_NOEXCEPT
{
  if(Running())
  {
    SendToRenderer(Message::Stop{});
    StopThread();
  }

  m_mailbox.clear();
  m_subtitle_buffers.clear();

#ifndef NDEBUG
  m_open = false;
#endif
}

void OMXPlayerSubtitles::Process()
{
  try
  {
    RenderLoop(m_font_path, m_italic_font_path, m_title_font_path, m_font_size, m_title_font_size,
               m_centered, m_title_centered, m_ghost_box, m_lines, m_av_clock);
  }
  catch(Enforce_error& e)
  {
    if(!e.user_friendly_what().empty())
      printf("Error: %s\n", e.user_friendly_what().c_str());
    CLog::Log(LOGERROR, "OMXPlayerSubtitles::RenderLoop threw %s (%s)",
              typeid(e).name(), e.what());
  }
  catch(std::exception& e)
  {
    CLog::Log(LOGERROR, "OMXPlayerSubtitles::RenderLoop threw %s (%s)",
              typeid(e).name(), e.what());
  }
  m_thread_stopped.store(true, memory_order_relaxed);
}

template <typename Iterator>
Iterator FindSubtitle(Iterator begin, Iterator end, int time)
{
  return upper_bound(begin, end, time,
    [](int a, const Subtitle& b) { return a < b.stop; });
}

void OMXPlayerSubtitles::
RenderLoop(const string& font_path,
           const string& italic_font_path,
           const string& title_font_path,
           float font_size,
           float title_font_size,
           bool centered,
           bool title_centered,
           bool ghost_box,
           unsigned int lines,
           OMXClock* clock)
{
  SubtitleRenderer renderer(m_display, m_layer,
                            font_path,
                            italic_font_path,
                            title_font_path,
                            font_size,
                            title_font_size,
                            0.06f, 0.06f,
                            centered,
                            title_centered,
                            0xDD,
                            ghost_box ? 0x80 : 0,
                            lines);

  vector<Subtitle> subtitles;
  std::string title;

  int prev_now{};
  size_t next_index{};
  bool exit{};
  bool paused{};
  bool have_next{};
  int current_stop = INT_MIN;
  bool showing{};
  bool osd{};
  chrono::time_point<std::chrono::steady_clock> osd_stop;
  int delay{};
  bool show_time{};

  auto GetCurrentTime = [&]
  {
    return static_cast<int>(clock->OMXMediaTime()/1000) - delay;
  };

  auto PrepareTime = [&]
  {
    extern OMXReader m_omx_reader;
    auto t = (unsigned) (m_av_clock->OMXMediaTime()*1e-3);
    auto dur = m_omx_reader.GetStreamLength() / 1000;

    std::string time_curr;
    std::string time_total;
    int hours = t/3600000;
    if (hours)
      time_curr = strprintf("%02d:%02d:%02d", hours, (t/60000)%60, (t/1000)%60);
    else
      time_curr = strprintf("%02d:%02d", (t/60000)%60, (t/1000)%60);

    hours = dur/3600;
    if (hours)
      time_total = strprintf("%02d:%02d:%02d", hours, (dur/60)%60, dur%60);
    else
      time_total = strprintf("%02d:%02d", (dur/60)%60, dur%60);

    renderer.prepare_time(time_curr + " / " + time_total);
  };

  auto TryPrepare = [&](int time)
  {
    for(; next_index != subtitles.size(); ++next_index)
    {
      if(subtitles[next_index].stop > time)
      {
        renderer.prepare(subtitles[next_index].text_lines);
        have_next = true;
        break;
      }
    }
  };

  auto Reset = [&](int time)
  {
    renderer.unprepare();
    current_stop = INT_MIN;

    auto it = FindSubtitle(subtitles.begin(),
                           subtitles.end(),
                           time);
    next_index = it - subtitles.begin();

    if(next_index != subtitles.size())
    {
      renderer.prepare(subtitles[next_index].text_lines);
      have_next = true;
    }
    else
    {
      have_next = false;
    }
  };

  auto show_time_offset = GetCurrentTime() % 1000;
  chrono::time_point<std::chrono::steady_clock> show_time_next_update;

  for(;;)
  {
    int timeout = INT_MAX;

    if(!paused)
    {
      auto now = GetCurrentTime();

      int till_stop =
        showing ? current_stop - now
                : INT_MAX;

      int till_next_start =
        have_next ? subtitles[next_index].start - now
                  : INT_MAX;

      int till_next_show_time = 1000;
      if (show_time)
        till_next_show_time = 1000 - (GetCurrentTime() % 1000) + show_time_offset;

      timeout = min(min(till_next_show_time, min(till_stop, till_next_start)), 1000);
    }

    if(show_time)
    {
      procrustes(timeout,
        chrono::duration_cast<std::chrono::milliseconds>(
          show_time_next_update - chrono::steady_clock::now()).count());
    }

    if(osd)
    {
      procrustes(timeout,
        chrono::duration_cast<std::chrono::milliseconds>(
          osd_stop - chrono::steady_clock::now()).count());
    }

    m_mailbox.receive_wait(chrono::milliseconds(timeout),
      [&](Message::Push&& args)
      {
        subtitles.push_back(std::move(args.subtitle));
      },
      [&](Message::Flush&& args)
      {
        subtitles = std::move(args.subtitles);
        title = std::move(args.title);
        show_time = std::move(args.show_time);
        prev_now = INT_MAX;

        if (title != "") {
          renderer.prepare_title(title);
          renderer.redraw();
        }
      },
      [&](Message::Touch&&)
      {
      },
      [&](Message::SetPaused&& args)
      {
        paused = args.value;
      },
      [&](Message::SetDelay&& args)
      {
        delay = args.value;
        prev_now = INT_MAX;
      },
      [&](Message::Stop&&)
      {
        exit = true;
      },
      [&](Message::DisplayText&& args)
      {
        renderer.prepare(args.text_lines);
        if (show_time)
          PrepareTime(); // Also regenerate displayed time.
        renderer.show_next();
        showing = true;
        osd = true;
        osd_stop = chrono::steady_clock::now() +
                   chrono::milliseconds(args.duration);
        prev_now = INT_MAX;
      },
      [&](Message::SetRect&& args)
      {
        renderer.set_rect(args.x1, args.y1, args.x2, args.y2);
      });

    if(exit) break;

    auto now = GetCurrentTime();

    if(now < prev_now || (have_next && subtitles[next_index].stop <= now))
    {
      Reset(now);
    }
    else if(!have_next)
    {
      TryPrepare(now);
    }

    prev_now = now;

    if(osd && chrono::steady_clock::now() >= osd_stop)
      osd = false;

    bool redraw_needed = false;

    if (show_time && chrono::steady_clock::now() >= show_time_next_update) {
      show_time_next_update = chrono::steady_clock::now() + chrono::seconds(1);
      PrepareTime();
      redraw_needed = true;
    }

    if(!osd && current_stop <= now)
    {
      if(have_next && subtitles[next_index].start <= now)
      {
        renderer.show_next();
        redraw_needed = false;
        // printf("show error: %i ms\n", now - subtitles[next_index].start);
        showing = true;
        current_stop = subtitles[next_index].stop;

        ++next_index;
        have_next = false;
        TryPrepare(now);
      }
      else if(showing)
      {
        renderer.hide();
        redraw_needed = false;
        // printf("hide error: %i ms\n", now - current_stop);
        showing = false;
      }
    }

    if(redraw_needed)
      renderer.redraw();
  }
}

void OMXPlayerSubtitles::FlushRenderer()
{
  assert(GetVisible());

  if(GetUseExternalSubtitles())
  {
    SendToRenderer(Message::Flush{m_external_subtitles, m_title});
  }
  else
  {
    Message::Flush flush;
    assert(!m_subtitle_buffers.empty());
    for(auto& s : m_subtitle_buffers[m_active_index])
      flush.subtitles.push_back(s);
    SendToRenderer(std::move(flush));
  }
}

void OMXPlayerSubtitles::Flush() BOOST_NOEXCEPT
{
  assert(m_open);

  for(auto& q : m_subtitle_buffers)
    q.clear();

  if(GetVisible())
  {
    if(GetUseExternalSubtitles())
      SendToRenderer(Message::Touch{});
    else
      SendToRenderer(Message::Flush{});
  }
}

void OMXPlayerSubtitles::Resume() BOOST_NOEXCEPT
{
  assert(m_open);
  SendToRenderer(Message::SetPaused{false});
}

void OMXPlayerSubtitles::Pause() BOOST_NOEXCEPT
{
  assert(m_open);
  SendToRenderer(Message::SetPaused{true});
}

void OMXPlayerSubtitles::SetUseExternalSubtitles(bool use) BOOST_NOEXCEPT
{
  assert(m_open);
  assert(use || !m_subtitle_buffers.empty());

  m_use_external_subtitles = use;
  if(GetVisible())
    FlushRenderer();
}

void OMXPlayerSubtitles::SetDelay(int value) BOOST_NOEXCEPT
{
  assert(m_open);

  m_delay = value;
  SendToRenderer(Message::SetDelay{value});
}

void OMXPlayerSubtitles::SetVisible(bool visible) BOOST_NOEXCEPT
{
  assert(m_open);

  if(visible)
  {
    if (!m_visible)
    {
      m_visible = true;
      FlushRenderer();
    }
  }
  else
  {
    if(m_visible)
    {
      m_visible = false;
      SendToRenderer(Message::Flush{});
    }
  }
}

void OMXPlayerSubtitles::SetActiveStream(size_t index) BOOST_NOEXCEPT
{
  assert(m_open);
  assert(index < m_subtitle_buffers.size());

  m_active_index = index;
  if(!GetUseExternalSubtitles() && GetVisible())
    FlushRenderer();
}

vector<string> OMXPlayerSubtitles::GetTextLines(OMXPacket *pkt)
{
  assert(pkt);

  m_subtitle_codec.Open(pkt->hints);

  auto result = m_subtitle_codec.Decode(pkt->data, pkt->size, 0, 0);
  assert(result == OC_OVERLAY);

  auto overlay = m_subtitle_codec.GetOverlay();
  assert(overlay);

  vector<string> text_lines;

  auto e = ((COMXOverlayText*) overlay)->m_pHead;
  if(e && e->IsElementType(COMXOverlayText::ELEMENT_TYPE_TEXT))
  {
    split(text_lines,
          ((COMXOverlayText::CElementText*) e)->m_text,
          is_any_of("\r\n"),
          token_compress_on);
  }

  return text_lines;
}

bool OMXPlayerSubtitles::AddPacket(OMXPacket *pkt, size_t stream_index) BOOST_NOEXCEPT
{
  assert(m_open);
  assert(stream_index < m_subtitle_buffers.size());

  if(!pkt)
    return false;

  SCOPE_EXIT
  {
    OMXReader::FreePacket(pkt);
  };

  if(pkt->hints.codec != AV_CODEC_ID_SUBRIP && 
     pkt->hints.codec != AV_CODEC_ID_SSA &&
     pkt->hints.codec != AV_CODEC_ID_ASS)
  {
    return true;
  }

  auto start = static_cast<int>(pkt->pts/1000);
  auto stop = start + static_cast<int>(pkt->duration/1000);
  auto text_lines = GetTextLines(pkt);

  if (!m_subtitle_buffers[stream_index].empty() &&
    stop < m_subtitle_buffers[stream_index].back().stop)
  {
    stop = m_subtitle_buffers[stream_index].back().stop;
  }

  m_subtitle_buffers[stream_index].push_back(
    Subtitle(start, stop, vector<string>()));
  m_subtitle_buffers[stream_index].back().text_lines = text_lines;

  if(!GetUseExternalSubtitles() &&
     GetVisible() &&
     stream_index == GetActiveStream())
  {
    SendToRenderer(Message::Push{{start, stop, std::move(text_lines)}});
  }

  return true;
}

void OMXPlayerSubtitles::DisplayText(const std::string& text, int duration) BOOST_NOEXCEPT
{
  assert(m_open);

  vector<string> text_lines;
  split(text_lines, text, is_any_of("\n"));
  SendToRenderer(Message::DisplayText{std::move(text_lines), duration});
}

void OMXPlayerSubtitles::SetSubtitleRect(int x1, int y1, int x2, int y2) BOOST_NOEXCEPT
{
  SendToRenderer(Message::SetRect{x1, y1, x2, y2});
}
