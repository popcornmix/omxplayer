#include <set>
#include <fstream>

#include "KeyConfig.h"

using namespace std;

/* Converts the action string from the config file into 
 * the corresponding enum value
 */
int convertStringToAction(string str_action){
    if(str_action == "DECREASE_SPEED")
        return KeyConfig::ACTION_DECREASE_SPEED;
    if(str_action == "INCREASE_SPEED")
        return KeyConfig::ACTION_INCREASE_SPEED;
    if(str_action == "REWIND")
        return KeyConfig::ACTION_REWIND;
    if(str_action == "FAST_FORWARD")
        return KeyConfig::ACTION_FAST_FORWARD;
    if(str_action == "SHOW_INFO")
        return KeyConfig::ACTION_SHOW_INFO;
    if(str_action == "PREVIOUS_AUDIO")
        return KeyConfig::ACTION_PREVIOUS_AUDIO;
    if(str_action == "NEXT_AUDIO")
        return KeyConfig::ACTION_NEXT_AUDIO;
    if(str_action == "PREVIOUS_CHAPTER")
        return KeyConfig::ACTION_PREVIOUS_CHAPTER;
    if(str_action == "NEXT_CHAPTER")
        return KeyConfig::ACTION_NEXT_CHAPTER;
    if(str_action == "PREVIOUS_SUBTITLE")
        return KeyConfig::ACTION_PREVIOUS_SUBTITLE;
    if(str_action == "NEXT_SUBTITLE")
        return KeyConfig::ACTION_NEXT_SUBTITLE;
    if(str_action == "TOGGLE_SUBTITLE")
        return KeyConfig::ACTION_TOGGLE_SUBTITLE;
    if(str_action == "DECREASE_SUBTITLE_DELAY")
        return KeyConfig::ACTION_DECREASE_SUBTITLE_DELAY;
    if(str_action == "INCREASE_SUBTITLE_DELAY")
        return KeyConfig::ACTION_INCREASE_SUBTITLE_DELAY;
    if(str_action == "EXIT")
        return KeyConfig::ACTION_EXIT;
    if(str_action == "PAUSE")
        return KeyConfig::ACTION_PAUSE;
    if(str_action == "DECREASE_VOLUME")
        return KeyConfig::ACTION_DECREASE_VOLUME;
    if(str_action == "INCREASE_VOLUME")
        return KeyConfig::ACTION_INCREASE_VOLUME;
    if(str_action == "SEEK_BACK_SMALL")
       return KeyConfig::ACTION_SEEK_BACK_SMALL;
    if(str_action == "SEEK_FORWARD_SMALL")
        return KeyConfig::ACTION_SEEK_FORWARD_SMALL;
    if(str_action == "SEEK_BACK_LARGE")
        return KeyConfig::ACTION_SEEK_BACK_LARGE;
    if(str_action == "SEEK_FORWARD_LARGE")
        return KeyConfig::ACTION_SEEK_FORWARD_LARGE;
    if(str_action == "STEP")
        return KeyConfig::ACTION_STEP;
            
    return -1;
}
/* Grabs the substring prior to the ':', this is the Action */
string getActionFromString(string line){
    string action;
    unsigned int colonIndex = line.find(":");
    if(colonIndex == string::npos)
        return "";

    action = line.substr(0,colonIndex);

    return action;
}

/* Grabs the substring after the ':', this is the Key */
string getKeyFromString(string line){
    string key;
    unsigned int colonIndex = line.find(":");
    if(colonIndex == string::npos)
        return "";
    
    key = line.substr(colonIndex+1);

    return key;
}

/* Returns a keymap consisting of the default
 *  keybinds specified with the -k option 
 */
void KeyConfig::buildDefaultKeymap(){
    keymap['1'] = ACTION_DECREASE_SPEED;
    keymap['2'] = ACTION_INCREASE_SPEED;
    keymap['<'] = ACTION_REWIND;
    keymap[','] = ACTION_REWIND;
    keymap['>'] = ACTION_FAST_FORWARD;
    keymap['.'] = ACTION_FAST_FORWARD;
    keymap['z'] = ACTION_SHOW_INFO;
    keymap['j'] = ACTION_PREVIOUS_AUDIO;
    keymap['k'] = ACTION_NEXT_AUDIO;
    keymap['i'] = ACTION_PREVIOUS_CHAPTER;
    keymap['o'] = ACTION_NEXT_CHAPTER;
    keymap['n'] = ACTION_PREVIOUS_SUBTITLE;
    keymap['m'] = ACTION_NEXT_SUBTITLE;
    keymap['s'] = ACTION_TOGGLE_SUBTITLE;
    keymap['d'] = ACTION_DECREASE_SUBTITLE_DELAY;
    keymap['f'] = ACTION_INCREASE_SUBTITLE_DELAY;
    keymap['q'] = ACTION_EXIT;
    keymap[KEY_ESC_HEX] = ACTION_EXIT;
    keymap['p'] = ACTION_PAUSE;
    keymap[' '] = ACTION_PAUSE;
    keymap['-'] = ACTION_DECREASE_VOLUME;
    keymap['+'] = ACTION_INCREASE_VOLUME;
    keymap['='] = ACTION_INCREASE_VOLUME;
    keymap[KEY_LEFT_HEX] = ACTION_SEEK_BACK_SMALL;
    keymap[KEY_RIGHT_HEX] = ACTION_SEEK_FORWARD_SMALL;
    keymap[KEY_DOWN_HEX] = ACTION_SEEK_BACK_LARGE;
    keymap[KEY_UP_HEX] = ACTION_SEEK_FORWARD_LARGE;
    keymap['v'] = ACTION_STEP;
}

/* Parses the supplied config file and adds the values to the map objects.
 * NOTE: Does not work with certain filepath shortcuts (e.g. ~ as user's home)
 */
void KeyConfig::parseConfigFile(char* filepath){
    ifstream config_file(filepath);
    string line;
    std::set<std::string> jsset;

    while(getline(config_file, line)){
        string str_action = getActionFromString(line);
        string key = getKeyFromString(line);

        if(str_action != "" && key != "" && str_action[0] != '#'){
            int key_action = convertStringToAction(str_action);
            if(key.substr(0,4) == "left"){
                keymap[KEY_LEFT_HEX] = key_action;
            }
            else if(key.substr(0,5) == "right"){
                keymap[KEY_RIGHT_HEX] = key_action;
            }
            else if(key.substr(0,2) == "up"){
                keymap[KEY_UP_HEX] = key_action;
            }
            else if(key.substr(0,4) == "down"){
                keymap[KEY_DOWN_HEX] = key_action;
            }
            else if(key.substr(0,3) == "esc"){
                keymap[KEY_ESC_HEX] = key_action;
            }
            else if(key.substr(0,3) == "hex"){
              const char *hex = key.substr(4).c_str();
              if (hex)
                keymap[strtoul(hex,0,0)] = key_action;
            }
            else if(key.substr(0,3) == "js "){
                string js_key = key.substr(3);
                jsmap[js_key] = key_action;
                string js_path = js_key.substr(0, js_key.find(' '));
                jsset.insert(js_path);
            }
            else keymap[key[0]] = key_action;
        }
    }

    //Turn the set of joysticks into a vector because it is more convenient to work with later
    joysticks.assign(jsset.begin(), jsset.end());
}

/* Default constructor that builds the default keymap */
KeyConfig::KeyConfig(){
    buildDefaultKeymap();
    num_joysticks = 0;
}

/* Constructor that takes a path to a config file. Falls back on the
 * default keymap in case of trouble.
 */
KeyConfig::KeyConfig(char* filepath){
    parseConfigFile(filepath);

    // true if config file is empty or there was an error reading it
    if(keymap.size() + jsmap.size() < 1){
        buildDefaultKeymap();
        num_joysticks = 0;
    }
    else if(jsmap.size() > 0){
        num_joysticks = joysticks.size();
    }
}

/* Finds the action associated with the provided keyboard event */
int KeyConfig::getAction(int key_pressed){
    return keymap[key_pressed];
}

/* Finds the action associated with the provided joystick/joystick event */
int KeyConfig::getAction(std::string joystick, struct js_event *jse){
    //value of 0 is either a button release or a dead joystick
    if(jse->value == 0)
        return -1;

    std::string key_string = joystick;

    if((jse->type & ~JS_EVENT_INIT) == JS_EVENT_BUTTON){
        key_string.append(" b");
        key_string.append(to_string(jse->number));
    }
    else if((jse->type & ~JS_EVENT_INIT) == JS_EVENT_AXIS){
        key_string.append(" a");
        key_string.append(to_string(jse->number));

        if(jse->value > 0)
            key_string.append("+");
        else if(jse->value < 0)
            key_string.append("-");
    }
    return jsmap[key_string];
}
