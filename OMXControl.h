#define OMXPLAYER_DBUS_NAME "org.mpris.MediaPlayer2.omxplayer"
#define OMXPLAYER_DBUS_PATH_SERVER "/org/mpris/MediaPlayer2"  
#define OMXPLAYER_DBUS_INTERFACE_ROOT "org.mpris.MediaPlayer2"
#define OMXPLAYER_DBUS_INTERFACE_PLAYER "org.mpris.MediaPlayer2.Player"

#include <dbus/dbus.h>
#include "OMXClock.h"
#include "OMXPlayerAudio.h"

class OMXControl
{
protected:
  DBusConnection *bus;
  OMXClock       *clock;
  OMXPlayerAudio *audio;
public:
  OMXControl();
  ~OMXControl();
  void init(OMXClock *m_av_clock, OMXPlayerAudio *m_player_audio);
  int getEvent();
  void dispatch();
private:
  int dbus_connect();
  void dbus_disconnect();
  DBusHandlerResult dbus_respond_ok(DBusMessage *m);
  DBusHandlerResult dbus_respond_int64(DBusMessage *m, int64_t i);
  DBusHandlerResult dbus_respond_double(DBusMessage *m, double d);
  DBusHandlerResult dbus_respond_boolean(DBusMessage *m, int b);
  DBusHandlerResult dbus_respond_string(DBusMessage *m, const char *text);
  DBusHandlerResult dbus_respond_array(DBusMessage *m, const char *array[], int size);
};

