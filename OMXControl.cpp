#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include <string.h>
#include <dbus/dbus.h>

#include <string>
#include <sstream>
#include <utility>

#include "utils/log.h"
#include "OMXControl.h"
#include "KeyConfig.h"


void ToURI(const std::string& str, char *uri)
{
  //Test if URL/URI
  bool isURL=true;
  auto result = str.find("://");
  if(result == std::string::npos || result == 0)
  {
    isURL=false;
  }
  
  if(isURL)
  {
    for(size_t i = 0; i < result; ++i)
    {
      if(!isalpha(str[i]))
        isURL=false;
    }
  }
  
  //Build URI if needed
  if(isURL)
  {
    //Just write URL as it is
    strncpy(uri, str.c_str(), PATH_MAX);
  }
  else
  {
    //Get file full path and add file://
    char * real_path=realpath(str.c_str(), NULL);
    sprintf(uri, "file://%s", real_path);
    free(real_path);
  }
}

void deprecatedMessage()
{
  CLog::Log(LOGWARNING, "DBus property access through direct method is deprecated. Use Get/Set methods instead.");
}


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
    dbus_connection_read_write(bus, 0);
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


  //----------------------------DBus root interface-----------------------------
  //Methods:
  if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_ROOT, "Quit"))
  {
    dbus_respond_ok(m);//Note: No reply according to MPRIS2 specs
    return KeyConfig::ACTION_EXIT;
  }
  else if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_ROOT, "Raise"))
  {
    //Does nothing
    return KeyConfig::ACTION_BLANK;
  }
  //Properties Get method:
  //TODO: implement GetAll
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "Get"))
  {
    DBusError error;
    dbus_error_init(&error);

    //Retrieve interface and property name
    const char *interface, *property;
    dbus_message_get_args(m, &error, DBUS_TYPE_STRING, &interface, DBUS_TYPE_STRING, &property, DBUS_TYPE_INVALID);
    //Root interface:
    if (strcmp(interface, OMXPLAYER_DBUS_INTERFACE_ROOT)==0)
    {
      if (strcmp(property, "CanRaise")==0)
      {
        dbus_respond_boolean(m, 0);
        return KeyConfig::ACTION_BLANK;
      }
      else if (strcmp(property, "CanQuit")==0)
      {
        dbus_respond_boolean(m, 1);
        return KeyConfig::ACTION_BLANK;
      }
      else if (strcmp(property, "CanSetFullscreen")==0)
      {
        dbus_respond_boolean(m, 0);
        return KeyConfig::ACTION_BLANK;
      }
      else if (strcmp(property, "Fullscreen")==0) //Fullscreen is read/write in theory not read only, but read only at the moment so...
      {
        dbus_respond_boolean(m, 1);
        return KeyConfig::ACTION_BLANK;
      }
      else if (strcmp(property, "HasTrackList")==0)
      {
        dbus_respond_boolean(m, 0);
        return KeyConfig::ACTION_BLANK;
      }
      else if (strcmp(property, "Identity")==0)
      {
        dbus_respond_string(m, "OMXPlayer");
        return KeyConfig::ACTION_BLANK;
      }
      else if (strcmp(property, "SupportedUriSchemes")==0) //TODO: Update ?
      {
        const char *UriSchemes[] = {"file", "http", "rtsp", "rtmp"};
        dbus_respond_array(m, UriSchemes, 4); // Array is of length 4
        return KeyConfig::ACTION_BLANK;
      }
      else if (strcmp(property, "SupportedMimeTypes")==0) //Vinc: TODO: Minimal list of supported types based on ffmpeg minimal support ?
      {
        const char *MimeTypes[] = {}; // Needs supplying
        dbus_respond_array(m, MimeTypes, 0);
        return KeyConfig::ACTION_BLANK;
      }
      //Wrong property
      else
      {
        //Error
        CLog::Log(LOGWARNING, "Unhandled dbus property message, member: %s interface: %s type: %d path: %s property: %s", dbus_message_get_member(m), dbus_message_get_interface(m), dbus_message_get_type(m), dbus_message_get_path(m), property );
        return KeyConfig::ACTION_BLANK;
      }
    }
    //Player interface:
    else if (strcmp(interface, OMXPLAYER_DBUS_INTERFACE_PLAYER)==0)
    {
      //MPRIS2 properties:
      if (strcmp(property, "CanGoNext")==0)
      {
        dbus_respond_boolean(m, 0);
        return KeyConfig::ACTION_BLANK;
      }
      else if (strcmp(property, "CanGoPrevious")==0)
      {
        dbus_respond_boolean(m, 0);
        return KeyConfig::ACTION_BLANK;
      }
      else if (strcmp(property, "CanSeek")==0)
      {
        dbus_respond_boolean(m, reader->CanSeek());
        return KeyConfig::ACTION_BLANK;
      }
      else if (strcmp(property, "CanControl")==0)
      {
        dbus_respond_boolean(m, 1);
        return KeyConfig::ACTION_BLANK;
      }
      else if (strcmp(property, "CanPlay")==0)
      {
        dbus_respond_boolean(m, 1);
        return KeyConfig::ACTION_BLANK;
      }
      else if (strcmp(property, "CanPause")==0)
      {
        dbus_respond_boolean(m, 1);
        return KeyConfig::ACTION_BLANK;
      }
      else if (strcmp(property, "Position")==0)
      {
        // Returns the current position in microseconds
        int64_t pos = clock->OMXMediaTime();
        dbus_respond_int64(m, pos);
        return KeyConfig::ACTION_BLANK;
      }
      else if (strcmp(property, "PlaybackStatus")==0)
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
      else if (strcmp(property, "MinimumRate")==0)
      {
        dbus_respond_double(m, (MIN_RATE)/1000.);
        return KeyConfig::ACTION_BLANK;
      }
      else if (strcmp(property, "MaximumRate")==0)
      {
        dbus_respond_double(m, (MAX_RATE)/1000.);
        return KeyConfig::ACTION_BLANK;
      }
      else if (strcmp(property, "Rate")==0)
      {
        //return current playing rate
        dbus_respond_double(m, (double)clock->OMXPlaySpeed()/1000.);
        return KeyConfig::ACTION_BLANK;
      }
      else if (strcmp(property, "Volume")==0)
      {
        //return current volume
        dbus_respond_double(m, audio->GetVolume());
        return KeyConfig::ACTION_BLANK;
      }
      else if (strcmp(property, "Metadata")==0)
      {
        DBusMessage *reply;
        reply = dbus_message_new_method_return(m);
        if(reply)
        {
          //Create iterator: Array of dict entries, composed of string (key)) and variant (value)
          DBusMessageIter array_cont, dict_cont, dict_entry_cont, var;
          dbus_message_iter_init_append(reply, &array_cont);
          dbus_message_iter_open_container(&array_cont, DBUS_TYPE_ARRAY, "{sv}", &dict_cont);
            //First dict entry: URI
            const char *key1 = "xesam:url";
            char uri[PATH_MAX+7];
            ToURI(reader->getFilename(), uri);
            const char *value1=uri;
            dbus_message_iter_open_container(&dict_cont, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry_cont);
              dbus_message_iter_append_basic(&dict_entry_cont, DBUS_TYPE_STRING, &key1);
              dbus_message_iter_open_container(&dict_entry_cont, DBUS_TYPE_VARIANT, DBUS_TYPE_STRING_AS_STRING, &var);
              dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &value1);
              dbus_message_iter_close_container(&dict_entry_cont, &var);
            dbus_message_iter_close_container(&dict_cont, &dict_entry_cont);
            //Second dict entry: duration in us
            const char *key2 = "mpris:length";
            dbus_int64_t value2 = reader->GetStreamLength()*1000;
            reader->GetStreamLength();
            dbus_message_iter_open_container(&dict_cont, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry_cont);
              dbus_message_iter_append_basic(&dict_entry_cont, DBUS_TYPE_STRING, &key2);
              dbus_message_iter_open_container(&dict_entry_cont, DBUS_TYPE_VARIANT, DBUS_TYPE_INT64_AS_STRING, &var);
              dbus_message_iter_append_basic(&var, DBUS_TYPE_INT64, &value2);
              dbus_message_iter_close_container(&dict_entry_cont, &var);
            dbus_message_iter_close_container(&dict_cont, &dict_entry_cont);
          dbus_message_iter_close_container(&array_cont, &dict_cont);
          //Send message
          dbus_connection_send(bus, reply, NULL);
          dbus_message_unref(reply);
        }
        return KeyConfig::ACTION_BLANK;
      }
      
      //Non-MPRIS2 properties:
      else if (strcmp(property, "Aspect")==0)
      {
        // Returns aspect ratio
        double ratio = reader->GetAspectRatio();
        dbus_respond_double(m, ratio);
        return KeyConfig::ACTION_BLANK;
      }
      else if (strcmp(property, "VideoStreamCount")==0)
      {
        // Returns number of video streams
        int64_t vcount = reader->VideoStreamCount();
        dbus_respond_int64(m, vcount);
        return KeyConfig::ACTION_BLANK;
      }
      else if (strcmp(property, "ResWidth")==0)
      {
        // Returns width of video
        int64_t width = reader->GetWidth();
        dbus_respond_int64(m, width);
        return KeyConfig::ACTION_BLANK;
      }
      else if (strcmp(property, "ResHeight")==0)
      {
        // Returns height of video
        int64_t height = reader->GetHeight();
        dbus_respond_int64(m, height);
        return KeyConfig::ACTION_BLANK;
      }
      else if (strcmp(property,  "Duration")==0)
      {
        // Returns the duration in microseconds
        int64_t dur = reader->GetStreamLength();
        dur *= 1000; // ms -> us
        dbus_respond_int64(m, dur);
        return KeyConfig::ACTION_BLANK;
      }
      //Wrong property
      else
      {
        //Error
        CLog::Log(LOGWARNING, "Unhandled dbus property message, member: %s interface: %s type: %d path: %s  property: %s", dbus_message_get_member(m), dbus_message_get_interface(m), dbus_message_get_type(m), dbus_message_get_path(m), property );
        return KeyConfig::ACTION_BLANK;
      }
    }
    //Wrong interface:
    else
    {
        //Error
        CLog::Log(LOGWARNING, "Unhandled dbus message, member: %s interface: %s type: %d path: %s", dbus_message_get_member(m), dbus_message_get_interface(m), dbus_message_get_type(m), dbus_message_get_path(m) );
        return KeyConfig::ACTION_BLANK;
    }
  }
  //Properties Set method:
  //TODO: implement signal generation on some property changes
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "Set"))
  {
    DBusError error;
    dbus_error_init(&error);

    //Retrieve interface, property name and value
    //Message has the form message[STRING:interface STRING:property DOUBLE:value] or message[STRING:interface STRING:property VARIANT[DOUBLE:value]]
    const char *interface, *property;
    double new_property_value;
    DBusMessageIter args;
    dbus_message_iter_init(m, &args);
    if(dbus_message_iter_has_next(&args))
    {
		//The interface name
		if( DBUS_TYPE_STRING == dbus_message_iter_get_arg_type(&args) ) 
			dbus_message_iter_get_basic (&args, &interface);
		else
		{
			printf("setE1\n");
			CLog::Log(LOGWARNING, "Unhandled dbus message, member: %s interface: %s type: %d path: %s", dbus_message_get_member(m), dbus_message_get_interface(m), dbus_message_get_type(m), dbus_message_get_path(m) );
			dbus_error_free(&error);
			return KeyConfig::ACTION_BLANK;
		}
		//The property name
		if( dbus_message_iter_next(&args) && DBUS_TYPE_STRING == dbus_message_iter_get_arg_type(&args) )
			dbus_message_iter_get_basic (&args, &property);
		else
		{
			CLog::Log(LOGWARNING, "Unhandled dbus message, member: %s interface: %s type: %d path: %s", dbus_message_get_member(m), dbus_message_get_interface(m), dbus_message_get_type(m), dbus_message_get_path(m) );
			dbus_error_free(&error);
			return KeyConfig::ACTION_BLANK;
		}
		//The value (either double or double in variant)
		if (dbus_message_iter_next(&args))
		{
			//Simply a double
			if (DBUS_TYPE_DOUBLE == dbus_message_iter_get_arg_type(&args))
			{
				dbus_message_iter_get_basic(&args, &new_property_value);
			}
			//A double within a variant
			else if(DBUS_TYPE_VARIANT == dbus_message_iter_get_arg_type(&args))
			{
				DBusMessageIter variant;
				dbus_message_iter_recurse(&args, &variant);
				if(DBUS_TYPE_DOUBLE == dbus_message_iter_get_arg_type(&variant))
				{
					dbus_message_iter_get_basic(&variant, &new_property_value);
				}
			}
			else
			{
				CLog::Log(LOGWARNING, "Unhandled dbus message, member: %s interface: %s type: %d path: %s", dbus_message_get_member(m), dbus_message_get_interface(m), dbus_message_get_type(m), dbus_message_get_path(m) );
				dbus_error_free(&error);
				return KeyConfig::ACTION_BLANK;
			}
		}
	}
    if ( dbus_error_is_set(&error) )
    {
        CLog::Log(LOGWARNING, "Unhandled dbus message, member: %s interface: %s type: %d path: %s", dbus_message_get_member(m), dbus_message_get_interface(m), dbus_message_get_type(m), dbus_message_get_path(m) );
        dbus_error_free(&error);
        return KeyConfig::ACTION_BLANK;
    }
    //Player interface:
    if (strcmp(interface, OMXPLAYER_DBUS_INTERFACE_PLAYER)==0)
    {
      if (strcmp(property, "Volume")==0)
      {
        double volume=new_property_value;
        //Min value is 0
        if(volume<.0)
        {
          volume=.0;
        }
        audio->SetVolume(volume);
        dbus_respond_double(m, volume);
        return KeyConfig::ACTION_BLANK;
      }
      else if (strcmp(property, "Rate")==0)
      {
        double rate=new_property_value;
        if(rate>MAX_RATE/1000.)
        {
          rate=MAX_RATE/1000.;
        }
        if(rate<MIN_RATE/1000.)
        {
          //Set to Pause according to MPRIS2 specs (no actual change of playing rate)
          dbus_respond_double(m, (double)clock->OMXPlaySpeed()/1000.);
          return KeyConfig::ACTION_PAUSE;
        }
        int iSpeed=(int)(rate*1000.);
        if(!clock)
        {
          dbus_respond_double(m, .0);//What value ????
          return KeyConfig::ACTION_BLANK;
        }
        //Can't do trickplay here so limit max speed
        if(iSpeed > MAX_RATE)
          iSpeed=MAX_RATE;
        dbus_respond_double(m, iSpeed/1000.);//Reply before applying to be faster
        clock->OMXSetSpeed(iSpeed, false, true);
        return KeyConfig::ACTION_PLAY;
      }
      //Wrong property
      else
      {
        //Error
        CLog::Log(LOGWARNING, "Unhandled dbus property message, member: %s interface: %s type: %d path: %s  property: %s", dbus_message_get_member(m), dbus_message_get_interface(m), dbus_message_get_type(m), dbus_message_get_path(m), property );
        return KeyConfig::ACTION_BLANK;
      }
    }
    //Wrong interface:
    else
    {
        //Error
        CLog::Log(LOGWARNING, "Unhandled dbus message, member: %s interface: %s type: %d path: %s", dbus_message_get_member(m), dbus_message_get_interface(m), dbus_message_get_type(m), dbus_message_get_path(m) );
        return KeyConfig::ACTION_BLANK;
    }
  }
  //----------------------------------------------------------------------------


  //-------------------------DEPRECATED PROPERTIES METHODS----------------------
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "CanQuit")
      || dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "Fullscreen"))
  {
    dbus_respond_boolean(m, 1);
    deprecatedMessage();
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "CanSetFullscreen")
      || dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "CanRaise")
      || dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "HasTrackList"))
  {
    dbus_respond_boolean(m, 0);
    deprecatedMessage();
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "Identity"))
  {
    dbus_respond_string(m, "OMXPlayer");
    deprecatedMessage();
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "SupportedUriSchemes"))
  {
    const char *UriSchemes[] = {"file", "http", "rtsp", "rtmp"};
    dbus_respond_array(m, UriSchemes, 2); // Array is of length 2
    deprecatedMessage();
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "SupportedMimeTypes"))
  {
    const char *MimeTypes[] = {}; // Needs supplying
    dbus_respond_array(m, MimeTypes, 0);
    deprecatedMessage();
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "CanGoNext")
        || dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "CanGoPrevious"))
  {
    dbus_respond_boolean(m, 0);
    deprecatedMessage();
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "CanSeek"))
  {
    dbus_respond_boolean(m, reader->CanSeek());
    deprecatedMessage();
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "CanControl")
        || dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "CanPlay")
        || dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "CanPause"))
  {
    dbus_respond_boolean(m, 1);
    deprecatedMessage();
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
    deprecatedMessage();
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "GetSource"))
  {
    dbus_respond_string(m, reader->getFilename().c_str());
    deprecatedMessage();
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
      deprecatedMessage();
      return KeyConfig::ACTION_BLANK;
    }
    else
    {
      //Min value is 0
      if(volume<.0)
      {
        volume=.0;
      }
      audio->SetVolume(volume);
      dbus_respond_double(m, volume);
      deprecatedMessage();
      return KeyConfig::ACTION_BLANK;
    }
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "Mute"))
  {
    audio->SetMute(true);
    dbus_respond_ok(m);
    deprecatedMessage();
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "Unmute"))
  {
    audio->SetMute(false);
    dbus_respond_ok(m);
    deprecatedMessage();
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "Position"))
  {
    // Returns the current position in microseconds
    int64_t pos = clock->OMXMediaTime();
    dbus_respond_int64(m, pos);
    deprecatedMessage();
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "Aspect"))
  {
    // Returns aspect ratio
    double ratio = reader->GetAspectRatio();
    dbus_respond_double(m, ratio);
    deprecatedMessage();
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "VideoStreamCount"))
  {
    // Returns number of video streams
    int64_t vcount = reader->VideoStreamCount();
    dbus_respond_int64(m, vcount);
    deprecatedMessage();
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "ResWidth"))
  {
    // Returns width of video
    int64_t width = reader->GetWidth();
    dbus_respond_int64(m, width);
    deprecatedMessage();
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "ResHeight"))
  {
    // Returns height of video
    int64_t height = reader->GetHeight();
    dbus_respond_int64(m, height);
    deprecatedMessage();
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "Duration"))
  {
    // Returns the duration in microseconds
    int64_t dur = reader->GetStreamLength();
    dur *= 1000; // ms -> us
    dbus_respond_int64(m, dur);
    deprecatedMessage();
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "MinimumRate"))
  {
    dbus_respond_double(m, 0.0);
    deprecatedMessage();
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, DBUS_INTERFACE_PROPERTIES, "MaximumRate"))
  {
    //TODO: to be made consistent
    dbus_respond_double(m, 10.125);
    deprecatedMessage();
    return KeyConfig::ACTION_BLANK;
  }
  //----------------------------------------------------------------------------


  //--------------------------Player interface methods--------------------------
  else if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_PLAYER, "GetSource"))
  {
    dbus_respond_string(m, reader->getFilename().c_str());
    return KeyConfig::ACTION_BLANK;
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
  else if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_PLAYER, "Play"))
  {
    dbus_respond_ok(m);
    return KeyConfig::ACTION_PLAY;
  }
  else if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_PLAYER, "PlayPause"))
  {
    dbus_respond_ok(m);
    return KeyConfig::ACTION_PLAYPAUSE;
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
  else if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_PLAYER, "SetAlpha"))
  {
    DBusError error;
    dbus_error_init(&error);

    int64_t alpha;
    const char *oPath; // ignoring path right now because we don't have a playlist
    dbus_message_get_args(m, &error, DBUS_TYPE_OBJECT_PATH, &oPath, DBUS_TYPE_INT64, &alpha, DBUS_TYPE_INVALID);

    // Make sure a value is sent for setting alpha
    if (dbus_error_is_set(&error))
    {
      CLog::Log(LOGWARNING, "SetAlpha D-Bus Error: %s", error.message );
      dbus_error_free(&error);
      dbus_respond_ok(m);
      return KeyConfig::ACTION_BLANK;
    }
    else
    {
      dbus_respond_int64(m, alpha);
      return OMXControlResult(KeyConfig::ACTION_SET_ALPHA, alpha);
    }
  }
  else if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_PLAYER, "SetAspectMode"))
  {
    DBusError error;
    dbus_error_init(&error);

    const char *aspectMode;
    const char *oPath; // ignoring path right now because we don't have a playlist
    dbus_message_get_args(m, &error, DBUS_TYPE_OBJECT_PATH, &oPath, DBUS_TYPE_STRING, &aspectMode, DBUS_TYPE_INVALID);

    // Make sure a value is sent for setting aspect mode
    if (dbus_error_is_set(&error))
    {
      CLog::Log(LOGWARNING, "SetAspectMode D-Bus Error: %s", error.message );
      dbus_error_free(&error);
      dbus_respond_ok(m);
      return KeyConfig::ACTION_BLANK;
    }
    else
    {
      dbus_respond_string(m, aspectMode);
      return OMXControlResult(KeyConfig::ACTION_SET_ASPECT_MODE, aspectMode);
    }
  }
  else if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_PLAYER, "Mute"))
  {
    audio->SetMute(true);
    dbus_respond_ok(m);
    return KeyConfig::ACTION_BLANK;
  }
  else if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_PLAYER, "Unmute"))
  {
    audio->SetMute(false);
    dbus_respond_ok(m);
    return KeyConfig::ACTION_BLANK;
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
  else if (dbus_message_is_method_call(m, OMXPLAYER_DBUS_INTERFACE_PLAYER, "SetVideoCropPos"))
  {
    DBusError error;
    dbus_error_init(&error);

    const char *crop;
    const char *oPath; // ignoring path right now because we don't have a playlist
    dbus_message_get_args(m, &error, DBUS_TYPE_OBJECT_PATH, &oPath, DBUS_TYPE_STRING, &crop, DBUS_TYPE_INVALID);

    // Make sure a value is sent for setting SetVideoCropPos
    if (dbus_error_is_set(&error))
    {
      CLog::Log(LOGWARNING, "SetVideoCropPos D-Bus Error: %s", error.message );
      dbus_error_free(&error);
      dbus_respond_ok(m);
      return KeyConfig::ACTION_BLANK;
    }
    else
    {
      dbus_respond_string(m, crop);
      return OMXControlResult(KeyConfig::ACTION_CROP_VIDEO, crop);
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
  //----------------------------------------------------------------------------
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
