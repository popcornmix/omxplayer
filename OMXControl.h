#define OMXPLAYER_DBUS_PATH_SERVER "/org/mpris/MediaPlayer2"  
#define OMXPLAYER_DBUS_INTERFACE_ROOT "org.mpris.MediaPlayer2"
#define OMXPLAYER_DBUS_INTERFACE_PLAYER "org.mpris.MediaPlayer2.Player"

#include <dbus/dbus.h>
#include "OMXClock.h"
#include "OMXPlayerAudio.h"
#include "OMXPlayerVideo.h"
#include "OMXPlayerSubtitles.h"

class OMXControlResult {
  int key;
  int64_t arg;
  const char *winarg;

public:
   OMXControlResult(int);
   OMXControlResult(int, int64_t);
   OMXControlResult(int, const char *);
   int getKey();
   int64_t getArg();
   const char *getWinArg();
};

class OMXControl
{
protected:
  DBusConnection     *bus;
  OMXClock           *clock;
  OMXPlayerAudio     *audio;
  OMXPlayerVideo     *video;
  OMXReader          *reader;
  OMXPlayerSubtitles *subtitles;
public:
  OMXControl();
  ~OMXControl();
  int init(OMXClock *m_av_clock, OMXPlayerAudio *m_player_audio, OMXPlayerVideo *m_player_video, OMXPlayerSubtitles *m_player_subtitles, OMXReader *m_omx_reader, std::string& dbus_name);
  OMXControlResult getEvent();
  void dispatch();
private:
  int dbus_connect(std::string& dbus_name);
  void dbus_disconnect();
  DBusHandlerResult dbus_respond_ok(DBusMessage *m);
  DBusHandlerResult dbus_respond_int64(DBusMessage *m, int64_t i);
  DBusHandlerResult dbus_respond_double(DBusMessage *m, double d);
  DBusHandlerResult dbus_respond_boolean(DBusMessage *m, int b);
  DBusHandlerResult dbus_respond_string(DBusMessage *m, const char *text);
  DBusHandlerResult dbus_respond_array(DBusMessage *m, const char *array[], int size);
};
