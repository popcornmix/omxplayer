#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <string.h>
#include <dbus/dbus.h>

#include <string>
#include <utility>

#include "utils/log.h"
#include "OMXControl.h"
#include "KeyConfig.h"

#define CLASSNAME "OMXControl"

OMXControl::OMXControl() 
{
  if (dbus_connect() < 0) 
  {
    CLog::Log(LOGWARNING, "DBus connection failed");
  } 
  else 
  {
    CLog::Log(LOGDEBUG, "DBus connection succeeded");
  }

  dbus_threads_init_default();
}

OMXControl::~OMXControl() 
{
    dbus_disconnect();
}

void OMXControl::init(OMXClock *m_av_clock, OMXPlayerAudio *m_player_audio) 
{
  clock = m_av_clock;
  audio = m_player_audio;
}
void OMXControl::dispatch() 
{
  if (bus)
    dbus_connection_read_write_dispatch(bus, 0);
}

int OMXControl::dbus_connect() 
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
        OMXPLAYER_DBUS_NAME,
        DBUS_NAME_FLAG_DO_NOT_QUEUE,
        &error) != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) 
  {
        if (dbus_error_is_set(&error)) 
        {
            CLog::Log(LOGWARNING, "dbus_bus_request_name(): %s", error.message);
            goto fail;
        }

        CLog::Log(LOGWARNING, "Failed to acquire D-Bus name '%s'", OMXPLAYER_DBUS_NAME);
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

int OMXControl::getEvent() 
{
  if (!bus)
    return KeyConfig::ACTION_BLANK;
  
  dispatch();
  DBusMessage *m = dbus_connection_pop_message(bus);

  if (m == NULL) 
    return KeyConfig::ACTION_BLANK;
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
        || dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "CanGoPrevious")
        || dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "CanSeek")) 
  {
    dbus_respond_boolean(m, 0);
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
  else if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_PLAYER, "Pause")) 
  {
    dbus_respond_ok(m);
    return KeyConfig::ACTION_PAUSE;
  } 
  else if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_PLAYER, "PlayPause")) 
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

    long offset;
    dbus_message_get_args(m, &error, DBUS_TYPE_INT64, &offset, DBUS_TYPE_INVALID);

    // Make sure a value is sent for seeking
    if (dbus_error_is_set(&error)) 
    {
          dbus_error_free(&error);
          dbus_respond_ok(m);
          return KeyConfig::ACTION_BLANK;
    } 
    else 
    {
      dbus_respond_int64(m, offset);
      if (offset < 0) 
      {
        return KeyConfig::ACTION_SEEK_BACK_SMALL;
      } 
      else if (offset > 0) 
      {
        return KeyConfig::ACTION_SEEK_FORWARD_SMALL;
      }
    }
    return KeyConfig::ACTION_BLANK;
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

    double vol;
    dbus_message_get_args(m, &error, DBUS_TYPE_DOUBLE, &vol, DBUS_TYPE_INVALID);

    if (dbus_error_is_set(&error)) 
    { // i.e. Get current volume
      dbus_error_free(&error);
      long volume = audio->GetVolume(); // Volume in millibels
      double r = pow(10, volume / 2000.0);
      dbus_respond_double(m, r);
      return KeyConfig::ACTION_BLANK;
    } 
    else 
    {
      long volume = static_cast<long>(2000.0 * log10(vol));
      audio->SetVolume(volume);
      dbus_respond_ok(m);
      return KeyConfig::ACTION_BLANK;
    }
  } 
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "Time_In_Us")) 
  {
    long pos = clock->OMXMediaTime();
    dbus_respond_int64(m, pos);
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
