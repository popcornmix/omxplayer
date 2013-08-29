#include <map>
#include <string>

class KeyConfig
{

  public: 
    enum 
    {
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
        ACTION_STEP,
        ACTION_BLANK
    };

    #define KEY_LEFT 0x5b44
    #define KEY_RIGHT 0x5b43
    #define KEY_UP 0x5b41
    #define KEY_DOWN 0x5b42
    #define KEY_ESC 27

    static std::map<int, int> buildDefaultKeymap();
    static std::map<int, int> parseConfigFile(std::string filepath);
};
