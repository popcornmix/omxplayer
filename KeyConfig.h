#include <map>
#include <vector>
#include <string>
#include <sys/types.h>
#include <linux/joystick.h>

class KeyConfig{

  public: 
    enum {
        ACTION_DECREASE_SPEED = 1,
        ACTION_INCREASE_SPEED,
        ACTION_REWIND,
        ACTION_FAST_FORWARD,
        ACTION_SHOW_INFO,
        ACTION_PREVIOUS_AUDIO,
        ACTION_NEXT_AUDIO,
        ACTION_PREVIOUS_CHAPTER,
        ACTION_NEXT_CHAPTER,
        ACTION_PREVIOUS_SUBTITLE,
        ACTION_NEXT_SUBTITLE,
        ACTION_TOGGLE_SUBTITLE,
        ACTION_DECREASE_SUBTITLE_DELAY,
        ACTION_INCREASE_SUBTITLE_DELAY,
        ACTION_EXIT,
        ACTION_PAUSE,
        ACTION_DECREASE_VOLUME,
        ACTION_INCREASE_VOLUME,
        ACTION_SEEK_BACK_SMALL,
        ACTION_SEEK_FORWARD_SMALL,
        ACTION_SEEK_BACK_LARGE,
        ACTION_SEEK_FORWARD_LARGE,
        ACTION_STEP
    };

    #define KEY_LEFT_HEX 0x5b44
    #define KEY_RIGHT_HEX 0x5b43
    #define KEY_UP_HEX 0x5b41
    #define KEY_DOWN_HEX 0x5b42
    #define KEY_ESC_HEX 27

    KeyConfig();
    KeyConfig(char*);

    int getAction(int key);
    int getAction(std::string joystick, struct js_event *jse);

    std::vector<std::string> getJoysticks(){ return joysticks; }
    int getNumJoysticks(){ return joysticks.size(); }

  private:
    std::map<int, int> keymap;
    std::map<std::string, int> jsmap;
    std::vector<std::string> joysticks;
    int num_joysticks;

    void buildDefaultKeymap();
    void parseConfigFile(char* filepath);
};
