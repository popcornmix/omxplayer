#define OMXPLAYER_DBUS_PATH_SERVER "/org/mpris/MediaPlayer2"  
#define OMXPLAYER_DBUS_INTERFACE_ROOT "org.mpris.MediaPlayer2"
#define OMXPLAYER_DBUS_INTERFACE_PLAYER "org.mpris.MediaPlayer2.Player"

#include "OMXThread.h"
#include <map>

 class Keyboard : public OMXThread
 {
 protected:
  struct termios orig_termios;
  int orig_fl;
  int m_action;
  DBusConnection *conn;
  std::map<int,int> m_keymap;
  std::string m_dbus_name;
 public:
  Keyboard();
  ~Keyboard();
  void Close();
  void Process();
  void setKeymap(std::map<int,int> keymap);
  void setDbusName(std::string dbus_name);
  void Sleep(unsigned int dwMilliSeconds);
  int getEvent();
 private:
  void restore_term();
  void send_action(int action);
  int dbus_connect();
  void dbus_disconnect();
 };
