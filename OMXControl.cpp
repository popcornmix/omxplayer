#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <string.h>
#include <dbus/dbus.h>

#include <string>
#include <sstream>
#include <utility>

#include "utils/log.h"
#include "OMXControl.h"
#include "KeyConfig.h"

#define CLASSNAME "OMXControl"

OMXControlResult::OMXControlResult( int newKey ) {
  key = newKey;
}

OMXControlResult::OMXControlResult( int newKey, int64_t newArg ) {
  key = newKey;
  arg = newArg;
}

OMXControlResult::OMXControlResult( int newKey, const char *newArg ) {
  key = newKey;
  winarg = newArg;
}

int OMXControlResult::getKey() {
  return key;
}

int64_t OMXControlResult::getArg() {
  return arg;
}

const char *OMXControlResult::getWinArg() {
  return winarg;
}

OMXControl::OMXControl() 
{

}

OMXControl::~OMXControl() 
{
    dbus_disconnect();
}

int OMXControl::init(OMXClock *m_av_clock, OMXPlayerAudio *m_player_audio, OMXPlayerSubtitles *m_player_subtitles, OMXReader *m_omx_reader, std::string& dbus_name)
{
  int ret = 0;
  clock     = m_av_clock;
  audio     = m_player_audio;
  subtitles = m_player_subtitles;
  reader    = m_omx_reader;

  if (dbus_connect(dbus_name) < 0)
  {
    CLog::Log(LOGWARNING, "DBus connection failed, trying alternate");
    dbus_disconnect();
    std::stringstream ss;
    ss << getpid();
    dbus_name += ".instance";
    dbus_name += ss.str();
    if (dbus_connect(dbus_name) < 0)
    {
      CLog::Log(LOGWARNING, "DBus connection failed, alternate failed, will continue without DBus");
      dbus_disconnect();
      ret = -1;
    } else {
      CLog::Log(LOGDEBUG, "DBus connection succeeded");
      dbus_threads_init_default();
    }
  }
  else
  {
    CLog::Log(LOGDEBUG, "DBus connection succeeded");
    dbus_threads_init_default();
  }
  return ret;
}

void OMXControl::dispatch()
{
  if (bus)
    dbus_connection_read_write_dispatch(bus, 0);
}

int OMXControl::dbus_connect(std::string& dbus_name)
{
  DBusError error;

  dbus_error_init(&error);
  if (!(bus = dbus_bus_get_private(DBUS_BUS_SESSION, &error)))
  {
    CLog::Log(LOGWARNING, "dbus_bus_get_private(): %s", error.message);
        goto fail;
  }

  dbus_connection_set_exit_on_disconnect(bus, FALSE);

  if (dbus_bus_request_name(
        bus,
        dbus_name.c_str(),
        DBUS_NAME_FLAG_DO_NOT_QUEUE,
        &error) != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
  {
        if (dbus_error_is_set(&error))
        {
            CLog::Log(LOGWARNING, "dbus_bus_request_name(): %s", error.message);
            goto fail;
        }

        CLog::Log(LOGWARNING, "Failed to acquire D-Bus name '%s'", dbus_name.c_str());
        goto fail;
    }

    return 0;

fail:
    if (dbus_error_is_set(&error))
        dbus_error_free(&error);

    if (bus)
    {
        dbus_connection_close(bus);
        dbus_connection_unref(bus);
        bus = NULL;
    }

    return -1;
}

void OMXControl::dbus_disconnect()
{
    if (bus)
    {
        dbus_connection_close(bus);
        dbus_connection_unref(bus);
        bus = NULL;
    }
}

OMXControlResult OMXControl::getEvent()
{
  if (!bus)
    return KeyConfig::ACTION_BLANK;

  dispatch();
  DBusMessage *m = dbus_connection_pop_message(bus);

  if (m == NULL)
    return KeyConfig::ACTION_BLANK;

  CLog::Log(LOGDEBUG, "Popped message member: %s interface: %s type: %d path: %s", dbus_message_get_member(m), dbus_message_get_interface(m), dbus_message_get_type(m), dbus_message_get_path(m) );

  if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_ROOT, "Quit"))
  {
    dbus_respond_ok(m);
    return KeyConfig::ACTION_EXIT;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "CanQuit")
      || dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "Fullscreen"))
  {
    dbus_respond_boolean(m, 1);
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "CanSetFullscreen")
      || dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "CanRaise")
      || dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "HasTrackList"))
  {
    dbus_respond_boolean(m, 0);
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "Identity"))
  {
    dbus_respond_string(m, "OMXPlayer");
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "SupportedUriSchemes"))
  {
    const char *UriSchemes[] = {"file", "http"};
    dbus_respond_array(m, UriSchemes, 2); // Array is of length 2
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "SupportedMimeTypes"))
  {
    const char *MimeTypes[] = {}; // Needs supplying
    dbus_respond_array(m, MimeTypes, 0);
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "CanGoNext")
        || dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "CanGoPrevious"))
  {
    dbus_respond_boolean(m, 0);
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "CanSeek"))
  {
    dbus_respond_boolean(m, reader->CanSeek());
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "CanControl")
        || dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "CanPlay")
        || dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "CanPause"))
  {
    dbus_respond_boolean(m, 1);
  }
  else if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_PLAYER, "Next"))
  {
    dbus_respond_ok(m);
    return KeyConfig::ACTION_NEXT_CHAPTER;
  }
  else if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_PLAYER, "Previous"))
  {
    dbus_respond_ok(m);
    return KeyConfig::ACTION_PREVIOUS_CHAPTER;
  }
  else if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_PLAYER, "Pause")
        || dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_PLAYER, "PlayPause"))
  {
    dbus_respond_ok(m);
    return KeyConfig::ACTION_PAUSE;
  }
  else if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_PLAYER, "Stop"))
  {
    dbus_respond_ok(m);
    return KeyConfig::ACTION_EXIT;
  }
  else if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_PLAYER, "Seek"))
  {
    DBusError error;
    dbus_error_init(&error);

    int64_t offset;
    dbus_message_get_args(m, &error, DBUS_TYPE_INT64, &offset, DBUS_TYPE_INVALID);

    // Make sure a value is sent for seeking
    if (dbus_error_is_set(&error))
    {
          CLog::Log(LOGWARNING, "Seek D-Bus Error: %s", error.message );
          dbus_error_free(&error);
          dbus_respond_ok(m);
          return KeyConfig::ACTION_BLANK;
    }
    else
    {
          dbus_respond_int64(m, offset);
          return OMXControlResult(KeyConfig::ACTION_SEEK_RELATIVE, offset);
    }
  }
  else if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_PLAYER, "SetPosition"))
    {
      DBusError error;
      dbus_error_init(&error);

      int64_t position;
      const char *oPath; // ignoring path right now because we don't have a playlist
      dbus_message_get_args(m, &error, DBUS_TYPE_OBJECT_PATH, &oPath, DBUS_TYPE_INT64, &position, DBUS_TYPE_INVALID);

      // Make sure a value is sent for setting position
      if (dbus_error_is_set(&error))
      {
            CLog::Log(LOGWARNING, "SetPosition D-Bus Error: %s", error.message );
            dbus_error_free(&error);
            dbus_respond_ok(m);
            return KeyConfig::ACTION_BLANK;
      }
      else
      {
            dbus_respond_int64(m, position);
            return OMXControlResult(KeyConfig::ACTION_SEEK_ABSOLUTE, position);
      }
    }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "PlaybackStatus"))
  {
    const char *status;
    if (clock->OMXIsPaused())
    {
      status = "Paused";
    }
    else
    {
      status = "Playing";
    }

    dbus_respond_string(m, status);
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "Volume"))
  {
    DBusError error;
    dbus_error_init(&error);

    double volume;
    dbus_message_get_args(m, &error, DBUS_TYPE_DOUBLE, &volume, DBUS_TYPE_INVALID);

    if (dbus_error_is_set(&error))
    { // i.e. Get current volume
      dbus_error_free(&error);
      dbus_respond_double(m, audio->GetVolume());
      return KeyConfig::ACTION_BLANK;
    }
    else
    {
      audio->SetVolume(volume);
      dbus_respond_double(m, volume);
      return KeyConfig::ACTION_BLANK;
    }
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "Balance"))
  {
    DBusError error;
    dbus_error_init(&error);

    double balance;
    dbus_message_get_args(m, &error, DBUS_TYPE_DOUBLE, &balance, DBUS_TYPE_INVALID);

    if (dbus_error_is_set(&error))
    {
      dbus_error_free(&error);
      dbus_respond_double(m, audio->GetBalance());
      return KeyConfig::ACTION_BLANK;
    }
    else
    {
      audio->SetBalance(balance);
      dbus_respond_double(m, balance);
      return KeyConfig::ACTION_BLANK;
    }
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "Mute"))
  {
    audio->SetMute(true);
    dbus_respond_ok(m);
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "Unmute"))
  {
    audio->SetMute(false);
    dbus_respond_ok(m);
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "Position"))
  {
    // Returns the current position in microseconds
    int64_t pos = clock->OMXMediaTime();
    dbus_respond_int64(m, pos);
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "Aspect"))
  {
    // Returns aspect ratio
    double ratio = reader->GetAspectRatio();
    dbus_respond_double(m, ratio);
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "VideoStreamCount"))
  {
    // Returns number of video streams
    int64_t vcount = reader->VideoStreamCount();
    dbus_respond_int64(m, vcount);
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "ResWidth"))
  {
    // Returns width of video
    int64_t width = reader->GetWidth();
    dbus_respond_int64(m, width);
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "ResHeight"))
  {
    // Returns height of video
    int64_t height = reader->GetHeight();
    dbus_respond_int64(m, height);
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "Duration"))
  {
    // Returns the duration in microseconds
    int64_t dur = reader->GetStreamLength();
    dur *= 1000; // ms -> us
    dbus_respond_int64(m, dur);
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "MinimumRate"))
  {
    dbus_respond_double(m, 0.0);
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "MaximumRate"))
  {
    dbus_respond_double(m, 1.125);
    return KeyConfig::ACTION_BLANK;

    // Implement extra OMXPlayer controls
  }
  else if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_PLAYER, "ListSubtitles"))
  {
    int count = reader->SubtitleStreamCount();
    char** values = new char*[count];

    for (int i=0; i < count; i++)
    {
       asprintf(&values[i], "%d:%s:%s:%s:%s", i,
                                              reader->GetStreamLanguage(OMXSTREAM_SUBTITLE, i).c_str(),
                                              reader->GetStreamName(OMXSTREAM_SUBTITLE, i).c_str(),
                                              reader->GetCodecName(OMXSTREAM_SUBTITLE, i).c_str(),
                                              ((int)subtitles->GetActiveStream() == i) ? "active" : "");
    }

    dbus_respond_array(m, (const char**)values, count);

    // Cleanup
    for (int i=0; i < count; i++)
    {
      delete[] values[i];
    }

    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_PLAYER, "VideoPos"))
  {
    DBusError error;
    dbus_error_init(&error);

    const char *win;
    const char *oPath; // ignoring path right now because we don't have a playlist
    dbus_message_get_args(m, &error, DBUS_TYPE_OBJECT_PATH, &oPath, DBUS_TYPE_STRING, &win, DBUS_TYPE_INVALID);

    // Make sure a value is sent for setting VideoPos
    if (dbus_error_is_set(&error))
    {
      CLog::Log(LOGWARNING, "VideoPos D-Bus Error: %s", error.message );
      dbus_error_free(&error);
      dbus_respond_ok(m);
      return KeyConfig::ACTION_BLANK;
    }
    else
    {
      dbus_respond_string(m, win);
      return OMXControlResult(KeyConfig::ACTION_MOVE_VIDEO, win);
    }
  }
  else if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_PLAYER, "HideVideo"))
  {
    dbus_respond_ok(m);
    return KeyConfig::ACTION_HIDE_VIDEO;
  }
  else if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_PLAYER, "UnHideVideo"))
  {
    dbus_respond_ok(m);
    return KeyConfig::ACTION_UNHIDE_VIDEO;
  }
  else if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_PLAYER, "ListAudio"))
  {
    int count = reader->AudioStreamCount();
    char** values = new char*[count];

    for (int i=0; i < count; i++)
    {
       asprintf(&values[i], "%d:%s:%s:%s:%s", i,
                                              reader->GetStreamLanguage(OMXSTREAM_AUDIO, i).c_str(),
                                              reader->GetStreamName(OMXSTREAM_AUDIO, i).c_str(),
                                              reader->GetCodecName(OMXSTREAM_AUDIO, i).c_str(),
                                              (reader->GetAudioIndex() == i) ? "active" : "");
    }

    dbus_respond_array(m, (const char**)values, count);

    // Cleanup
    for (int i=0; i < count; i++)
    {
      delete[] values[i];
    }

    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_PLAYER, "ListVideo"))
  {
    int count = reader->AudioStreamCount();
    char** values = new char*[count];

    for (int i=0; i < count; i++)
    {
       asprintf(&values[i], "%d:%s:%s:%s:%s", i,
                                              reader->GetStreamLanguage(OMXSTREAM_VIDEO, i).c_str(),
                                              reader->GetStreamName(OMXSTREAM_VIDEO, i).c_str(),
                                              reader->GetCodecName(OMXSTREAM_VIDEO, i).c_str(),
                                              (reader->GetVideoIndex() == i) ? "active" : "");
    }

    dbus_respond_array(m, (const char**)values, count);

    // Cleanup
    for (int i=0; i < count; i++)
    {
      delete[] values[i];
    }

    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_PLAYER, "SelectSubtitle"))
  {
    DBusError error;
    dbus_error_init(&error);

    int index;
    dbus_message_get_args(m, &error, DBUS_TYPE_INT32, &index, DBUS_TYPE_INVALID);

    if (dbus_error_is_set(&error))
    {
      dbus_error_free(&error);
      dbus_respond_boolean(m, 0);
    }
    else
    {
      if (reader->SetActiveStream(OMXSTREAM_SUBTITLE, index))
      {
        subtitles->SetActiveStream(reader->GetSubtitleIndex());
        dbus_respond_boolean(m, 1);
      }
      else {
        dbus_respond_boolean(m, 0);
      }
    }
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_PLAYER, "SelectAudio"))
  {
    DBusError error;
    dbus_error_init(&error);

    int index;
    dbus_message_get_args(m, &error, DBUS_TYPE_INT32, &index, DBUS_TYPE_INVALID);

    if (dbus_error_is_set(&error))
    {
      dbus_error_free(&error);
      dbus_respond_boolean(m, 0);
    }
    else
    {
      if (reader->SetActiveStream(OMXSTREAM_AUDIO, index))
      {
        dbus_respond_boolean(m, 1);
      }
      else {
        dbus_respond_boolean(m, 0);
      }
    }
    return KeyConfig::ACTION_BLANK;
  }
  // TODO: SelectVideo ???
  else if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_PLAYER, "ShowSubtitles"))
  {
    subtitles->SetVisible(true);
    dbus_respond_ok(m);
    return KeyConfig::ACTION_SHOW_SUBTITLES;
  }
  else if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_PLAYER, "HideSubtitles"))
  {
    subtitles->SetVisible(false);
    dbus_respond_ok(m);
    return KeyConfig::ACTION_HIDE_SUBTITLES;
  }
  else if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_PLAYER, "Action"))
  {
    DBusError error;
    dbus_error_init(&error);

    int action;
    dbus_message_get_args(m, &error, DBUS_TYPE_INT32, &action, DBUS_TYPE_INVALID);

    if (dbus_error_is_set(&error))
    {
      dbus_error_free(&error);
      dbus_respond_ok(m);
      return KeyConfig::ACTION_BLANK;
    }
    else
    {
      dbus_respond_ok(m);
      return action; // Directly return enum
    }
  }
  else {
    CLog::Log(LOGWARNING, "Unhandled dbus message, member: %s interface: %s type: %d path: %s", dbus_message_get_member(m), dbus_message_get_interface(m), dbus_message_get_type(m), dbus_message_get_path(m) );
  }

  return KeyConfig::ACTION_BLANK;
}

DBusHandlerResult OMXControl::dbus_respond_ok(DBusMessage *m)
{
  DBusMessage *reply;

  reply = dbus_message_new_method_return(m);

  if (!reply)
    return DBUS_HANDLER_RESULT_NEED_MEMORY;

  dbus_connection_send(bus, reply, NULL);
  dbus_message_unref(reply);

  return DBUS_HANDLER_RESULT_HANDLED;
}

DBusHandlerResult OMXControl::dbus_respond_string(DBusMessage *m, const char *text)
{
  DBusMessage *reply;

  reply = dbus_message_new_method_return(m);

  if (!reply)
  {
    CLog::Log(LOGWARNING, "Failed to allocate message");
    return DBUS_HANDLER_RESULT_NEED_MEMORY;
  }

  dbus_message_append_args(reply, DBUS_TYPE_STRING, &text, DBUS_TYPE_INVALID);
  dbus_connection_send(bus, reply, NULL);
  dbus_message_unref(reply);

  return DBUS_HANDLER_RESULT_HANDLED;
}

DBusHandlerResult OMXControl::dbus_respond_int64(DBusMessage *m, int64_t i)
{
  DBusMessage *reply;

  reply = dbus_message_new_method_return(m);

  if (!reply)
  {
    CLog::Log(LOGWARNING, "Failed to allocate message");
    return DBUS_HANDLER_RESULT_NEED_MEMORY;
  }

  dbus_message_append_args(reply, DBUS_TYPE_INT64, &i, DBUS_TYPE_INVALID);
  dbus_connection_send(bus, reply, NULL);
  dbus_message_unref(reply);

  return DBUS_HANDLER_RESULT_HANDLED;
}

DBusHandlerResult OMXControl::dbus_respond_double(DBusMessage *m, double d)
{
  DBusMessage *reply;

  reply = dbus_message_new_method_return(m);

  if (!reply) 
  {
    CLog::Log(LOGWARNING, "Failed to allocate message");
    return DBUS_HANDLER_RESULT_NEED_MEMORY;
  }

  dbus_message_append_args(reply, DBUS_TYPE_DOUBLE, &d, DBUS_TYPE_INVALID);
  dbus_connection_send(bus, reply, NULL);
  dbus_message_unref(reply);

  return DBUS_HANDLER_RESULT_HANDLED;
}

DBusHandlerResult OMXControl::dbus_respond_boolean(DBusMessage *m, int b)
{
  DBusMessage *reply;

  reply = dbus_message_new_method_return(m);

  if (!reply)
  {
    CLog::Log(LOGWARNING, "Failed to allocate message");
    return DBUS_HANDLER_RESULT_NEED_MEMORY;
  }

  dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &b, DBUS_TYPE_INVALID);
  dbus_connection_send(bus, reply, NULL);
  dbus_message_unref(reply);

  return DBUS_HANDLER_RESULT_HANDLED;
}

DBusHandlerResult OMXControl::dbus_respond_array(DBusMessage *m, const char *array[], int size)
{
  DBusMessage *reply;

  reply = dbus_message_new_method_return(m);

  if (!reply)
  {
    CLog::Log(LOGWARNING, "Failed to allocate message");
    return DBUS_HANDLER_RESULT_NEED_MEMORY;
  }

  dbus_message_append_args(reply, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &array, size, DBUS_TYPE_INVALID);
  dbus_connection_send(bus, reply, NULL);
  dbus_message_unref(reply);

  return DBUS_HANDLER_RESULT_HANDLED;
}
